/**
 * @file
 * @brief       App Layer state machines — implementation
 *
 * @ingroup     dcu_app
 *
 * @details     Two independent Zephyr SMF contexts, both flat (no ancestor
 *              support). The contract is in state_machine.h.
 *
 *              | Context   | Driven by            | Drives                     |
 *              |-----------|----------------------|----------------------------|
 *              | manual    | CAN `RTD_State`      | operating mode, EV screen  |
 *              | driverless| CAN `AS_state`       | DV screen                  |
 *
 *              ### Why SMF
 *              The manual context is a single transition pair today and a
 *              plain @c if would do. It is an SMF machine for consistency with
 *              the driverless context, which genuinely mirrors the vehicle's
 *              autonomous-system state and will grow per-state actions
 *              (emergency, finished) later.
 *
 *              ### Both contexts bring the driver back
 *              A driving screen is entered on its own state change, and left
 *              again when that state ends. Each context remembers the screen the
 *              driver was on when it took over and restores it afterwards. The
 *              manual one does so on entering IDLE, the only state R2D can end
 *              in; the driverless one in the exit action of AS_DRIVING, because
 *              that state can be left for any of the other five.
 *
 *              ### Not running every CAN cycle
 *              `AS_state` arrives on the bus continuously. app.c only calls
 *              state_machine_notify_as_state() when the raw value changed, and
 *              dv_run() only transitions when the mapped state differs from the
 *              current one — so the run and entry actions fire on an actual
 *              state change, not on every frame.
 *
 *              ### Event delivery
 *              SMF has no event queue. Each notify function drops its input
 *              into the context struct, calls smf_run_state() once, then
 *              invalidates it — so a state's @c run function only ever sees the
 *              input that was just posted.
 *
 *              ### The two contexts do not yet coordinate
 *              Both can publish a screen switch. In practice the selected
 *              mission makes them mutually exclusive (Manual Driving vs. a DV
 *              discipline); a guard for that is deliberately left out for now.
 *
 * @author      Mario Wegmann <mario.wegmann@web.de>
 * @date        Created: 2026-09-07
 *
 * @copyright   Copyright (c) 2026 Mario Wegmann.
 *              SPDX-License-Identifier: Apache-2.0
 */

/* ── Corresponding Header ────────────────────────────────────────────────────────────────────── */

#include "app/state_machine.h"

/* ── Zephyr Includes ─────────────────────────────────────────────────────────────────────────── */

#include <zephyr/kernel.h>
#include <zephyr/smf.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/logging/log.h>

/* ── Project Includes ────────────────────────────────────────────────────────────────────────── */

#include "app/app_state.h"
#include "services/event_bus/event_bus.h"
#include "services/event_bus/events.h"

/* ── Zephyr Logging ──────────────────────────────────────────────────────────────────────────── */

LOG_MODULE_REGISTER(state_machine, CONFIG_LOG_DEFAULT_LEVEL);


/* ── Shared Helper ───────────────────────────────────────────────────────────────────────────── */

/**
 * @brief Ask the UI module to load a screen.
 *
 * The state machine never touches LVGL; it publishes on ui_nav_chan and the UI
 * thread does the work on its next tick.
 *
 * @param screen  Screen to load.
 */
static void request_screen(enum screen_id screen)
{
    const struct ui_nav_cmd cmd = { .screen = screen };

    int ret = zbus_chan_pub(&ui_nav_chan, &cmd, K_NO_WAIT);
    if (ret != 0) {
        LOG_ERR("ui_nav_chan publish failed: %d", ret);
    }
}


/* ════════════════════════════════════════════════════════════════════════════════════════════════
 * Manual / EV context
 * ════════════════════════════════════════════════════════════════════════════════════════════════ */

/** @brief Index into manual_states[]. */
enum manual_state_id {
    MANUAL_STATE_IDLE = 0, /**< Not driving; full screen navigation.        */
    MANUAL_STATE_RTD,      /**< Vehicle in R2D; EV driving screen.              */
};

/**
 * @brief SMF context plus the single pending event.
 *
 * @c smf_ctx must be the first member — SMF_CTX() casts the object pointer to
 * it.
 */
struct manual_sm {
    struct smf_ctx ctx;           /**< SMF bookkeeping.                        */
    enum sm_event  event;         /**< Event currently being processed.        */
    bool           event_valid;   /**< True only for the duration of one post. */
    enum screen_id return_screen; /**< Screen to restore when R2D ends.        */
};

static struct manual_sm s_manual;

static const struct smf_state manual_states[];

/**
 * @brief Enter idle — operating mode DEBUG, all screens reachable.
 *
 * Coming back from RTD, this restores the screen the driver was on before the
 * car went ready-to-drive; the UI deletes the EV screen as part of that switch.
 * On the initial transition there is nothing to restore — the UI has not even
 * built its first screen yet — and the request is skipped.
 *
 * @param obj  The manual_sm.
 */
static void manual_idle_entry(void *obj)
{
    struct manual_sm *o = obj;

    app_state_set_mode(OPERATING_MODE_DEBUG);

    if (o->return_screen != SCREEN_NONE) {
        request_screen(o->return_screen);
        o->return_screen = SCREEN_NONE;
    }

    LOG_INF("SM manual: IDLE");
}

