/**
 * @file
 * @brief       Debug screen for the low-voltage accumulator
 *
 * @ingroup     dcu_ui_screens
 *
 * @details     A read-only debug screen: bars bound to generated LVGL subjects,
 *              no interactive widgets, no events published.
 *
 *              One value: the low-voltage accumulator voltage as a bar with its
 *              numeric readout. Range, unit and number of decimals are declared
 *              in dbc/dcu_app.yaml, not here.
 *
 *              ### Screen layout (480 × 320)
 *
 *              ```
 *              ┌──────────────────────────────────────┐
 *              │ DBG LV ACCU                   ▪▪▪▪▪▪ │ ← shared header
 *              ├──────────────────────────────────────┤
 *              │ LV Accu Voltage                      │
 *              │ ▓▓▓▓▓▓▓▓▓▓▓▓▓░░░░░░░  24.3 V         │
 *              └──────────────────────────────────────┘
 *              ```
 *
 *              Every bar row takes what it shows from its signal descriptor
 *              (ui_sig_*, generated from dbc/dcu_app.yaml): the caption, the
 *              unit, the decimals, the bar range and the limits. Nothing of
 *              that is written into this screen. A readout turns gold past its
 *              warning limit and red past its critical one, through
 *              ui_quantity_bind_signal().
 *              The LV voltage is a float subject, which LVGL's own
 *              lv_obj_bind_state_if_lt/gt() refuses — see ui_quantity.h.
 *
 *              Reachable through the screen carousel only.  All three group
 *              accessors return NULL, so the right encoder and both button
 *              pads are detached while this screen is shown.
 *
 * @author      Mario Wegmann <mario.wegmann@web.de>
 * @date        Created: 2026-06-25
 *
 * @copyright   Copyright (c) 2026 Mario Wegmann.
 *              SPDX-License-Identifier: Apache-2.0
 */

#ifndef MODULES_UI_SCREENS_SCREEN_DEBUG_LV_ACCU_H
#define MODULES_UI_SCREENS_SCREEN_DEBUG_LV_ACCU_H

/* ── LVGL Include ────────────────────────────────────────────────────────────────────────────── */

#include <lvgl.h>


/* ── Public Function Declarations ────────────────────────────────────────────────────────────── */

/**
 * @brief Create the low-voltage accumulator debug screen.
 *
 * Builds the header and the value bars, binding each to its generated subject.
 * Must be called after ui_styles_init().
 *
 * @param status_subjects  Device-status subjects for the header widget.
 * @return                 Pointer to the top-level screen object. Never NULL.
 */
lv_obj_t *screen_debug_lv_accu_create(lv_subject_t *status_subjects);

/*
 * The screen has no interactive widgets, so all three accessors return NULL
 * and ui.c detaches the corresponding input device while it is shown. They
 * exist because ui.c calls the same three accessors for every screen.
 */

/** @brief Return the input group for the right encoder. @return Always NULL. */
lv_group_t *screen_debug_lv_accu_get_right_encoder_group(void);

/** @brief Return the input group for the left button pad. @return Always NULL. */
lv_group_t *screen_debug_lv_accu_get_left_button_group(void);

/** @brief Return the input group for the right button pad. @return Always NULL. */
lv_group_t *screen_debug_lv_accu_get_right_button_group(void);

#endif /* MODULES_UI_SCREENS_SCREEN_DEBUG_LV_ACCU_H */