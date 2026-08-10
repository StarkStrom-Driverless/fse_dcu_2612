/**
 * @file        screen_debug_pressure.h
 * @brief       Debug screen for air and break pressure
 *
 * @details     
 *
 * @author      Mario Wegmann <mario.wegmann@web.de>
 * @date        Created: 2026-06-24
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
 * 0.1.0    2026-06-24  Mario Wegmann   Initial creation
 * ─────────────────────────────────────────────────────────────────────────────────────────────────
 */

#ifndef MODULES_UI_SCREENS_SCREEN_DEBUG_PRESSURE_H
#define MODULES_UI_SCREENS_SCREEN_DEBUG_PRESSURE_H

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
lv_obj_t *screen_debug_pressure_create(void);

/**
 * @brief Return the LVGL input group for the right encoder.
 *
 * Tab order: roller → OK button → RTD button.
 * ui.c assigns this group to the RIGHT encoder input device whenever this
 * screen becomes active, and removes it when navigating away:
 *
 * @code
 *   // on screen enter:
 *   lv_indev_set_group(right_encoder_indev, screen_debug_pressure_get_group());
 *   // on screen leave:
 *   lv_indev_set_group(right_encoder_indev, NULL);
 * @endcode
 *
 * @return  Pointer to the lv_group_t.  Valid after screen_debug_pressure_create().
 */
lv_group_t *screen_debug_pressure_get_right_encoder_group(void);
lv_group_t *screen_debug_pressure_get_left_button_group(void);
lv_group_t *screen_debug_pressure_get_right_button_group(void);

#endif /* MODULES_UI_SCREENS_SCREEN_DEBUG_PRESSURE_H */