/**
 * @brief Idle run — the only exit is the vehicle reporting R2D.
 * @param obj  The manual_sm.
 * @return SMF_EVENT_HANDLED (flat machine — the result is not propagated).
 */
static enum smf_state_result manual_idle_run(void *obj)
{
    struct manual_sm *o = obj;

    if (o->event_valid && o->event == SM_EVENT_VEHICLE_RTD) {
        smf_set_state(SMF_CTX(o), &manual_states[MANUAL_STATE_RTD]);
    }

    return SMF_EVENT_HANDLED;
}

/**
 * @brief Enter RTD — record the mode and pull the driver onto the EV screen.
 *
 * The screen the driver is leaving is remembered, so the end of R2D can put
 * them back there. Two values are not worth returning to: SCREEN_NONE, when
 * the car was already in R2D before the UI built anything, and EV_DRIVING
 * itself, which cannot happen today but would strand the driver there; both
 * fall back to the boot screen.
 *
 * The mode has no effect on the CAN bus. Leaving the PRE_RTD screen releases
 * the RTD button (see screen_checklist.c), which is harmless here: the vehicle
 * is already in R2D. The UI module locks the carousel and repurposes the left
 * encoder as a side effect of EV_DRIVING being the active screen.
 *
 * @param obj  The manual_sm.
 */
static void manual_rtd_entry(void *obj)
{
    struct manual_sm *o = obj;

    enum screen_id from = app_state_get_active_screen();

    o->return_screen = (from == SCREEN_NONE || from == SCREEN_EV_DRIVING)
                       ? SCREEN_BOOT
                       : from;

    app_state_set_mode(OPERATING_MODE_RTD);
    request_screen(SCREEN_EV_DRIVING);
    LOG_INF("SM manual: RTD — EV driving (return to %d)", (int)o->return_screen);
}

/**
 * @brief RTD run — the only exit is the vehicle leaving R2D.
 * @param obj  The manual_sm.
 * @return SMF_EVENT_HANDLED (flat machine — the result is not propagated).
 */
static enum smf_state_result manual_rtd_run(void *obj)
{
    struct manual_sm *o = obj;

    if (o->event_valid && o->event == SM_EVENT_VEHICLE_IDLE) {
        smf_set_state(SMF_CTX(o), &manual_states[MANUAL_STATE_IDLE]);
    }

    return SMF_EVENT_HANDLED;
}

static const struct smf_state manual_states[] = {
    [MANUAL_STATE_IDLE] =
        SMF_CREATE_STATE(manual_idle_entry, manual_idle_run, NULL, NULL, NULL),
    [MANUAL_STATE_RTD] =
        SMF_CREATE_STATE(manual_rtd_entry, manual_rtd_run, NULL, NULL, NULL),
};


/* ════════════════════════════════════════════════════════════════════════════════════════════════
 * Driverless (AS_state) context
 * ════════════════════════════════════════════════════════════════════════════════════════════════ */

/**
 * @brief The autonomous-system states the DCU mirrors.
 *
 * Order is the index into dv_states[], not a wire format — the mapping from the
 * CAN raw value lives in as_state_from_raw(). All transitions are currently
 * allowed: any raw-value change moves straight to the mapped state.
 */
enum dv_state_id {
    DV_STATE_AS_OFF = 0,     /**< Autonomous system off.            */
    DV_STATE_MANUAL_DRIVING, /**< Car driven manually.              */
    DV_STATE_AS_READY,       /**< Armed, waiting for the go signal. */
    DV_STATE_AS_DRIVING,     /**< Autonomous mission running.       */
    DV_STATE_AS_FINISHED,    /**< Mission completed.                */
    DV_STATE_AS_EMERGENCY,   /**< Emergency brake / fault.          */
    DV_STATE_COUNT,
};

struct dv_sm {
    struct smf_ctx    ctx;           /**< SMF bookkeeping.                     */
    enum dv_state_id  target;        /**< State the last notify mapped to.     */
    bool              target_valid;  /**< True only for the duration of one post. */
    enum screen_id    return_screen; /**< Screen to restore when AS_DRIVING ends. */
};

static struct dv_sm s_dv;

static const struct smf_state dv_states[];

/**
 * @brief Map the raw 3-bit AS_state signal onto @ref dv_state_id.
 *
 * The DBC carries no value table, so this mapping is project knowledge. Adjust
 * it here — nothing else in the firmware interprets the raw value.
 *
 * @param raw  AS_state as decoded from DV_system_status (0..7).
 * @return The state it stands for; DV_STATE_AS_OFF for an unknown value.
 */
static enum dv_state_id as_state_from_raw(uint8_t raw)
{
    switch (raw) {
    case 0:  return DV_STATE_MANUAL_DRIVING;
    case 1:  return DV_STATE_AS_OFF;
    case 2:  return DV_STATE_AS_READY;
    case 3:  return DV_STATE_AS_DRIVING;
    case 4:  return DV_STATE_AS_EMERGENCY;
    case 5:  return DV_STATE_AS_FINISHED;
    default:
        LOG_WRN("Unknown AS_state raw value %u — treating as AS_OFF", (unsigned)raw);
        return DV_STATE_AS_OFF;
    }
}

