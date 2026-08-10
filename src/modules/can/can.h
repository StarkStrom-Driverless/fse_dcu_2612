/**
 * @file        can.h
 * @brief       Public interface for the CAN module
 *
 * @details     The CAN module owns the CAN controller hardware, sends outgoing
 *              frames on demand, and publishes decoded signal snapshots and
 *              bus status events onto the Zbus event bus.
 *
 *              Downward channel consumed (App → CAN):
 *                can_tx_cmd_chan  — commands to transmit frames
 *
 *              Upward channels produced (CAN → App):
 *                can_status_chan  — bus connectivity and error state
 *                can_data_chan    — decoded RX signal snapshot (future)
 *
 *              The module runs its own thread (priority 3, stack 1024 B).
 *              Callers must not use any Zephyr CAN driver API directly.
 *
 * @author      Mario Wegmann <mario.wegmann@web.de>
 * @date        Created: 2026-06-02
 *
 * @version     0.1.0
 *
 * @copyright   Copyright (c) 2026 Mario Wegmann
 *              SPDX-License-Identifier: Apache-2.0
 *
 * @note        Target RTOS : Zephyr RTOS (https://zephyrproject.org)
 *              UI Library  : LVGL (https://lvgl.io)
 *
 * ─────────────────────────────────────────────────────────────────────────────────────────────────
 * Revision History
 * ─────────────────────────────────────────────────────────────────────────────────────────────────
 * Version  Date        Author          Description
 * 0.1.0    2026-06-02  Mario Wegmann   Initial creation
 * ─────────────────────────────────────────────────────────────────────────────────────────────────
 */

#ifndef MODULES_CAN_CAN_H
#define MODULES_CAN_CAN_H


/* ── Public Function Declarations ────────────────────────────────────────────────────────────── */

/**
 * @brief Initialise the CAN module.
 *
 * Configures the CAN controller (1 Mbps, normal mode), starts the
 * controller, spawns the CAN worker thread, and subscribes to
 * can_tx_cmd_chan on the Zbus event bus.
 *
 * Must be called once from main() after app_state_init() and before
 * any other module or the App thread is started.
 *
 * @note If the CAN device is not ready (e.g., not defined in the
 *       devicetree), an error is logged and the module remains inactive.
 *       The firmware continues; only CAN functionality is unavailable.
 */
void can_module_init(void);

#endif /* MODULES_CAN_CAN_H */
