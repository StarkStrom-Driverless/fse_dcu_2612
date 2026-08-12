/**
 * @file        screen_sdc.h
 * @brief       Shutdown-circuit screen factory
 *
 * @ingroup     dcu_ui_screens
 *
 * @details     Shows the state of all twelve shutdown-circuit nodes twice, in
 *              two views of the same data:
 *
 *                – a top-down drawing of the car with an LED at each node's
 *                  physical position, for locating an open node on the vehicle
 *                – a two-column name table, for reading it off by name
 *
 *              ### Screen layout (480 × 320)
 *
 *              ```
 *              ┌──────────────────────────────────────┐
 *              │ SDC                           ▪▪▪▪▪▪ │ ← shared header
 *              ├──────────────────────────────────────┤
 *              │      ╭────────────╮   ASCU    HVD    │
 *              │      │  ●      ●  │   MOT RL  RES    │
 *              │      │   car with │   MOT FL  BOTS   │
 *              │      │   node LEDs│   MOT FR  COCKPIT│
 *              │      │  ●      ●  │   INERTIA MH     │
 *              │      ╰────────────╯   MOT RR  BSPD   │
 *              └──────────────────────────────────────┘
 *              ```
 *
 *              Green LED / dark text  →  node closed (healthy)
 *              Red   LED / red text   →  node open   (circuit interrupted)
 *
 *              The screen is display-only: it has no interactive widgets, and
 *              all three group accessors return NULL.
 *
 * @author      Mario Wegmann <mario.wegmann@web.de>
 * @date        Created: 2026-08-05
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
 * 0.1.0    2026-08-05  Mario Wegmann   Initial creation
 * ─────────────────────────────────────────────────────────────────────────────────────────────────
 */

#ifndef MODULES_UI_SCREENS_SCREEN_SDC_H
#define MODULES_UI_SCREENS_SCREEN_SDC_H

/* ── LVGL Include ────────────────────────────────────────────────────────────────────────────── */

#include <lvgl.h>


/* ── Public Function Declarations ────────────────────────────────────────────────────────────── */

/**
 * @brief Create the shutdown-circuit screen.
 *
 * Builds the header, the top-down car image with one overlay LED per node, and
 * the node name table. Each element observes the generated ui_subj_sdc_*
 * subjects, so the screen needs no update calls afterwards.
 *
 * Must be called after ui_styles_init().
 *
 * @param status_subjects  Device-status subjects for the header widget.
 * @return                 Pointer to the top-level screen object. Never NULL.
 */
lv_obj_t *screen_sdc_create(lv_subject_t *status_subjects);

/*
 * The screen has no interactive widgets, so all three accessors return NULL
 * and ui.c detaches the corresponding input device while it is shown. They
 * exist because ui.c calls the same three accessors for every screen it routes
 * input to.
 */

/** @brief Return the input group for the right encoder. @return Always NULL. */
lv_group_t *screen_sdc_get_right_encoder_group(void);

/** @brief Return the input group for the left button pad. @return Always NULL. */
lv_group_t *screen_sdc_get_left_button_group(void);

/** @brief Return the input group for the right button pad. @return Always NULL. */
lv_group_t *screen_sdc_get_right_button_group(void);

#endif /* MODULES_UI_SCREENS_SCREEN_SDC_H */