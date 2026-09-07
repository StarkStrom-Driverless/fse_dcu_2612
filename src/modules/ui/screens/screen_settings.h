/**
 * @file        screen_settings.h
 * @brief       Settings screen factory — edit every persistent setting
 *
 * @ingroup     dcu_ui_screens
 *
 * @details     One editable row per entry in the generated settings schema
 *              (src/generated/settings_schema_gen.h): the values the code
 *              generator derives from the `persist:` blocks and the top-level
 *              `settings:` section of dbc/dcu_app.yaml. Adding a setting there
 *              and rerunning the generator makes a new row appear here without
 *              a change to this file.
 *
 *              ### Screen layout (480 × 320)
 *
 *              ```
 *              ┌──────────────────────────────────────┐
 *              │ DV SETTINGS                   ▪▪▪▪▪▪ │ ← shared header
 *              ├──────────────────────────────────────┤
 *              │      ┌────────────────────────┐      │
 *              │      │ Debug Bits          3  │      │ ← focused row: gold
 *              │  ┌─┐ │ ASR                 0  │ ┌─┐  │
 *              │  │-│ │ Recuperation        1  │ │+│  │ ← left / right button pad
 *              │  └─┘ │ Torque Vectoring    0  │ └─┘  │
 *              │      │ Power Limit         2  │      │
 *              │      │ Display Brightness 80  │      │
 *              │      └────────────────────────┘      │
 *              └──────────────────────────────────────┘
 *              ```
 *
 *              ### Input assignment
 *
 *              | Input         | Drives                                       |
 *              |---------------|----------------------------------------------|
 *              | Left encoder  | Screen carousel — handled in ui.c, not here  |
 *              | Right encoder | Moves the row focus (group not in edit mode) |
 *              | Left buttons  | "-" — lower the focused setting              |
 *              | Right buttons | "+" — raise the focused setting              |
 *
 *              ### Selecting versus applying
 *              There is no separate confirm step: "-" and "+" clamp the new
 *              value against the schema bounds and publish
 *              UI_INPUT_SETTING_SELECTED straight away. The App Layer hands it
 *              to the settings service, which owns the value, clamps it a
 *              second time and coalesces the flash write. The row shows the
 *              value optimistically once the publish succeeded, the same way
 *              the other screens update their confirmed-value labels.
 *
 *              ### State across visits
 *              The screen is destroyed on leaving. The row values are seeded
 *              from settings_get() on every build, so they always reflect the
 *              live settings; the focused row is remembered in the file-scope
 *              s_sel_idx and restored on the next visit.
 *
 * @author      Mario Wegmann <mario.wegmann@web.de>
 * @date        Created: 2026-09-01
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
 * 0.1.0    2026-09-01  Mario Wegmann   Initial creation
 * ─────────────────────────────────────────────────────────────────────────────────────────────────
 */

#ifndef MODULES_UI_SCREENS_SCREEN_SETTINGS_H
#define MODULES_UI_SCREENS_SCREEN_SETTINGS_H

/* ── LVGL Include ────────────────────────────────────────────────────────────────────────────── */

#include <lvgl.h>


/* ── Public Function Declarations ────────────────────────────────────────────────────────────── */

/**
 * @brief Create the settings screen.
 *
 * Builds the header, one editable row per schema entry, the "-" / "+" buttons
 * and the three input groups. Must be called after ui_styles_init() and after
 * settings_service_init(), since the rows are seeded with settings_get().
 *
 * @param status_subjects  Device-status subjects for the header widget.
 * @return                 Pointer to the top-level screen object. Never NULL.
 */
lv_obj_t *screen_settings_create(lv_subject_t *status_subjects);

/**
 * @brief Return the LVGL input group for the right encoder.
 *
 * Holds every setting row and is deliberately *not* in edit mode, so a turn of
 * the encoder moves the focus from row to row.
 *
 * @return  The group. Valid only after screen_settings_create().
 */
lv_group_t *screen_settings_get_right_encoder_group(void);

/**
 * @brief Return the LVGL input group for the left button pad.
 *
 * Contains the "-" button, which lowers the focused setting.
 *
 * @return  The group. Valid only after screen_settings_create().
 */
lv_group_t *screen_settings_get_left_button_group(void);

/**
 * @brief Return the LVGL input group for the right button pad.
 *
 * Contains the "+" button, which raises the focused setting.
 *
 * @return  The group. Valid only after screen_settings_create().
 */
lv_group_t *screen_settings_get_right_button_group(void);

#endif /* MODULES_UI_SCREENS_SCREEN_SETTINGS_H */
