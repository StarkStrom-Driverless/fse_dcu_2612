/**
 * @file        screen_settings.h
 * @brief       Settings screen factory — edit every persistent setting
 *
 * @ingroup     dcu_ui_screens
 *
 * @details     One slider per entry in the generated settings schema
 *              (src/generated/settings_schema_gen.h): the values the code
 *              generator derives from the `persist:` blocks and the top-level
 *              `settings:` section of dbc/dcu_app.yaml. Adding a setting there
 *              and rerunning the generator makes a new row appear here without
 *              a change to this file.
 *
 *              The one schema entry this screen does *not* edit is
 *              SETTING_DEBUG_BITS. It belongs to DBG CUSTOM, which is also the
 *              screen that spells out what each bit does; having two editors
 *              for the same value only invited disagreement about which one was
 *              authoritative.
 *
 *              ### Screen layout (480 × 320)
 *
 *              ```
 *              ┌──────────────────────────────────────┐
 *              │ DV SETTINGS                   ▪▪▪▪▪▪ │ ← shared header
 *              ├──────────────────────────────────────┤
 *              │      ASR                ▓▓░░░░░   0  │
 *              │  ┌─┐ Recuperation       ▓▓▓▓░░░   1  │
 *              │  │-│ Torque Vectoring   ▓▓░░░░░   0  │ ← focused: gold bar
 *              │  └─┘ Power Limit        ▓▓▓▓▓░░   2  │ ┌─┐
 *              │      Display Brightness ▓▓▓▓▓▓░  80  │ │+│
 *              │                                      │ └─┘
 *              │            Settings saved            │ ← 1.5 s after the write
 *              ├──────────────────────────────────────┤
 *              │ hint bar                             │
 *              └──────────────────────────────────────┘
 *              ```
 *
 *              ### Input assignment
 *
 *              | Input         | Drives                                         |
 *              |---------------|------------------------------------------------|
 *              | Left encoder  | Screen carousel — handled in ui.c, not here    |
 *              | Right encoder | Moves the slider focus (group not in edit mode)|
 *              | Left buttons  | "-" — lower the focused setting                |
 *              | Right buttons | "+" — raise the focused setting                |
 *
 *              The sliders are not draggable: the two button pads are the only
 *              way to move a value. They are sliders rather than bars because
 *              only a slider can take the keyboard focus, which is what makes
 *              the encoder selection visible.
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
 *              ### Applied is not yet saved
 *              Because the flash write is coalesced behind a delay, a change is
 *              live on the bus long before it survives a power cycle. The
 *              "Settings saved" notice marks the moment it does — it appears
 *              when the settings service reports SETTINGS_EVT_SAVED and this
 *              screen is the active one, see screen_settings_notify_saved().
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
 * @version     0.2.0
 *
 * @copyright   Copyright (c) 2026 Mario Wegmann.
 *              SPDX-License-Identifier: Apache-2.0
 */

/*
 * ─────────────────────────────────────────────────────────────────────────────────────────────────
 * Revision History
 * Version  Date        Author          Description
 * 0.1.0    2026-09-01  Mario Wegmann   Initial creation
 * 0.2.0    2026-09-19  Mario Wegmann   Sliders instead of button rows, debug
 *                                      bits moved to DBG CUSTOM, saved notice
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
 * Builds the header, one slider row per schema entry, the "-" / "+" buttons,
 * the hidden "saved" notice and the three input groups. Must be called after
 * ui_styles_init() and after settings_service_init(), since the rows are seeded
 * with settings_get().
 *
 * @param status_subjects  Device-status subjects for the header widget.
 * @return                 Pointer to the top-level screen object. Never NULL.
 */
lv_obj_t *screen_settings_create(lv_subject_t *status_subjects);

/**
 * @brief Show the "settings saved" notice for a moment.
 *
 * Called from the UI module when the settings service reports that the blob
 * reached the flash (SETTINGS_EVT_SAVED → UI_CMD_SETTINGS_SAVED), and only
 * while this screen is the active one. A no-op if the screen is not built, so
 * the caller does not have to check.
 *
 * Must run in the LVGL thread.
 */
void screen_settings_notify_saved(void);

/**
 * @brief Return the LVGL input group for the right encoder.
 *
 * Holds every setting slider and is deliberately *not* in edit mode, so a turn
 * of the encoder moves the focus from row to row. The sliders are inserted
 * bottom-up so the focus follows the direction the encoder is turned.
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
