/**
 * @file        screen_debug_write.h
 * @brief       Debug-bits transmit screen factory
 *
 * @ingroup     dcu_ui_screens
 *
 * @details     The one screen that writes to the vehicle for diagnostic
 *              purposes: it sets the three-bit Debug_SETTING signal that the
 *              CAN module puts into every DCU_2_mABX frame.
 *
 *              ### Screen layout (480 × 320)
 *
 *              ```
 *              ┌──────────────────────────────────────┐
 *              │ DBG TX                        ▪▪▪▪▪▪ │ ← shared header
 *              ├──────────────────────────────────────┤
 *              │              ┌─────────┐             │
 *              │              │    2    │             │ ← roller 0…7,
 *              │              │▶   3   ◀│             │   right encoder
 *              │              │    4    │             │
 *              │              └─────────┘             │
 *              │        Current Debug Bits: 3         │ ← last confirmed
 *              │                   ┌───────────────┐  │
 *              │                   │   SET BITS    │  │ ← right button pad
 *              │                   └───────────────┘  │
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

#ifndef MODULES_UI_SCREENS_SCREEN_DEBUG_WRITE_H
#define MODULES_UI_SCREENS_SCREEN_DEBUG_WRITE_H

/* ── LVGL Include ────────────────────────────────────────────────────────────────────────────── */

#include <lvgl.h>


/* ── Public Function Declarations ────────────────────────────────────────────────────────────── */

/**
 * @brief Create the debug-bits transmit screen.
 *
 * Builds the header, the 0…7 roller with its confirmed-value label, the
 * SET BITS button and the two input groups.
 * Must be called after ui_styles_init().
 *
 * @param status_subjects  Device-status subjects for the header widget.
 * @return                 Pointer to the top-level screen object. Never NULL.
 */
lv_obj_t *screen_debug_write_create(lv_subject_t *status_subjects);

/**
 * @brief Return the LVGL input group for the right encoder.
 *
 * Contains the roller only, in edit mode, so a turn scrolls the value list.
 *
 * @return  The group. Valid only after screen_debug_write_create().
 */
lv_group_t *screen_debug_write_get_right_encoder_group(void);

/**
 * @brief Return the LVGL input group for the left button pad.
 * @return Always NULL — this screen has nothing on the left pad.
 */
lv_group_t *screen_debug_write_get_left_button_group(void);

/**
 * @brief Return the LVGL input group for the right button pad.
 *
 * Contains the SET BITS button.
 *
 * @return  The group. Valid only after screen_debug_write_create().
 */
lv_group_t *screen_debug_write_get_right_button_group(void);

#endif /* MODULES_UI_SCREENS_SCREEN_DEBUG_WRITE_H */