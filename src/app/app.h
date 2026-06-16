/**
 * @file        app.h
 * @brief       Public interface for the App Layer (Dirigent)
 *
 * @details     The App Layer is the sole writer of the global application state
 *              (app_state.h) and the central coordinator between all modules.
 *
 *              It subscribes to every upward Zbus channel (Module → App),
 *              updates the application state accordingly, and publishes commands
 *              to the downward channels (App → Module) as needed.
 *
 *              No module other than src/app/ may call app_state_set_*() functions.
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

#pragma once


/* ── Public Function Declarations ────────────────────────────────────────────────────────────── */

/**
 * @brief Initialise the App Layer.
 *
 * Calls app_state_init() to reset the global state to safe defaults, then
 * spawns the App thread which blocks on the Zbus subscriber queue.
 *
 * Must be called from main() before any module that publishes to an upward
 * Zbus channel is started; otherwise early events may be dropped.
 */
void app_module_init(void);
