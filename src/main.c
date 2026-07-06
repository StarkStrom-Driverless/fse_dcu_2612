/**
 * @file        main.c
 * @brief       Firmware entry point — module initialisation sequence
 *
 * @details     main() is the single place that wires all modules together in
 *              the correct order.  All real work happens in the threads spawned
 *              by the module init functions; main() exits immediately after
 *              the last init call and the Zephyr kernel takes over.
 *
 *              Initialisation order
 *              ─────────────────────
 *              1. app_module_init()
 *                 Resets app_state to safe defaults and starts the App (Dirigent)
 *                 thread.  The app_sub Zbus subscriber must be active before any
 *                 other module begins publishing, so that no events are silently
 *                 dropped.
 *
 *              2. can_module_init()
 *                 Configures the CAN controller, starts it, publishes
 *                 CAN_STATUS_CONNECTED to can_status_chan, and spawns the CAN
 *                 worker thread.  The App thread (step 1) is already running and
 *                 will receive the status event.
 *
 *              3. ui_module_init()
 *                 Creates all LVGL screen objects, loads the boot screen, enables
 *                 the display, and starts the LVGL task thread.  UI is started
 *                 last because it generates user-input events that the App thread
 *                 must be ready to handle.
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

/* ── Zephyr Includes ─────────────────────────────────────────────────────────────────────────── */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

/* ── Project Includes ────────────────────────────────────────────────────────────────────────── */

#include "app/app.h"
#include "modules/can/can.h"
#include "modules/lighting/lighting.h"
#include "modules/ui/ui.h"

/* ── Zephyr Logging ──────────────────────────────────────────────────────────────────────────── */

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);


/* ── Entry Point ─────────────────────────────────────────────────────────────────────────────── */

int main(void)
{
    LOG_INF("FSE DCU 2612 — firmware starting");

    /*
     * Step 1: App Layer (Dirigent)
     *
     * Initialises app_state to safe defaults and starts the App thread.
     * Must run first so the Zbus subscriber queue is draining before
     * any module starts publishing events.
     */
    app_module_init();

    /*
     * Step 2: CAN module
     *
     * Configures and starts the CAN controller, then publishes
     * CAN_STATUS_CONNECTED.  The App thread is already running at this
     * point and will process the status event immediately.
     */
    can_module_init();

    /*
     * Step 3: Lighting module
     *
     * Starts the lighting thread and begins the KITT scanner effect.
     * Must run before the UI module so LEDs are active when the display
     * shows the boot screen.
     */
    lighting_module_init();

    /*
     * Step 4: UI module
     *
     * Creates LVGL screen objects, renders the first frame, enables the
     * display, and starts the LVGL task thread.  Started last because
     * encoder and button events flow towards the App thread which must
     * already be active.
     *
     * Note: All modules prior to UI must be initialised first so that no
     * upward Zbus events are dropped during the UI startup phase.
     */
    ui_module_init();

    LOG_INF("All modules initialised — kernel takes over");

    /*
     * main() returns here; all further execution is in the module threads
     * (App thread pri 5, CAN thread pri 3, LVGL task thread pri 8).
     * Zephyr keeps the system alive.
     */
    return 0;
}
