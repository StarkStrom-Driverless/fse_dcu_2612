/**
 * @file        state_machine.h
 * @brief       App Layer operating-mode state machine (Zephyr SMF)
 *
 * @ingroup     dcu_app
 *
 * @details     Owns the operating-mode transitions that used to live inline in
 *              app.c's UI-input handler. The App thread feeds it semantic
 *              events; the state entry actions write app_state and publish the
 *              screen switches that follow from a mode change.
 *
 *              ### States (manual / EV driving)
 *
 *              | State            | operating_mode        | On entry            |
 *              |------------------|-----------------------|---------------------|
 *              | `MANUAL_IDLE`    | `OPERATING_MODE_DEBUG` | –                   |
 *              | `MANUAL_RTD`     | `OPERATING_MODE_RTD`   | load `EV_DRIVING`    |
 *
 *              `MANUAL_IDLE → MANUAL_RTD` on @ref SM_EVENT_RTD_REQUEST. There is
 *              no transition back: leaving RTD means power-cycling the car. The
 *              carousel lock on the EV driving screen and the repurposed left
 *              encoder are a UI-module consequence of that screen being active,
 *              not something this module drives.
 *
 *              ### Not here yet
 *              A second context for driverless operation, mirroring the CAN
 *              `AS_state` signal, will be added to this same module. The event
 *              enum and the run/entry split are shaped so it slots in beside
 *              the manual context rather than replacing it.
 *
 *              ### Threading
 *              state_machine_post() must be called from the App thread only —
 *              the SMF context is not guarded. app.c satisfies this: every
 *              caller sits in a Zbus handler on that thread.
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

#ifndef APP_STATE_MACHINE_H
#define APP_STATE_MACHINE_H


/* ── Public Types ────────────────────────────────────────────────────────────────────────────── */

/**
 * @brief Events the App thread can feed into the state machine.
 *
 * Kept separate from @ref ui_input_type so the state machine does not depend on
 * the shape of the UI event payloads. app.c maps the one onto the other.
 */
enum sm_event {
    SM_EVENT_RTD_REQUEST = 0, /**< Driver long-pressed the RTD button. */
};


/* ── Public Function Declarations ────────────────────────────────────────────────────────────── */

/**
 * @brief Initialise the state machine in its idle state.
 *
 * Runs the entry action of @c MANUAL_IDLE, which sets
 * @c OPERATING_MODE_DEBUG — already the app_state default, so this is a no-op
 * on a fresh boot and a real reset only if it is ever re-run.
 *
 * Call once from app_module_init(), after app_state_init().
 */
void state_machine_init(void);

/**
 * @brief Deliver one event to the state machine and run the current state.
 *
 * A transition (exit of the old state, entry of the new one) happens inside
 * this call. App thread context only.
 *
 * @param event  The event to process.
 */
void state_machine_post(enum sm_event event);

#endif /* APP_STATE_MACHINE_H */
