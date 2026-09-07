/**
 * @file        state_machine.c
 * @brief       App Layer state machines — implementation
 *
 * @ingroup     dcu_app
 *
 * @details     Two independent Zephyr SMF contexts, both flat (no ancestor
 *              support). The contract is in state_machine.h.
 *
 *              | Context   | Driven by            | Drives                     |
 *              |-----------|----------------------|----------------------------|
 *              | manual    | RTD button           | operating mode, EV screen  |
 *              | driverless| CAN `AS_state`       | DV screen                  |
 *
 *              ### Why SMF
 *              The manual context is a single one-way transition today and a
 *              plain @c if would do. It is an SMF machine for consistency with
 *              the driverless context, which genuinely mirrors the vehicle's
 *              autonomous-system state and will grow per-state actions
 *              (emergency, finished) later.
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
 * @version     0.1.0
 *
 * @copyright   Copyright (c) 2026 Mario Wegmann.
 *              SPDX-License-Identifier: Apache-2.0
 */

/*
 * ─────────────────────────────────────────────────────────────────────────────────────────────────
 * Revision History
 * Version  Date        Author          Description
 * 0.1.0    2026-09-07  Mario Wegmann   Initial creation
 * 0.2.0    2026-09-07  Mario Wegmann   Driverless (AS_state) context added
 * ─────────────────────────────────────────────────────────────────────────────────────────────────
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
 * The state machine never touches LVGL; it publishes the same ui_cmd a
 * carousel move would, and the UI thread does the work on its next tick.
 *
 * @param screen  Screen to load.
 */
static void request_screen(enum screen_id screen)
{
    struct ui_cmd cmd = {
        .type        = UI_CMD_SET_SCREEN,
        .data.screen = screen,
    };

    int ret = zbus_chan_pub(&ui_cmd_chan, &cmd, K_NO_WAIT);
    if (ret != 0) {
        LOG_ERR("ui_cmd_chan publish failed: %d", ret);
    }
}


/* ════════════════════════════════════════════════════════════════════════════════════════════════
 * Manual / EV context
 * ════════════════════════════════════════════════════════════════════════════════════════════════ */

/** @brief Index into manual_states[]. */
enum manual_state_id {
    MANUAL_STATE_IDLE = 0, /**< Not driving; full screen navigation.        */
    MANUAL_STATE_RTD,      /**< RTD latched; EV driving screen; no way back. */
};

/**
 * @brief SMF context plus the single pending event.
 *
 * @c smf_ctx must be the first member — SMF_CTX() casts the object pointer to
 * it.
 */
struct manual_sm {
    struct smf_ctx ctx;         /**< SMF bookkeeping.                        */
    enum sm_event  event;       /**< Event currently being processed.        */
    bool           event_valid; /**< True only for the duration of one post. */
};

static struct manual_sm s_manual;

static const struct smf_state manual_states[];

/**
 * @brief Enter idle — operating mode DEBUG, all screens reachable.
 * @param obj  Unused.
 */
static void manual_idle_entry(void *obj)
{
    ARG_UNUSED(obj);

    app_state_set_mode(OPERATING_MODE_DEBUG);
    LOG_INF("SM manual: IDLE");
}

/**
 * @brief Idle run — the only exit is a driver RTD request.
 * @param obj  The manual_sm.
 * @return SMF_EVENT_HANDLED (flat machine — the result is not propagated).
 */
static enum smf_state_result manual_idle_run(void *obj)
{
    struct manual_sm *o = obj;

    if (o->event_valid && o->event == SM_EVENT_RTD_REQUEST) {
        smf_set_state(SMF_CTX(o), &manual_states[MANUAL_STATE_RTD]);
    }

    return SMF_EVENT_HANDLED;
}

/**
 * @brief Enter RTD — raise the mode and pull the driver onto the EV screen.
 *
 * The CAN module turns @c OPERATING_MODE_RTD into the RTD_Button bit on its
 * next TX cycle; the UI module locks the carousel and repurposes the left
 * encoder as a side effect of EV_DRIVING being the active screen.
 *
 * @param obj  Unused.
 */
static void manual_rtd_entry(void *obj)
{
    ARG_UNUSED(obj);

    app_state_set_mode(OPERATING_MODE_RTD);
    request_screen(SCREEN_EV_DRIVING);
    LOG_INF("SM manual: RTD — EV driving");
}

/**
 * @brief RTD run — terminal for now; leaving RTD means a power cycle.
 * @param obj  Unused.
 * @return SMF_EVENT_HANDLED.
 */
static enum smf_state_result manual_rtd_run(void *obj)
{
    ARG_UNUSED(obj);

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
    struct smf_ctx    ctx;          /**< SMF bookkeeping.                     */
    enum dv_state_id  target;       /**< State the last notify mapped to.     */
    bool              target_valid; /**< True only for the duration of one post. */
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
 * @brief Enter AS_DRIVING — show the DV driving screen.
 *
 * The only DV state with an action so far. The screen is not on the carousel
 * and nothing switches away from it, so once shown it stays until power-off,
 * even if AS_state later moves on to FINISHED or EMERGENCY.
 *
 * @param obj  Unused.
 */
static void dv_as_driving_entry(void *obj)
{
    ARG_UNUSED(obj);

    request_screen(SCREEN_DV_DRIVING);
    LOG_INF("SM dv: AS_DRIVING — DV driving screen");
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
        SMF_CREATE_STATE(dv_as_driving_entry, dv_run, NULL, NULL, NULL),
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
    LOG_INF("State machines initialised");
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
