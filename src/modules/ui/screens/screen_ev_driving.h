/**
 * @file        screen_ev_driving.h
 * @brief       EV driving screen factory
 *
 * @ingroup     dcu_ui_screens
 *
 * @details     The driver's screen while the car is moving under electric
 *              drive: live temperatures and accumulator voltage, plus the two
 *              settings that may be changed on the move.
 *
 *              ### Screen layout (480 × 320)
 *
 *              ```
 *              ┌──────────────────────────────────────┐
 *              │ EV DRIVING                    ▪▪▪▪▪▪ │ ← shared header
 *              ├──────────────────────────────────────┤
 *              │ ▓  HV Accu   Inverter   Motor      ▓ │
 *              │ ▓   42°C      38°C      52°C       ▓ │ ← left/right sliders
 *              │ ▓                                  ▓ │   TQG F / TQG R
 *              │ ▓  HV SoC                  488 V   ▓ │
 *              │ ▓  ▓▓▓▓▓▓▓▓▓▓▓░░░░░░░░░░░░░░░░░░   ▓ │
 *              │  ┌───────────┐      ┌───────────┐    │
 *              │  │ PWR Limit │      │  TQ Vect  │    │
 *              │  └───────────┘      └───────────┘    │
 *              └──────────────────────────────────────┘
 *              ```
 *
 *              ### Input assignment
 *
 *              | Input         | Drives                                      |
 *              |---------------|---------------------------------------------|
 *              | Left encoder  | The left torque-gain slider (TQG F)         |
 *              | Right encoder | The right torque-gain slider (TQG R)        |
 *              | Left buttons  | The PWR Limit button                        |
 *              | Right buttons | The TQ Vect button                          |
 *
 *              This screen is not in the carousel. It is loaded only when the
 *              state machine latches RTD, and the left encoder is claimed for
 *              TQG F, so there is no input left to navigate away with — the
 *              driver stays here until the car is powered down.
 *
 *              Both buttons are checkable and mirror a generated TX subject
 *              through an observer, so their visual state follows the value
 *              rather than the press.
 *
 *              @note The torque-gain sliders are display-only so far: their
 *              positions are remembered across visits but are not written to
 *              any setting or CAN signal.
 *
 * @author      Mario Wegmann <mario.wegmann@web.de>
 * @date        Created: 2026-06-15
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
 * 0.1.0    2026-06-15  Mario Wegmann   Initial creation
 * ─────────────────────────────────────────────────────────────────────────────────────────────────
 */

#ifndef MODULES_UI_SCREENS_SCREEN_EV_DRIVING_H
#define MODULES_UI_SCREENS_SCREEN_EV_DRIVING_H

/* ── LVGL Include ────────────────────────────────────────────────────────────────────────────── */

#include <lvgl.h>


/* ── Public Function Declarations ────────────────────────────────────────────────────────────── */

/**
 * @brief Create the EV driving screen.
 *
 * Builds the header, the three sliders, the temperature readouts, the two
 * setting buttons and the three input groups.
 * Must be called after ui_styles_init().
 *
 * @param status_subjects  Device-status subjects for the header widget.
 * @return                 Pointer to the top-level screen object. Never NULL.
 */
lv_obj_t *screen_ev_driving_create(lv_subject_t *status_subjects);

/**
 * @brief Return the LVGL input group for the right encoder.
 *
 * Contains the right torque-gain slider, in edit mode, so a turn changes its
 * value instead of moving focus.
 *
 * @return  The group. Valid only after screen_ev_driving_create().
 */
lv_group_t *screen_ev_driving_get_right_encoder_group(void);

/**
 * @brief Return the LVGL input group for the left encoder.
 *
 * Contains the left torque-gain slider, in edit mode. ui.c routes the left
 * encoder here for this screen; a non-NULL group is also what tells ui.c to
 * stop driving the carousel with that encoder.
 *
 * @return  The group. Valid only after screen_ev_driving_create().
 */
lv_group_t *screen_ev_driving_get_left_encoder_group(void);

/**
 * @brief Return the LVGL input group for the left button pad.
 *
 * Contains the PWR Limit button.
 *
 * @return  The group. Valid only after screen_ev_driving_create().
 */
lv_group_t *screen_ev_driving_get_left_button_group(void);

/**
 * @brief Return the LVGL input group for the right button pad.
 *
 * Contains the TQ Vect button.
 *
 * @return  The group. Valid only after screen_ev_driving_create().
 */
lv_group_t *screen_ev_driving_get_right_button_group(void);

#endif /* MODULES_UI_SCREENS_SCREEN_EV_DRIVING_H */