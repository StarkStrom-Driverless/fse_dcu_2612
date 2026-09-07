/**
 * @file        screen_dv_driving.h
 * @brief       Driverless driving screen factory
 *
 * @ingroup     dcu_ui_screens
 *
 * @details     The driver's screen while an autonomous mission is running. For
 *              now it shows one thing: the DV mission that was confirmed on the
 *              mission-select screen.
 *
 *              ### Screen layout (480 × 320)
 *
 *              ```
 *              ┌──────────────────────────────────────┐
 *              │ DV DRIVING                    ▪▪▪▪▪▪ │ ← shared header
 *              ├──────────────────────────────────────┤
 *              │                                      │
 *              │               MISSION                │
 *              │             Trackdrive               │ ← from ui_tx_subj_drive_mode
 *              │                                      │
 *              └──────────────────────────────────────┘
 *              ```
 *
 *              ### How it is reached and left
 *              Loaded by the state machine when @c AS_state reaches AS driving.
 *              It is not on the carousel and nothing navigates away from it, so
 *              once shown it stays until the DCU is powered off — the same
 *              contract as the EV driving screen.
 *
 *              ### Input assignment
 *              None. The screen is display-only; all three group accessors
 *              return NULL and the left encoder does nothing here (ui.c does
 *              not carousel-navigate off a non-carousel screen).
 *
 * @author      Mario Wegmann <mario.wegmann@web.de>
 * @date        Created: 2026-09-07
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
 * 0.1.0    2026-09-07  Mario Wegmann   Initial creation
 * ─────────────────────────────────────────────────────────────────────────────────────────────────
 */

#ifndef MODULES_UI_SCREENS_SCREEN_DV_DRIVING_H
#define MODULES_UI_SCREENS_SCREEN_DV_DRIVING_H

/* ── LVGL Include ────────────────────────────────────────────────────────────────────────────── */

#include <lvgl.h>


/* ── Public Function Declarations ────────────────────────────────────────────────────────────── */

/**
 * @brief Create the driverless driving screen.
 *
 * Builds the header and the mission readout. Must be called after
 * ui_styles_init() and ui_tx_subjects_gen_init().
 *
 * @param status_subjects  Device-status subjects for the header widget.
 * @return                 Pointer to the top-level screen object. Never NULL.
 */
lv_obj_t *screen_dv_driving_create(lv_subject_t *status_subjects);

/** @brief Right-encoder group. Always NULL — this screen takes no input. */
lv_group_t *screen_dv_driving_get_right_encoder_group(void);

/** @brief Left-button-pad group. Always NULL — this screen takes no input. */
lv_group_t *screen_dv_driving_get_left_button_group(void);

/** @brief Right-button-pad group. Always NULL — this screen takes no input. */
lv_group_t *screen_dv_driving_get_right_button_group(void);

#endif /* MODULES_UI_SCREENS_SCREEN_DV_DRIVING_H */
