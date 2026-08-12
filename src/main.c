/**
 * @file        main.c
 * @brief       Firmware entry point — module initialisation sequence
 *
 * @details     main() is the single place that wires all modules together in
 *              the correct order.  All real work happens in the threads spawned
 *              by the module init functions; main() returns immediately after
 *              the last init call and the Zephyr kernel takes over.
 *
 *              ### Initialisation order
 *
 *              Each step is a prerequisite of the ones after it:
 *
 *              1. **app_module_init()** — resets app_state to safe defaults and
 *                 starts the App (Dirigent) thread. The app_sub Zbus subscriber
 *                 must be draining before any other module begins publishing,
 *                 otherwise early events are silently dropped.
 *              2. **settings_service_init()** — loads persisted settings from
 *                 flash, or the schema defaults, and publishes
 *                 SETTINGS_EVT_LOADED. Must precede CAN and UI so both see the
 *                 stored values from their very first cycle.
 *              3. **can_module_init()** — configures and starts the CAN
 *                 controller, publishes CAN_STATUS_CONNECTED, and spawns the CAN
 *                 worker thread. The App thread from step 1 is already running
 *                 and receives the status event.
 *              4. **audio_module_init()** and
 *                 5. **lighting_module_init()** — piezo and LED strip, both
 *                 before the UI so feedback is available from the first screen
 *                 interaction.
 *              6. **ui_module_init()** — initialises the shared styles and LVGL
 *                 subjects, builds and loads the boot screen, and starts the
 *                 LVGL task thread, which renders the first frame and switches
 *                 the backlight on. Last, because the screens it builds publish
 *                 input events that the App thread must be ready to handle.
 *
 *              ### Resulting threads
 *
 *              | Thread   | Priority | Source                     |
 *              |----------|----------|----------------------------|
 *              | can      | 3        | modules/can/can.c          |
 *              | app      | 5        | app/app.c                  |
 *              | audio    | 6        | modules/audio/audio.c      |
 *              | lighting | 7        | modules/lighting/lighting.c|
 *              | ui_lvgl  | 8        | modules/ui/ui.c            |
 *
 *              A module whose hardware is unavailable logs the reason and stays
 *              inactive; no init step aborts the boot.
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

/**
 * @defgroup dcu_app App Layer
 * @brief Dirigent coordinator and the global application state.
 *
 * The App Layer is the only writer of app_state.  It consumes every upward
 * Zbus channel (Module → App) and answers with downward commands
 * (App → Module).  Modules never talk to each other directly.
 */

/**
 * @defgroup dcu_modules Modules
 * @brief Hardware-facing feature modules: CAN, UI, lighting, audio.
 *
 * Each module owns its peripheral exclusively, runs its own thread, and
 * exposes nothing but a `<name>_module_init()` function.  Everything else
 * crosses module boundaries through the event bus.
 */

/**
 * @defgroup dcu_services Services
 * @brief Cross-cutting infrastructure: event bus and persistent settings.
 *
 * Services carry no vehicle logic.  They provide the transport (Zbus
 * channels) and the storage (settings) that the App Layer and the modules
 * build on.
 */

/* ── Zephyr Includes ─────────────────────────────────────────────────────────────────────────── */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

/* ── Project Includes ────────────────────────────────────────────────────────────────────────── */

#include "app/app.h"
#include "modules/audio/audio.h"
#include "modules/can/can.h"
#include "modules/lighting/lighting.h"
#include "modules/ui/ui.h"
#include "services/settings/settings.h"

/* ── Zephyr Logging ──────────────────────────────────────────────────────────────────────────── */

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);


/* ── Entry Point ─────────────────────────────────────────────────────────────────────────────── */

/**
 * @brief Firmware entry point — initialise every module in dependency order.
 *
 * Each step is a prerequisite of the ones after it; the ordering rationale is
 * documented per call site below and summarised in the file header.
 *
 * Returning does not end the program: the module threads created here keep
 * running and the Zephyr kernel takes over the main thread's slot.
 *
 * @return Always 0.
 */
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
     * Step 2: Settings service
     *
     * Loads persisted settings from flash, or falls back to schema defaults.
     * Must precede the CAN module so the first DCU_2_mABX frame carries the
     * stored values, and precede the UI so screens bind correct values while
     * being constructed.  Never fails hard — on a storage error the system
     * continues on defaults.
     */
    settings_service_init();

    /*
     * Step 3: CAN module
     *
     * Configures and starts the CAN controller, then publishes
     * CAN_STATUS_CONNECTED.  The App thread is already running at this
     * point and will process the status event immediately.
     */
    can_module_init();

    /*
     * Step 4: Audio module
     *
     * Configures the piezo GPIO and starts the audio subscriber thread.
     * Initialised before the UI so audio feedback is available from first
     * screen interaction.
     */
    audio_module_init();

    /*
     * Step 5: Lighting module
     *
     * Starts the lighting thread and begins the rotating gear effect.
     * Must run before the UI module so LEDs are active when the display
     * shows the boot screen.
     */
    lighting_module_init();

    /*
     * Step 6: UI module
     *
     * Initialises styles and LVGL subjects, builds and loads the boot screen,
     * and starts the LVGL task thread — that thread renders the first frame
     * and switches the backlight on.  Started last because encoder and button
     * events flow towards the App thread which must already be active.
     *
     * Note: All modules prior to UI must be initialised first so that no
     * upward Zbus events are dropped during the UI startup phase.
     */
    ui_module_init();

    LOG_INF("All modules initialised — kernel takes over");

    /*
     * main() returns here; all further execution is in the module threads
     * (CAN pri 3, App pri 5, audio pri 6, lighting pri 7, LVGL task pri 8).
     * Zephyr keeps the system alive.
     */
    return 0;
}
