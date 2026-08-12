/**
 * @file        screen_debug_hv_accu.h
 * @brief       Debug screen for the high-voltage accumulator
 *
 * @ingroup     dcu_ui_screens
 *
 * @details     A read-only debug screen: bars bound to generated LVGL subjects,
 *              no interactive widgets, no events published.
 *
 *              Two values, each as a bar with its numeric readout:
 *              accumulator voltage (0…500 V) and accumulator temperature
 *              (0…100 °C).
 *
 *              ### Screen layout (480 × 320)
 *
 *              ```
 *              ┌──────────────────────────────────────┐
 *              │ DBG HV ACCU                   ▪▪▪▪▪▪ │ ← shared header
 *              ├──────────────────────────────────────┤
 *              │ HV Accu Voltage                      │
 *              │ ▓▓▓▓▓▓▓▓▓░░░░░░░░░░░░  488 V         │
 *              │ HV Accu Temperature                  │
 *              │ ▓▓▓▓▓░░░░░░░░░░░░░░░░   42 °C        │
 *              └──────────────────────────────────────┘
 *              ```
 *
 *              Each numeric readout turns gold past its warning threshold and
 *              red past its critical one, driven by lv_obj_bind_state_if_lt()
 *              against the limits generated from dbc/dcu_app.yaml.
 *
 *              Reachable through the screen carousel only.  All three group
 *              accessors return NULL, so the right encoder and both button
 *              pads are detached while this screen is shown.
 *
 * @author      Mario Wegmann <mario.wegmann@web.de>
 * @date        Created: 2026-06-25
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
 * 0.1.0    2026-06-25  Mario Wegmann   Initial creation
 * ─────────────────────────────────────────────────────────────────────────────────────────────────
 */

#ifndef MODULES_UI_SCREENS_SCREEN_DEBUG_HV_ACCU_H
#define MODULES_UI_SCREENS_SCREEN_DEBUG_HV_ACCU_H

/* ── LVGL Include ────────────────────────────────────────────────────────────────────────────── */

#include <lvgl.h>


/* ── Public Function Declarations ────────────────────────────────────────────────────────────── */

/**
 * @brief Create the high-voltage accumulator debug screen.
 *
 * Builds the header and the value bars, binding each to its generated subject.
 * Must be called after ui_styles_init().
 *
 * @param status_subjects  Device-status subjects for the header widget.
 * @return                 Pointer to the top-level screen object. Never NULL.
 */
lv_obj_t *screen_debug_hv_accu_create(lv_subject_t *status_subjects);

/*
 * The screen has no interactive widgets, so all three accessors return NULL
 * and ui.c detaches the corresponding input device while it is shown. They
 * exist because ui.c calls the same three accessors for every screen.
 */

/** @brief Return the input group for the right encoder. @return Always NULL. */
lv_group_t *screen_debug_hv_accu_get_right_encoder_group(void);

/** @brief Return the input group for the left button pad. @return Always NULL. */
lv_group_t *screen_debug_hv_accu_get_left_button_group(void);

/** @brief Return the input group for the right button pad. @return Always NULL. */
lv_group_t *screen_debug_hv_accu_get_right_button_group(void);

#endif /* MODULES_UI_SCREENS_SCREEN_DEBUG_HV_ACCU_H */