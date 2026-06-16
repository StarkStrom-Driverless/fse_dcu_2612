/**
 * @file        ui.h
 * @brief       Public interface for the UI module
 *
 * @details     The UI module owns the LVGL task thread, all screen objects, and
 *              the encoder-driven screen carousel.
 *
 *              Responsibilities
 *              ────────────────
 *              – Initialise LVGL styles and create all screen objects.
 *              – Run lv_timer_handler() in a dedicated thread (priority 8).
 *              – Navigate the screen carousel via the LEFT encoder
 *                (INPUT_REL_WHEEL, handled through Zephyr's input callback).
 *              – Assign the RIGHT encoder's LVGL indev to the active screen's
 *                input group (in-screen widget navigation).
 *              – Subscribe to ui_cmd_chan (App → UI) for programmatic screen
 *                switches, CAN data pushes, and status updates.
 *
 *              Hardware layout (from board overlay)
 *              ─────────────────────────────────────
 *              Left  encoder  (qdec_input0 / lvgl_encoder0) : screen carousel
 *              Right encoder  (qdec_input1 / lvgl_encoder1) : in-screen roller
 *              Buttons: ESC → UI_INPUT_BACK
 *                       OK  → LV_KEY_ENTER (via keypad indev)
 *                       RTD → UI_INPUT_RTD_REQUEST (input module, future)
 *                       TS  → UI_INPUT_TIMESTAMP   (input module, future)
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
 * @brief Initialise the UI module.
 *
 * Performs the following steps in order:
 *  1. Initialise all shared LVGL styles (ui_styles_init).
 *  2. Create every MVP screen object.
 *  3. Load the boot screen and render the first frame.
 *  4. Enable the display (display_blanking_off).
 *  5. Locate the right encoder LVGL indev.
 *  6. Start the LVGL task thread.
 *
 * Must be called from main() after can_module_init() and before the App
 * thread is started.  LVGL must already be initialised by the Zephyr display
 * driver before this function is called.
 */
void ui_module_init(void);
