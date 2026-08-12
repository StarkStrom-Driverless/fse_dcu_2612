/**
 * @file        ui.h
 * @brief       Public interface for the UI module
 *
 * @ingroup     dcu_ui
 *
 * @details     The UI module owns the LVGL task thread, the screen lifecycle,
 *              and the encoder-driven screen carousel.
 *
 *              ### Responsibilities
 *              – Initialise the shared styles and the generated LVGL subjects.
 *              – Run lv_timer_handler() in a dedicated thread (priority 8).
 *                Every LVGL call in the firmware happens in that thread.
 *              – Create screens on first visit and delete them on leaving, so
 *                only one screen occupies RAM at a time.
 *              – Navigate the screen carousel via the LEFT encoder
 *                (INPUT_REL_WHEEL, handled through Zephyr's input callback).
 *              – Route the RIGHT encoder and the two button pads to the input
 *                groups the active screen provides.
 *              – Subscribe to ui_cmd_chan (App → UI) for programmatic screen
 *                switches and CAN data pushes, and to can_status_chan for the
 *                CAN icon in the header.
 *
 *              ### Input devices (devicetree aliases)
 *
 *              | Alias            | Routed to                                   |
 *              |------------------|---------------------------------------------|
 *              | qdec_input_left  | Screen carousel — consumed in ui.c, never handed to LVGL |
 *              | qdec_input_right | Focused widget of the active screen         |
 *              | keypad_left      | Left button pad → active screen's group     |
 *              | keypad_right     | Right button pad → active screen's group    |
 *
 *              Buttons act on LVGL widgets. A screen decides what that means
 *              and publishes the semantic event on ui_input_chan itself; the
 *              UI module does not translate key presses into ui_input events.
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

#ifndef MODULES_UI_UI_H
#define MODULES_UI_UI_H

/**
 * @defgroup dcu_ui UI module
 * @ingroup  dcu_modules
 * @brief LVGL thread, screen lifecycle, input routing and the screen carousel.
 *
 * All LVGL API calls in the firmware happen in the thread this module owns.
 * Data reaches widgets through LVGL subjects rather than through direct
 * updates, so screens do not poll and do not need refresh calls.
 * @{
 */


/* ── Public Function Declarations ────────────────────────────────────────────────────────────── */

/**
 * @brief Initialise the UI module.
 *
 * Performs the following steps in order:
 *  1. Initialise the shared LVGL styles (ui_styles_init()).
 *  2. Initialise the generated RX and TX subjects and the device-status
 *     subjects — screens bind to them while being built, so they must exist
 *     first.
 *  3. Resolve the display device for backlight control.
 *  4. Build and load the boot screen.
 *  5. Resolve the encoder and keypad input devices.
 *  6. Start the LVGL task thread.
 *
 * The first frame is deliberately *not* rendered here: lv_timer_handler() runs
 * in the new thread, on its own 16 kB stack, because rendering the large fonts
 * on the main thread's stack overflows it.  The backlight follows the first
 * frame for the same reason.
 *
 * Call last in main() — the screens it builds publish input events that the
 * App thread must already be ready to receive.  LVGL itself is initialised
 * earlier by the Zephyr display driver.
 */
void ui_module_init(void);

/** @} */ /* dcu_ui */

#endif /* MODULES_UI_UI_H */
