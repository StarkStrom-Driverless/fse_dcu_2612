/**
 * @file        screen_debug_custom.h
 * @brief       Generic value screen factory — read two, write one
 *
 * @ingroup     dcu_ui_screens
 *
 * @details     A scratch screen for generic values: two received ones on the
 *              left, one transmitted one on the right. None of the three has a
 *              fixed meaning — the point is that an engineer can bind something
 *              to them between runs and read or set it from the wheel without
 *              touching the firmware.
 *
 *              ### Screen layout (480 × 320)
 *
 *              ```
 *              ┌──────────────────────────────────────┐
 *              │ DBG CUSTOM                    ▪▪▪▪▪▪ │ ← shared header
 *              ├──────────────────────────────────────┤
 *              │ DCU_Custom_Wert_1     ┌─────────┐    │
 *              │ 1234                  │    2    │    │ ← roller 0…7,
 *              │                       │▶   3   ◀│    │   right encoder
 *              │ DCU_Custom_Wert_2     │    4    │    │
 *              │ 5678                  └─────────┘    │
 *              │                  Current Debug Bits: 3
 *              │                       ┌───────────┐  │
 *              │                       │ SET BITS  │  │ ← right button pad
 *              │                       └───────────┘  │
 *              └──────────────────────────────────────┘
 *              ```
 *
 *              ### Input assignment
 *
 *              | Input         | Drives                                      |
 *              |---------------|---------------------------------------------|
 *              | Left encoder  | Screen carousel — handled in ui.c, not here |
 *              | Right encoder | The roller, permanently in edit mode        |
 *              | Right buttons | The SET BITS button                         |
 *              | Left buttons  | Nothing; the group accessor returns NULL    |
 *
 *              The read column is display-only and takes no input at all.
 *
 *              ### Selecting versus confirming
 *              Scrolling the roller changes nothing outside this screen. Only
 *              SET BITS publishes UI_INPUT_DEBUG_BITS_SELECTED, which the App
 *              Layer hands to the settings service; the CAN module reads the
 *              stored value on its next TX cycle. The label under the roller
 *              shows what was last confirmed.
 *
 * @author      Mario Wegmann <mario.wegmann@web.de>
 * @date        Created: 2026-06-09
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
 * 0.1.0    2026-06-09  Mario Wegmann   Initial creation
 * ─────────────────────────────────────────────────────────────────────────────────────────────────
 */

#ifndef MODULES_UI_SCREENS_SCREEN_DEBUG_CUSTOM_H
#define MODULES_UI_SCREENS_SCREEN_DEBUG_CUSTOM_H

/* ── LVGL Include ────────────────────────────────────────────────────────────────────────────── */

#include <lvgl.h>


/* ── Public Function Declarations ────────────────────────────────────────────────────────────── */

/**
 * @brief Create the generic value screen.
 *
 * Builds the header, the two read labels, the 0…7 roller with its
 * confirmed-value label, the SET BITS button and the two input groups.
 * Must be called after ui_styles_init().
 *
 * @param status_subjects  Device-status subjects for the header widget.
 * @return                 Pointer to the top-level screen object. Never NULL.
 */
lv_obj_t *screen_debug_custom_create(lv_subject_t *status_subjects);

/**
 * @brief Return the LVGL input group for the right encoder.
 *
 * Contains the roller only, in edit mode, so a turn scrolls the value list.
 *
 * @return  The group. Valid only after screen_debug_custom_create().
 */
lv_group_t *screen_debug_custom_get_right_encoder_group(void);

/**
 * @brief Return the LVGL input group for the left button pad.
 * @return Always NULL — this screen has nothing on the left pad.
 */
lv_group_t *screen_debug_custom_get_left_button_group(void);

/**
 * @brief Return the LVGL input group for the right button pad.
 *
 * Contains the SET BITS button.
 *
 * @return  The group. Valid only after screen_debug_custom_create().
 */
lv_group_t *screen_debug_custom_get_right_button_group(void);

#endif /* MODULES_UI_SCREENS_SCREEN_DEBUG_CUSTOM_H */