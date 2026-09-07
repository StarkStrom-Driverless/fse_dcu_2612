/**
 * @file        screen_checklist.h
 * @brief       Pre-RTD screen factory — carries the Ready-to-Drive button
 *
 * @ingroup     dcu_ui_screens
 *
 * @details     Registered for SCREEN_PRE_RTD.  The name is historical: the
 *              screen is intended to become the guided pre-drive checklist,
 *              but at present it holds nothing but the RTD button.
 *
 *              This is the only screen from which Ready-to-Drive can be
 *              requested.
 *
 *              ### Screen layout (480 × 320)
 *              ```
 *              ┌──────────────────────────────────────┐
 *              │ PRE RTD                       ▪▪▪▪▪▪ │ ← shared header
 *              ├──────────────────────────────────────┤
 *              │                                      │
 *              │            (checklist to follow)     │
 *              │                                      │
 *              │                   ┌───────────┐      │
 *              │                   │    RTD    │      │ ← dedicated RTD pad
 *              │                   └───────────┘      │
 *              └──────────────────────────────────────┘```
 *
 *              ### Input assignment
 *
 *              | Input         | Drives                                      |
 *              |---------------|---------------------------------------------|
 *              | Left encoder  | Screen carousel — handled in ui.c, not here |
 *              | Right encoder | Empty group; nothing to focus yet           |
 *              | Middle button | The RTD button (dedicated keypad_rtd pad)    |
 *              | Left / right buttons | Nothing; the accessors return NULL   |
 *
 *              ### Long press to enter RTD
 *              RTD latches. A long press of the dedicated RTD button publishes
 *              UI_INPUT_RTD_REQUEST; the App Layer's state machine raises
 *              operating mode RTD (→ CAN RTD_Button bit) and switches the
 *              display to EV DRIVING, which replaces this screen. There is no
 *              release event and no way back short of a power cycle. Requiring
 *              a long press keeps a brush against the button from starting the
 *              car.
 *
 * @author      Mario Wegmann <mario.wegmann@web.de>
 * @date        Created: 2026-06-08
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
 * 0.1.0    2026-06-08  Mario Wegmann   Initial creation
 * ─────────────────────────────────────────────────────────────────────────────────────────────────
 */

#ifndef MODULES_UI_SCREENS_SCREEN_CHECKLIST_H
#define MODULES_UI_SCREENS_SCREEN_CHECKLIST_H

/* ── LVGL Include ────────────────────────────────────────────────────────────────────────────── */

#include <lvgl.h>


/* ── Public Function Declarations ────────────────────────────────────────────────────────────── */

/**
 * @brief Create the pre-RTD screen.
 *
 * Builds the header, the RTD button and the input groups.
 * Must be called after ui_styles_init().
 *
 * @param status_subjects  Device-status subjects for the header widget.
 * @return                 Pointer to the top-level screen object. Never NULL.
 */
lv_obj_t *screen_checklist_create(lv_subject_t *status_subjects);

/**
 * @brief Return the LVGL input group for the right encoder.
 *
 * Created but empty — the screen has nothing to focus yet. Returning an empty
 * group rather than NULL keeps the encoder attached, so widgets added here
 * later become reachable without touching ui.c.
 *
 * @return  The group. Valid only after screen_checklist_create().
 */
lv_group_t *screen_checklist_get_right_encoder_group(void);

/**
 * @brief Return the LVGL input group for the left button pad.
 * @return Always NULL — this screen has nothing on the left pad.
 */
lv_group_t *screen_checklist_get_left_button_group(void);

/**
 * @brief Return the LVGL input group for the right button pad.
 * @return Always NULL — the RTD button is on the dedicated RTD pad instead.
 */
lv_group_t *screen_checklist_get_right_button_group(void);

/**
 * @brief Return the LVGL input group for the dedicated RTD button pad.
 *
 * Contains the RTD button, in edit mode. ui.c binds this to the keypad_rtd
 * device for the PRE_RTD screen only.
 *
 * @return  The group. Valid only after screen_checklist_create().
 */
lv_group_t *screen_checklist_get_rtd_button_group(void);

#endif /* MODULES_UI_SCREENS_SCREEN_CHECKLIST_H */