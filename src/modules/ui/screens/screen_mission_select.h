/**
 * @file        screen_mission_select.h
 * @brief       Mission selection screen factory
 *
 * @ingroup     dcu_ui_screens
 *
 * @details     Lets the driver pick the autonomous-driving mission and send it
 *              to the vehicle.
 *
 *              ### Screen layout (480 × 320)
 *
 *              ```
 *              ┌──────────────────────────────────────┐
 *              │ DV MISSION                    ▪▪▪▪▪▪ │ ← shared header
 *              ├──────────────────────────────────────┤
 *              │         ┌─────────────────┐          │
 *              │         │  Acceleration   │          │ ← roller,
 *              │         │▶ Skidpad       ◀│          │   right encoder
 *              │         │  Trackdrive     │          │
 *              │         └─────────────────┘          │
 *              │      Current Mission: Skidpad        │ ← last confirmed
 *              │                                      │
 *              │                   ┌───────────────┐  │
 *              │                   │  SET MISSION  │  │ ← right button pad
 *              │                   └───────────────┘  │
 *              └──────────────────────────────────────┘
 *              ```
 *
 *              ### Input assignment
 *
 *              | Input         | Drives                                        |
 *              |---------------|-----------------------------------------------|
 *              | Left encoder  | Screen carousel — handled in ui.c, not here   |
 *              | Right encoder | The roller, permanently in edit mode, so a turn scrolls the list instead of moving focus |
 *              | Right buttons | The SET MISSION button                        |
 *              | Left buttons  | Nothing; the group accessor returns NULL      |
 *
 *              ### Selecting versus confirming
 *              Scrolling the roller changes nothing outside this screen — no
 *              event, no CAN frame. Only SET MISSION publishes
 *              UI_INPUT_MISSION_SELECTED, and the App Layer stores that in
 *              app_state, from where the CAN module picks it up on its next
 *              cycle. The label under the roller shows what was last
 *              confirmed, so the driver can see selection and confirmation
 *              disagree.
 *
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

#ifndef MODULES_UI_SCREENS_SCREEN_MISSION_SELECT_H
#define MODULES_UI_SCREENS_SCREEN_MISSION_SELECT_H

/* ── LVGL Include ────────────────────────────────────────────────────────────────────────────── */

#include <lvgl.h>


/* ── Public Function Declarations ────────────────────────────────────────────────────────────── */

/**
 * @brief Create the mission selection screen.
 *
 * Builds the header, the mission roller with its confirmed-mission label, the
 * SET MISSION button and the two input groups.
 * Must be called after ui_styles_init().
 *
 * @param status_subjects  Device-status subjects for the header widget.
 * @return                 Pointer to the top-level screen object. Never NULL.
 */
lv_obj_t *screen_mission_select_create(lv_subject_t *status_subjects);

/**
 * @brief Return the LVGL input group for the right encoder.
 *
 * Contains the roller only, and is created in edit mode so a turn scrolls the
 * mission list rather than moving focus.
 *
 * ui.c assigns this group to the right encoder when the screen becomes active
 * and detaches it on leaving.
 *
 * @return  The group. Valid only after screen_mission_select_create().
 */
lv_group_t *screen_mission_select_get_right_encoder_group(void);

/**
 * @brief Return the LVGL input group for the left button pad.
 * @return Always NULL — this screen has nothing on the left pad.
 */
lv_group_t *screen_mission_select_get_left_button_group(void);

/**
 * @brief Return the LVGL input group for the right button pad.
 *
 * Contains the SET MISSION button.
 *
 * @return  The group. Valid only after screen_mission_select_create().
 */
lv_group_t *screen_mission_select_get_right_button_group(void);

#endif /* MODULES_UI_SCREENS_SCREEN_MISSION_SELECT_H */