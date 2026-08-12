/**
 * @file        app.h
 * @brief       Public interface for the App Layer (Dirigent)
 *
 * @ingroup     dcu_app
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
 *              The whole module surface is this one init function; everything
 *              else reaches the App Layer through the event bus.
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

#ifndef APP_APP_H
#define APP_APP_H


/* ── Public Function Declarations ────────────────────────────────────────────────────────────── */

/**
 * @ingroup dcu_app
 * @brief Initialise the App Layer.
 *
 * Calls app_state_init() to reset the global state to safe defaults, then
 * spawns the App thread (priority 5), which blocks on the Zbus subscriber
 * queue for the rest of the runtime.
 *
 * Must be the first init call in main(): every module that publishes to an
 * upward Zbus channel needs the subscriber queue to be draining already,
 * otherwise its early events are dropped.
 */
void app_module_init(void);

#endif /* APP_APP_H */
