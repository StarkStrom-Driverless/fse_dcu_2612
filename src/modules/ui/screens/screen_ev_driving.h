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
 *              │TQG F  HV Accu  Inverter   Motor TQG R│
 *              │ ▓      38°C      52°C      61°C    ▓ │ ← left/right sliders
 *              │ ▓                                  ▓ │   TQG F / TQG R
 *              │ ▓  HV Accu Voltage          496 V  ▓ │
 *              │ ▓  ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓░░░░░░░░░░   ▓ │
 *              │ ▓    ┌───────────┐ ┌───────────┐   ▓ │
 *              │ ▓    │ PWR Limit │ │ TQ Vect   │   ▓ │
 *              │ ▓    │ OFF       │ │ ON        │   ▓ │ ← green while ON
 *              │      └───────────┘ └───────────┘     │
 *              └──────────────────────────────────────┘
 *              ```
 *
 *              Three columns in the content area (ui_layout.h): the two sliders
 *              take what their width needs, the middle column takes the rest.
 *              Its three blocks — the temperatures, the HV bar and the buttons —
 *              are spread over its height. No widget has a coordinate.
 *
 *              ### Input assignment
 *
 *              | Input         | Drives                                      |
 *              |---------------|---------------------------------------------|
 *              | Left encoder  | The left torque-gain slider (TQG F)         |
 *              | Right encoder | The right torque-gain slider (TQG R)        |
 *              | Left buttons  | PWR Limit on/off                            |
 *              | Right buttons | TQ Vect on/off                              |
 *
 *              This screen is not in the carousel. It is loaded only when the
 *              MABX reports RTD_State = 1, and the left encoder is claimed for
 *              TQG F, so there is no input left to navigate away with — the
 *              driver stays here until the car is powered down.
 *
 *              Both buttons are the same widget with a different caption: a
 *              checkable button showing its title and ON/OFF, green while on,
 *              mirroring a generated TX subject through an observer — so the
 *              visual state follows the stored value rather than the press.
 *
 *              Neither setting is a boolean in the schema (power limit 0…7,
 *              torque vectoring 0…3). Here they are reduced to on/off: any
 *              non-zero value shows ON, switching on stores 1, switching off
 *              stores 0. A level chosen on DV SETTINGS is flattened the first
 *              time the button is pressed.
 *
 *              @note The torque-gain sliders are display-only so far: their
 *              positions are remembered across visits but are not written to
 *              any setting or CAN signal.
 *
 * @author      Mario Wegmann <mario.wegmann@web.de>
 * @date        Created: 2026-06-15
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
 * 0.1.0    2026-06-15  Mario Wegmann   Initial creation
 * 0.2.0    2026-09-20  Mario Wegmann   Layout without coordinates: columns, rows and
 *                                      layout units instead of pixel positions
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
 * Builds the header, the two torque-gain sliders, the temperature readouts,
 * the HV bar, the two setting buttons and the four input groups.
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