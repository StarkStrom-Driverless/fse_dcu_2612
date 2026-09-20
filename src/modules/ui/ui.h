/**
 * @file
 * @brief       Public interface for the UI module
 *
 * @ingroup     dcu_ui
 *
 * @details     The UI module owns the LVGL task thread, the screen lifecycle,
 *              and the encoder-driven screen carousel.
 *
 *              ### Responsibilities
 *              – Initialize the shared styles and the generated LVGL subjects.
 *              – Run lv_timer_handler() in a dedicated thread (priority 8).
 *                Every LVGL call in the firmware happens in that thread.
 *              – Create screens on first visit and delete them on leaving, so
 *                only one screen occupies RAM at a time.
 *              – Navigate the screen carousel via the LEFT encoder
 *                (INPUT_REL_WHEEL, handled through Zephyr's input callback).
 *              – Route the RIGHT encoder and the two button pads to the input
 *                groups the active screen provides.
 *              – Follow ui_nav_chan (App → UI) for screen switches, ui_cmd_chan
 *                for CAN data pushes and feedback, and can_status_chan for the
 *                CAN icon in the header.
 *
 *              ### Input devices (devicetree aliases)
 *
 *              | Alias / node     | Routed to                                   |
 *              |------------------|---------------------------------------------|
 *              | qdec_input_left  | Screen carousel (ui.c callback) — except on the EV driving screen, where lvgl_encoder0 drives the TQG Front slider |
 *              | qdec_input_right | Focused widget of the active screen         |
 *              | keypad_left      | Left button pad → active screen's group     |
 *              | keypad_right     | Right button pad → active screen's group    |
 *              | keypad_rtd       | Dedicated RTD button → PRE_RTD screen only  |
 *
 *              Buttons act on LVGL widgets. A screen decides what that means
 *              and publishes the semantic event on ui_input_chan itself; the
 *              UI module does not translate key presses into ui_input events.
 *
 * @author      Mario Wegmann <mario.wegmann@web.de>
 * @date        Created: 2026-06-02
 *
 * @copyright   Copyright (c) 2026 Mario Wegmann.
 *              SPDX-License-Identifier: Apache-2.0
 */

#ifndef MODULES_UI_UI_H
#define MODULES_UI_UI_H

/* ── Standard Includes ───────────────────────────────────────────────────────────────────────── */

#include <stdint.h>

/* ── LVGL Include ────────────────────────────────────────────────────────────────────────────── */

#include <lvgl.h>

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
 * @brief Initialize the UI module.
 *
 * Performs the following steps in order:
 *  1. Initialize the shared LVGL styles (ui_styles_init()).
 *  2. Initialize the generated RX and TX subjects and the device-status
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
 * App thread must already be ready to receive.  LVGL itself is initialized
 * earlier by the Zephyr display driver.
 */
void ui_module_init(void);


/* ── Carousel Introspection ──────────────────────────────────────────────────────────────────── */

/**
 * @brief Position value for "the active screen is not a carousel stop".
 *
 * Returned by ui_carousel_get_position() on EV_DRIVING and DV_DRIVING, which
 * are reached through ui_nav_chan rather than by paging. The page indicator
 * then highlights no segment instead of leaving the previous one lit.
 */
#define UI_CAROUSEL_POS_NONE  0xFFU

/*
 * The carousel is private to ui.c — these two only report on it, so a screen or
 * widget can show the driver where they are without being able to move them.
 * The page indicator in the header widget is the one consumer.
 *
 * Both are safe to call while a screen is being built, which is the only time
 * they are called: screen construction happens in the UI thread, and the very
 * first one runs on the main thread before that thread exists. Neither case
 * competes with the navigation that writes the position.
 */

/**
 * @brief Number of screens reachable through the screen carousel.
 * @return Length of the carousel; constant for the runtime.
 */
uint8_t ui_carousel_get_length(void);

/**
 * @brief Index of the current screen within the carousel.
 *
 * The carousel position itself is remembered while the driver is on a screen
 * outside the carousel — that is where paging resumes once they return — but
 * it is not reported as the current position, because it is not where they
 * are.
 *
 * @return Zero-based index below ui_carousel_get_length(), or
 *         @ref UI_CAROUSEL_POS_NONE if the active screen is not in the
 *         carousel.
 */
uint8_t ui_carousel_get_position(void);

/**
 * @brief Create an input group that lives and dies with a screen.
 *
 * LVGL keeps every group in a global list and frees none of them by itself:
 * deleting the widgets takes them out of their group, but the group stays.
 * A screen that is built on every visit and creates its groups with
 * lv_group_create() therefore leaks a little each time, until the LVGL memory
 * pool runs dry and the firmware faults.
 *
 * A group made here is deleted together with @p owner. The deletion also
 * detaches it from any input device that still points at it.
 *
 * The callback carries the group itself, not a file-scope pointer, so a screen
 * that is rebuilt before the old instance is deleted cannot free the new
 * instance's groups.
 *
 * @param owner  The screen object the group belongs to.
 * @return       The new group, or NULL if the LVGL pool is exhausted.
 */
lv_group_t *ui_group_create(lv_obj_t *owner);

/** @} */ /* dcu_ui */

#endif /* MODULES_UI_UI_H */
