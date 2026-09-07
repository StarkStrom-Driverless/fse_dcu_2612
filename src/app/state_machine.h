/**
 * @file        state_machine.h
 * @brief       App Layer operating-mode state machine (Zephyr SMF)
 *
 * @ingroup     dcu_app
 *
 * @details     Owns the mode transitions that used to live inline in app.c's
 *              Zbus handlers. The App thread feeds it; the state entry actions
 *              write app_state and publish the screen switches that follow.
 *
 *              Two independent SMF contexts:
 *
 *              ### Manual / EV driving  — driven by the RTD button
 *
 *              | State         | operating_mode        | On entry         |
 *              |---------------|-----------------------|------------------|
 *              | `MANUAL_IDLE` | `OPERATING_MODE_DEBUG` | –                |
 *              | `MANUAL_RTD`  | `OPERATING_MODE_RTD`   | load `EV_DRIVING` |
 *
 *              `MANUAL_IDLE → MANUAL_RTD` on @ref SM_EVENT_RTD_REQUEST. No
 *              transition back: leaving RTD means power-cycling the car.
 *
 *              ### Driverless  — driven by the CAN `AS_state` signal
 *
 *              Six states: AS off, manual driving, AS ready, AS driving, AS
 *              finished, AS emergency. All transitions are allowed; the state
 *              follows whatever `AS_state` reports. Only *AS driving* has an
 *              action so far — it loads `DV_DRIVING`, which then stays up until
 *              power-off like the EV screen.
 *
 *              state_machine_notify_as_state() is called only when the raw
 *              value changed (app.c does the edge check), and the machine only
 *              transitions when the mapped state actually differs — so a steady
 *              `AS_state` at 100 ms costs nothing.
 *
 *              ### Threading
 *              Both notify functions must be called from the App thread only —
 *              the SMF contexts are not guarded. app.c satisfies this: every
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
 * 0.2.0    2026-09-07  Mario Wegmann   Driverless (AS_state) context added
 * ─────────────────────────────────────────────────────────────────────────────────────────────────
 */

#ifndef APP_STATE_MACHINE_H
#define APP_STATE_MACHINE_H

/* ── Standard Includes ───────────────────────────────────────────────────────────────────────── */

#include <stdint.h>


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
 * @brief Initialise both state machines in their start states.
 *
 * Manual → MANUAL_IDLE (sets @c OPERATING_MODE_DEBUG, already the app_state
 * default). Driverless → AS_OFF.
 *
 * Call once from app_module_init(), after app_state_init().
 */
void state_machine_init(void);

/**
 * @brief Deliver one event to the manual state machine and run it.
 *
 * A transition (exit of the old state, entry of the new one) happens inside
 * this call. App thread context only.
 *
 * @param event  The event to process.
 */
void state_machine_post(enum sm_event event);

/**
 * @brief Tell the driverless state machine the current raw AS_state value.
 *
 * Maps the value onto an internal state and transitions there if it differs
 * from the current one. Call only when the raw value changed; App thread
 * context only.
 *
 * @param as_state_raw  AS_state as decoded from DV_system_status (0..7).
 */
void state_machine_notify_as_state(uint8_t as_state_raw);

#endif /* APP_STATE_MACHINE_H */
