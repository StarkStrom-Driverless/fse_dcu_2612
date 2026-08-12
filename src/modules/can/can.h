/**
 * @file        can.h
 * @brief       Public interface for the CAN module
 *
 * @ingroup     dcu_can
 *
 * @details     The CAN module owns the CAN controller hardware.  It transmits
 *              on a fixed schedule and publishes decoded signal snapshots and
 *              bus status events onto the Zbus event bus.
 *
 *              Upward channels produced (CAN → App):
 *                can_status_chan  — bus connectivity and controller state
 *                can_data_chan    — decoded RX signal snapshot
 *
 *              The module subscribes to nothing.  Transmission is not command
 *              driven: the worker thread reads mission and operating mode from
 *              app_state and the persistent values from the settings service
 *              on each cycle, so can_tx_cmd_chan stays unused.
 *
 *              The module runs its own thread (priority 3, stack 2 kB).
 *              Callers must not use any Zephyr CAN driver API directly.
 *
 * @author      Mario Wegmann <mario.wegmann@web.de>
 * @date        Created: 2026-06-02
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
 * 0.1.0    2026-06-02  Mario Wegmann   Initial creation
 * ─────────────────────────────────────────────────────────────────────────────────────────────────
 */

#ifndef MODULES_CAN_CAN_H
#define MODULES_CAN_CAN_H

/**
 * @defgroup dcu_can CAN module
 * @ingroup  dcu_modules
 * @brief CAN controller ownership, periodic TX, RX decoding and bus status.
 *
 * Raw frames never leave this module: everything outside sees decoded
 * snapshots on can_data_chan. All frame layouts come from the code generator,
 * so adding a message means editing dbc/dcu_app.yaml, not this code.
 * @{
 */


/* ── Public Function Declarations ────────────────────────────────────────────────────────────── */

/**
 * @brief Initialise the CAN module.
 *
 * Configures the CAN controller (1 Mbit/s, normal mode), installs one hardware
 * RX filter per generated RX frame ID, starts the controller, publishes the
 * initial CAN_STATUS_CONNECTED, and spawns the CAN worker thread.
 *
 * Call once from main() after app_module_init() and settings_service_init():
 * the App thread has to be draining its subscriber queue to catch the status
 * event, and the first transmitted frame reads the settings.
 *
 * @note If the CAN device is not ready (e.g., not defined in the devicetree)
 *       or configuration fails, an error is logged and the worker thread exits
 *       on its first run.  The firmware continues; only CAN is unavailable.
 */
void can_module_init(void);

/** @} */ /* dcu_can */

#endif /* MODULES_CAN_CAN_H */