/**
 * @brief Enter AS_DRIVING — remember the current screen, show the DV driving one.
 *
 * The screen the driver is leaving is remembered so that dv_as_driving_exit()
 * can put them back there, the same way manual_rtd_entry() does for R2D. Three
 * values are not worth returning to and fall back to the boot screen:
 * SCREEN_NONE, when the vehicle was already in AS driving before the UI built
 * anything, and the two driving screens. Both are reached only through this
 * kind of state change, and returning to one whose state has ended would strand
 * the driver on it — the EV screen would be restored after the manual machine
 * had already left R2D.
 *
 * @param obj  The dv_sm.
 */
static void dv_as_driving_entry(void *obj)
{
    struct dv_sm *o = obj;

    enum screen_id from = app_state_get_active_screen();

    o->return_screen = (from == SCREEN_NONE       ||
                        from == SCREEN_DV_DRIVING ||
                        from == SCREEN_EV_DRIVING)
                       ? SCREEN_BOOT
                       : from;

    request_screen(SCREEN_DV_DRIVING);
    LOG_INF("SM dv: AS_DRIVING — DV driving screen (return to %d)",
            (int)o->return_screen);
}

/**
 * @brief Leave AS_DRIVING — restore the screen the driver came from.
 *
 * An exit action rather than a part of the entry of whatever state follows:
 * AS_DRIVING can be left for any of the other five, and this way the screen
 * comes back for all of them, in one place. The UI deletes the DV screen as
 * part of the switch.
 *
 * @param obj  The dv_sm.
 */
static void dv_as_driving_exit(void *obj)
{
    struct dv_sm *o = obj;

    if (o->return_screen != SCREEN_NONE) {
        request_screen(o->return_screen);
        LOG_INF("SM dv: leaving AS_DRIVING — back to screen %d", (int)o->return_screen);
        o->return_screen = SCREEN_NONE;
    }
}

/**
 * @brief Log the entry to any DV state that has no action of its own.
 * @param obj  The dv_sm.
 */
static void dv_state_log_entry(void *obj)
{
    struct dv_sm *o = obj;

    LOG_INF("SM dv: state %d", (int)(o->ctx.current - dv_states));
}

/**
 * @brief Shared run for every DV state — transition to the notified target.
 *
 * A no-op unless a fresh notify arrived and it maps to a different state, so
 * a steady AS_state costs nothing.
 *
 * @param obj  The dv_sm.
 * @return SMF_EVENT_HANDLED.
 */
static enum smf_state_result dv_run(void *obj)
{
    struct dv_sm *o = obj;

    if (o->target_valid && &dv_states[o->target] != o->ctx.current) {
        smf_set_state(SMF_CTX(o), &dv_states[o->target]);
    }

    return SMF_EVENT_HANDLED;
}

static const struct smf_state dv_states[] = {
    [DV_STATE_AS_OFF] =
        SMF_CREATE_STATE(dv_state_log_entry, dv_run, NULL, NULL, NULL),
    [DV_STATE_MANUAL_DRIVING] =
        SMF_CREATE_STATE(dv_state_log_entry, dv_run, NULL, NULL, NULL),
    [DV_STATE_AS_READY] =
        SMF_CREATE_STATE(dv_state_log_entry, dv_run, NULL, NULL, NULL),
    [DV_STATE_AS_DRIVING] =
        SMF_CREATE_STATE(dv_as_driving_entry, dv_run, dv_as_driving_exit, NULL, NULL),
    [DV_STATE_AS_FINISHED] =
        SMF_CREATE_STATE(dv_state_log_entry, dv_run, NULL, NULL, NULL),
    [DV_STATE_AS_EMERGENCY] =
        SMF_CREATE_STATE(dv_state_log_entry, dv_run, NULL, NULL, NULL),
};


/* ── Public Function Implementations ─────────────────────────────────────────────────────────── */

void state_machine_init(void)
{
    smf_set_initial(SMF_CTX(&s_manual), &manual_states[MANUAL_STATE_IDLE]);
    smf_set_initial(SMF_CTX(&s_dv), &dv_states[DV_STATE_AS_OFF]);
    LOG_INF("State machines initialized");
}

void state_machine_post(enum sm_event event)
{
    s_manual.event       = event;
    s_manual.event_valid = true;

    int32_t ret = smf_run_state(SMF_CTX(&s_manual));

    s_manual.event_valid = false;

    if (ret != 0) {
        LOG_ERR("Manual state machine terminated: %d", (int)ret);
    }
}

void state_machine_notify_as_state(uint8_t as_state_raw)
{
    s_dv.target       = as_state_from_raw(as_state_raw);
    s_dv.target_valid = true;

    int32_t ret = smf_run_state(SMF_CTX(&s_dv));

    s_dv.target_valid = false;

    if (ret != 0) {
        LOG_ERR("Driverless state machine terminated: %d", (int)ret);
    }
}
