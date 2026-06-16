/**
 * @file        screen_ev_driving.h
 * @brief       EV driving screen factory
 *
 * @details     Provides a factory function and a group accessor for the ev driving screen.
 *
 *              Screen layout (480 × 320)
 *              ─────────────────────────
 *
 *                ┌──────────────────────────────────────┐
 *                │  MISSION                 ← gradient header (15 %)
 *                ├──────────────────────────────────────┤
 *                │                                      │
 *                │         ┌─────────────────┐          │
 *                │         │  Acceleration   │          │ ← Roller
 *                │         │▶ Skidpad       ◀│          │   right encoder
 *                │         │  Autocross      │          │
 *                │         └─────────────────┘          │
 *                │                                      │
 *                │    ┌───────────┐  ┌───────────┐      │
 *                │    │    OK     │  │    RTD    │      │
 *                │    └───────────┘  └───────────┘      │
 *                └──────────────────────────────────────┘
 *
 *              Encoder / button assignment
 *              ────────────────────────────
 *              Left  encoder  →  screen carousel (managed by ui.c, not this screen)
 *              Right encoder  →  assigned to this screen's LVGL group by ui.c
 *                                Tab order:  [Roller] → [OK] → [RTD]
 *
 *              Physical buttons on this screen:
 *                ESC  →  ui.c handles navigation back to boot screen
 *                OK   →  LV_KEY_ENTER → click focused widget (toggle roller
 *                         edit-mode or confirm OK / RTD button)
 *                RTD  →  publishes UI_INPUT_RTD_REQUEST directly (input module)
 *                TS   →  not used on this screen
 *
 *              Two distinct user actions
 *              ──────────────────────────
 *              OK  button widget  →  publishes UI_INPUT_MISSION_SELECTED
 *                                    carrying the roller's current selection.
 *                                    App Layer updates mission state and sends
 *                                    CAN_TX_CMD_SEND_MISSION.
 *
 *              RTD button widget  →  publishes UI_INPUT_RTD_REQUEST (no payload).
 *                                    App Layer sends CAN_TX_CMD_SEND_RTD_REQUEST
 *                                    with the last-known drive mode.
 *
 *              Rolling the roller does NOT trigger any CAN transmission; only
 *              pressing OK or RTD does.
 *
 * @author      Mario Wegmann <mario.wegmann@web.de>
 * @date        Created: 2026-06-15
 *
 * @version     0.1.0
 *
 * @copyright   Copyright (c) 2026 Mario Wegmann
 *              SPDX-License-Identifier: Apache-2.0
 *
 * @note        Target RTOS : Zephyr RTOS (https://zephyrproject.org)
 *              UI Library  : LVGL (https://lvgl.io)
 *
 * ─────────────────────────────────────────────────────────────────────────────────────────────────
 * Revision History
 * ─────────────────────────────────────────────────────────────────────────────────────────────────
 * Version  Date        Author          Description
 * 0.1.0    2026-06-15  Mario Wegmann   Initial creation
 * ─────────────────────────────────────────────────────────────────────────────────────────────────
 */

#pragma once

/* ── LVGL Include ────────────────────────────────────────────────────────────────────────────── */

#include <lvgl.h>


/* ── Public Function Declarations ────────────────────────────────────────────────────────────── */

/**
 * @brief Create the mission selection screen.
 *
 * Builds the header, roller, OK button, RTD button, and the LVGL input group.
 * Must be called after ui_styles_init().
 *
 * @return  Pointer to the top-level screen object.  Never NULL.
 */
lv_obj_t *screen_ev_driving_create(void);

/**
 * @brief Return the LVGL input group for the right encoder.
 *
 * Tab order: roller → OK button → RTD button.
 * ui.c assigns this group to the RIGHT encoder input device whenever this
 * screen becomes active, and removes it when navigating away:
 *
 * @code
 *   // on screen enter:
 *   lv_indev_set_group(right_encoder_indev, screen_ev_driving_get_group());
 *   // on screen leave:
 *   lv_indev_set_group(right_encoder_indev, NULL);
 * @endcode
 *
 * @return  Pointer to the lv_group_t.  Valid after screen_ev_driving_create().
 */
lv_group_t *screen_ev_driving_get_right_encoder_group(void);
lv_group_t *screen_ev_driving_get_left_button_group(void);
lv_group_t *screen_ev_driving_get_right_button_group(void);