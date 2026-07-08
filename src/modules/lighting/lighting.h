/**
 * @file        lighting.h
 * @brief       Public interface for the Lighting module
 *
 * @details     The Lighting module owns the APA102 LED strip and renders
 *              visual effects in a dedicated thread (priority 7).
 *
 *              Subscribes to lighting_cmd_chan (App → Lighting) for state and
 *              effect commands (not yet implemented — KITT scanner runs by
 *              default on startup).
 *
 * @author      Mario Wegmann <mario.wegmann@web.de>
 * @date        Created: 2026-07-06
 *
 * @version     0.1.0
 *
 * @copyright   Copyright (c) 2026 Mario Wegmann
 *              SPDX-License-Identifier: Apache-2.0
 *
 * @note        Target RTOS : Zephyr RTOS (https://zephyrproject.org)
 *
 * ─────────────────────────────────────────────────────────────────────────────────────────────────
 * Revision History
 * ─────────────────────────────────────────────────────────────────────────────────────────────────
 * Version  Date        Author          Description
 * 0.1.0    2026-07-06  Mario Wegmann   Initial creation — KITT scanner effect
 * ─────────────────────────────────────────────────────────────────────────────────────────────────
 */

#ifndef MODULES_LIGHTING_LIGHTING_H
#define MODULES_LIGHTING_LIGHTING_H


/* ── Public Function Declarations ────────────────────────────────────────────────────────────── */

/**
 * @brief Initialise the Lighting module.
 *
 * Verifies the LED strip device is ready and starts the lighting thread.
 * Must be called from main() after app_module_init().
 *
 * If the LED strip device is not ready the function returns without starting
 * the thread; all other modules continue normally.
 */
void lighting_module_init(void);


#endif /* MODULES_LIGHTING_LIGHTING_H */
