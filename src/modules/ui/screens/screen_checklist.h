/**
 * @file        screen_checklist.h
 * @brief       EV checklist screen factory (SCREEN_PRE_RTD) — readouts and the Ready-to-Drive button
 *
 * @ingroup     dcu_ui_screens
 *
 * @details     Registered for SCREEN_PRE_RTD; the header reads "EV CHECKLIST".
 *              The screen carries the RTD button and six live readouts of the
 *              values a driver looks at before requesting Ready-to-Drive.
 *
 *              This is the only screen from which Ready-to-Drive can be
 *              requested.
 *
 *              The name still promises more than there is: the readouts are
 *              displayed, not evaluated. Nothing on this screen checks them
 *              against a precondition or holds the RTD button back — whether
 *              the vehicle may enter R2D is decided by the mABX.
 *
 *              ### Screen layout (480 × 320)
 *              ```
 *              ┌──────────────────────────────────────┐
 *              │ EV CHECKLIST                  ▪▪▪▪▪▪ │ ← shared header
 *              ├──────────────────────────────────────┤
 *              │ Brake Front       Brake Rear         │
 *              │ ▓▓▓▓░░░  8.2 Bar  ▓▓▓░░░░  7.9 Bar   │
 *              │ Air Front         Air Rear           │
 *              │ ▓▓▓▓▓░░  8.4 Bar  ▓▓▓▓▓░░  8.2 Bar   │
 *              │ HV Accu           LV Accu            │
 *              │ ▓▓▓▓▓▓▓  496 V    ▓▓▓▓▓░░  24.3 V    │
 *              │                                      │
 *              │                   ┌───────────┐      │
 *              │                   │ SEND RTD  │      │ ← dedicated RTD pad
 *              │                   └───────────┘      │
 *              └──────────────────────────────────────┘
 *              ```
 *              Captions are abbreviated here; the screen spells them out.
 *
 *              ### Readouts
 *              Two columns of three, each a bar with its value to the right.
 *              They are bound to the generated RX subjects, so they follow the
 *              bus without any code of their own here.
 *
 *              | Column | Row | Descriptor                   |
 *              |--------|-----|------------------------------|
 *              | Left   | 1   | ui_sig_brake_pressure_front  |
 *              | Left   | 2   | ui_sig_air_pressure_front    |
 *              | Left   | 3   | ui_sig_voltage_accu_hv       |
 *              | Right  | 1   | ui_sig_brake_pressure_rear   |
 *              | Right  | 2   | ui_sig_air_pressure_rear     |
 *              | Right  | 3   | ui_sig_lv_accu_voltage       |
 *
 *              Each row takes its caption, unit, decimals, bar range and limits
 *              from the signal descriptor (ui_sig_*, generated from
 *              dbc/dcu_app.yaml); the screen writes none of them. The value is
 *              colored through ui_quantity_bind_signal(): gold past the warning
 *              limit, red past the critical one. Air pressure and the two
 *              voltages have limits on both sides.
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
 *              ### Hold to request RTD
 *              Nothing latches. The screen reports press and release of the
 *              dedicated RTD button (UI_INPUT_RTD_PRESSED / _RELEASED), and the
 *              CAN module sends RTD_Button = 1 on its 100 ms frames only while
 *              the button is held and has been held for APP_RTD_HOLD_MS
 *              (500 ms). The button shows where that stands:
 *
 *              | Color | Meaning                                          |
 *              |--------|--------------------------------------------------|
 *              | White  | Not pressed                                      |
 *              | Gold   | Held, but RTD_Button = 1 is not on the bus yet   |
 *              | Green  | Held, and a frame with RTD_Button = 1 was sent   |
 *
 *              The driver keeps holding until green. When the MABX then reports
 *              RTD_State = 1, the state machine switches to EV DRIVING, which
 *              replaces this screen and thereby releases the request.
 *
 *              The button works on this screen only: ui.c binds the RTD pad to
 *              no group anywhere else, and a press that started on another
 *              screen is not picked up here — LVGL sees no new press.
 *
 * @author      Mario Wegmann <mario.wegmann@web.de>
 * @date        Created: 2026-06-08
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
 * 0.1.0    2026-06-08  Mario Wegmann   Initial creation
 * 0.2.0    2026-09-20  Mario Wegmann   Pressure and voltage readouts added; header
 *                                      retitled "EV CHECKLIST"
 * ─────────────────────────────────────────────────────────────────────────────────────────────────
 */

#ifndef MODULES_UI_SCREENS_SCREEN_CHECKLIST_H
#define MODULES_UI_SCREENS_SCREEN_CHECKLIST_H

/* ── LVGL Include ────────────────────────────────────────────────────────────────────────────── */

#include <lvgl.h>


/* ── Public Function Declarations ────────────────────────────────────────────────────────────── */

/**
 * @brief Create the EV checklist screen (SCREEN_PRE_RTD).
 *
 * Builds the header, the six readouts, the RTD button and the input groups.
 * Must be called after ui_styles_init() and ui_subjects_gen_init(), since the
 * readouts bind to the generated subjects while they are being built.
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

/**
 * @brief Show whether RTD_Button = 1 is currently on the bus.
 *
 * Called by ui.c on UI_CMD_RTD_TX_STATE while this screen is active. Turns the
 * button green only while it is also held; a report that arrives after the
 * release leaves it white.
 *
 * @param on_bus  True if the last transmitted frame carried RTD_Button = 1.
 */
void screen_checklist_set_rtd_tx(bool on_bus);

#endif /* MODULES_UI_SCREENS_SCREEN_CHECKLIST_H */