/**
 * @file        state_machine.c
 * @brief       App Layer operating-mode state machine — implementation
 *
 * @ingroup     dcu_app
 *
 * @details     Zephyr SMF, flat (no ancestor support). The contract and the
 *              state table are described in state_machine.h.
 *
 *              ### Why SMF for two states
 *              Today the manual context is a single one-way transition and a
 *              plain @c if would do. It is written as an SMF state machine
 *              because the driverless context that lands next is genuinely
 *              stateful (it mirrors the vehicle's @c AS_state), and both
 *              contexts sharing one framework keeps the entry/exit discipline
 *              consistent.
 *
 *              ### Event delivery
 *              SMF has no event queue of its own. state_machine_post() drops
 *              the event into the context struct, calls smf_run_state() once,
 *              then clears it — so a state's @c run function only ever sees the
 *              event that was just posted, never a stale one.
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


/* ── State Identifiers ───────────────────────────────────────────────────────────────────────── */

/** @brief Index into sm_states[]. */
enum sm_state_id {
    SM_STATE_MANUAL_IDLE = 0, /**< Not driving; full screen navigation.        */
    SM_STATE_MANUAL_RTD,      /**< RTD latched; EV driving screen; no way back. */
};


/* ── State Machine Context ───────────────────────────────────────────────────────────────────── */

/**
 * @brief SMF context plus the single pending event.
 *
 * @c smf_ctx must be the first member — SMF_CTX() casts the object pointer to
 * it.
 */
struct sm_object {
    struct smf_ctx ctx;         /**< SMF bookkeeping.                           */
    enum sm_event  event;       /**< Event currently being processed.           */
    bool           event_valid; /**< True only for the duration of one post.    */
};

static struct sm_object s_obj;

/* Forward declaration — the state functions reference the table and vice versa. */
static const struct smf_state sm_states[];


/* ── Helpers ─────────────────────────────────────────────────────────────────────────────────── */

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


/* ── State: MANUAL_IDLE ──────────────────────────────────────────────────────────────────────── */

/**
 * @brief Enter idle — operating mode DEBUG, all screens reachable.
 * @param obj  Unused; the entry action needs no context.
 */
static void manual_idle_entry(void *obj)
{
    ARG_UNUSED(obj);

    app_state_set_mode(OPERATING_MODE_DEBUG);
    LOG_INF("SM: MANUAL_IDLE");
}

/**
 * @brief Idle run — the only exit is a driver RTD request.
 * @param obj  The sm_object.
 * @return SMF_EVENT_HANDLED (flat machine — the result is not propagated).
 */
static enum smf_state_result manual_idle_run(void *obj)
{
    struct sm_object *o = obj;

    if (o->event_valid && o->event == SM_EVENT_RTD_REQUEST) {
        smf_set_state(SMF_CTX(o), &sm_states[SM_STATE_MANUAL_RTD]);
    }

    return SMF_EVENT_HANDLED;
}


/* ── State: MANUAL_RTD ───────────────────────────────────────────────────────────────────────── */

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
    LOG_INF("SM: MANUAL_RTD — EV driving");
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


/* ── State Table ─────────────────────────────────────────────────────────────────────────────── */

static const struct smf_state sm_states[] = {
    [SM_STATE_MANUAL_IDLE] =
        SMF_CREATE_STATE(manual_idle_entry, manual_idle_run, NULL, NULL, NULL),
    [SM_STATE_MANUAL_RTD] =
        SMF_CREATE_STATE(manual_rtd_entry, manual_rtd_run, NULL, NULL, NULL),
};


/* ── Public Function Implementations ─────────────────────────────────────────────────────────── */

void state_machine_init(void)
{
    smf_set_initial(SMF_CTX(&s_obj), &sm_states[SM_STATE_MANUAL_IDLE]);
    LOG_INF("State machine initialised");
}

void state_machine_post(enum sm_event event)
{
    s_obj.event       = event;
    s_obj.event_valid = true;

    int32_t ret = smf_run_state(SMF_CTX(&s_obj));

    s_obj.event_valid = false;

    if (ret != 0) {
        LOG_ERR("State machine terminated: %d", (int)ret);
    }
}
