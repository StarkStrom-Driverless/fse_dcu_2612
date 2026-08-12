/**
 * @file        screen_debug_pressure.c
 * @brief       Debug screen for air and brake pressure
 *
 * @ingroup     dcu_ui_screens
 *
 * @details     Implementation; the contract is in screen_debug_pressure.h.
 *
 *              Builds four bar-plus-readout pairs: brake pressure front and
 *              rear, air pressure front and rear. Each is bound to its
 *              generated subject; the readouts print two decimals because the
 *              signals carry scaled values.
 *
 * @author      Mario Wegmann <mario.wegmann@web.de>
 * @date        Created: 2026-06-24
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
 * 0.1.0    2026-06-24  Mario Wegmann   Initial creation
 * ─────────────────────────────────────────────────────────────────────────────────────────────────
 */

/* ── Corresponding Header ────────────────────────────────────────────────────────────────────── */

#include "modules/ui/screens/screen_debug_pressure.h"

/* ── Zephyr Includes ─────────────────────────────────────────────────────────────────────────── */

#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/logging/log.h>

/* ── Project Includes ────────────────────────────────────────────────────────────────────────── */

#include "app/app_state.h"
#include "modules/ui/ui_styles.h"
#include "modules/ui/widgets/ui_header.h"
#include "modules/ui/widgets/ui_unit_label.h"
#include "services/event_bus/event_bus.h"
#include "services/event_bus/events.h"
#include "generated/ui_subjects_gen.h"

/* ── Zephyr Logging ──────────────────────────────────────────────────────────────────────────── */

LOG_MODULE_REGISTER(screen_debug_pressure, CONFIG_LOG_DEFAULT_LEVEL);


/* ── Private Macros & Constants ──────────────────────────────────────────────────────────────── */

/* ── Private Variables ───────────────────────────────────────────────────────────────────────── */


/* ── Private Function Prototypes ─────────────────────────────────────────────────────────────── */
static void build_bars(lv_obj_t *scr);

/* ── Private Function Implementations ───────────────────────────────────────────────────────── */

/**
 * @brief Build every bar and its numeric readout.
 *
 * One block per value, each following the same shape: create the bar,
 * strip the LVGL defaults, apply the shared slider styles, set the range,
 * bind it to its subject, then add the caption above and the readout to
 * its right.
 *
 * @param scr  Screen object to build into.
 */
static void build_bars(lv_obj_t *scr)
{
    /* ── Brake Pressure Front ───────────────────────────────────────────── */

    lv_obj_t * bar_bp_front = lv_bar_create(scr);
    lv_obj_remove_style_all(bar_bp_front);
    lv_obj_add_style(bar_bp_front, &ui_style_slider_main, LV_PART_MAIN);
    lv_obj_add_style(bar_bp_front, &ui_style_slider_indicator, LV_PART_INDICATOR);
    lv_bar_set_range(bar_bp_front, 0, 100);
    lv_bar_bind_value(bar_bp_front, &ui_subj_brake_pressure_front);
    lv_obj_set_size(bar_bp_front, 300, 20);
    lv_obj_align(bar_bp_front, LV_ALIGN_TOP_MID, -70, 80);

    lv_obj_t *lbl_bar_bp_front_title = lv_label_create(scr);
    lv_obj_add_style(lbl_bar_bp_front_title, &ui_style_label_subtitle, 0);
    lv_label_set_text(lbl_bar_bp_front_title, "Break Pressure Front");
    lv_obj_align_to(lbl_bar_bp_front_title, bar_bp_front, LV_ALIGN_OUT_TOP_LEFT, 0, 0);

    lv_obj_t *lbl_bar_bp_front_value = ui_unit_label_create(scr,
                                      &BarlowCondensed_BoldItalic_32,
                                      &BarlowCondensed_Italic_20, "Bar");
    lv_obj_add_style(lbl_bar_bp_front_value, &ui_style_level_warn, UI_STATE_WARN);
    lv_obj_add_style(lbl_bar_bp_front_value, &ui_style_level_crit, UI_STATE_CRIT);
    lv_obj_bind_state_if_lt(lbl_bar_bp_front_value, &ui_subj_brake_pressure_front, UI_STATE_WARN, UI_VOLTAGE_ACCU_HV_WARN_LOW);
    lv_obj_bind_state_if_lt(lbl_bar_bp_front_value, &ui_subj_brake_pressure_front, UI_STATE_CRIT, UI_VOLTAGE_ACCU_HV_CRIT_LOW);
    // lv_obj_set_size(lbl_bar_bp_front_value, 60, 30);
    ui_unit_label_bind_value(lbl_bar_bp_front_value, &ui_subj_brake_pressure_front, "%.2f");
    lv_obj_align_to(lbl_bar_bp_front_value, bar_bp_front, LV_ALIGN_OUT_RIGHT_MID, 20, 0);

    /* ── Brake Pressure Rear ────────────────────────────────────────────── */

    lv_obj_t * bar_bp_rear = lv_bar_create(scr);
    lv_obj_remove_style_all(bar_bp_rear);
    lv_obj_add_style(bar_bp_rear, &ui_style_slider_main, LV_PART_MAIN);
    lv_obj_add_style(bar_bp_rear, &ui_style_slider_indicator, LV_PART_INDICATOR);
    lv_bar_set_range(bar_bp_rear, 0, 100);
    lv_bar_bind_value(bar_bp_rear, &ui_subj_brake_pressure_rear);
    lv_obj_set_size(bar_bp_rear, 300, 20);
    lv_obj_align(bar_bp_rear, LV_ALIGN_TOP_MID, -70, 130);

    lv_obj_t *lbl_bar_bp_rear_title = lv_label_create(scr);
    lv_obj_add_style(lbl_bar_bp_rear_title, &ui_style_label_subtitle, 0);
    lv_label_set_text(lbl_bar_bp_rear_title, "Break Pressure Rear");
    lv_obj_align_to(lbl_bar_bp_rear_title, bar_bp_rear, LV_ALIGN_OUT_TOP_LEFT, 00, 0);

    lv_obj_t *lbl_bar_bp_rear_value = ui_unit_label_create(scr,
                                      &BarlowCondensed_BoldItalic_32,
                                      &BarlowCondensed_Italic_20, "Bar");
    lv_obj_add_style(lbl_bar_bp_rear_value, &ui_style_level_warn, UI_STATE_WARN);
    lv_obj_add_style(lbl_bar_bp_rear_value, &ui_style_level_crit, UI_STATE_CRIT);
    lv_obj_bind_state_if_lt(lbl_bar_bp_rear_value, &ui_subj_brake_pressure_rear, UI_STATE_WARN, UI_VOLTAGE_ACCU_HV_WARN_LOW);
    lv_obj_bind_state_if_lt(lbl_bar_bp_rear_value, &ui_subj_brake_pressure_rear, UI_STATE_CRIT, UI_VOLTAGE_ACCU_HV_CRIT_LOW);
    // lv_obj_set_size(lbl_bar_bp_rear_value, 60, 30);
    ui_unit_label_bind_value(lbl_bar_bp_rear_value, &ui_subj_brake_pressure_rear, "%.2f");
    lv_obj_align_to(lbl_bar_bp_rear_value, bar_bp_rear, LV_ALIGN_OUT_RIGHT_MID, 20, 0);

    /* ── Air Pressure Front ───────────────────────────────────────────── */

    lv_obj_t * bar_air_front = lv_bar_create(scr);
    lv_obj_remove_style_all(bar_air_front);
    lv_obj_add_style(bar_air_front, &ui_style_slider_main, LV_PART_MAIN);
    lv_obj_add_style(bar_air_front, &ui_style_slider_indicator, LV_PART_INDICATOR);
    lv_bar_set_range(bar_air_front, 0, 100);
    lv_bar_bind_value(bar_air_front, &ui_subj_air_pressure_front);
    lv_obj_set_size(bar_air_front, 300, 20);
    lv_obj_align(bar_air_front, LV_ALIGN_TOP_MID, -70, 180);

    lv_obj_t *lbl_bar_air_front_title = lv_label_create(scr);
    lv_obj_add_style(lbl_bar_air_front_title, &ui_style_label_subtitle, 0);
    lv_label_set_text(lbl_bar_air_front_title, "Air Pressure Front");
    lv_obj_align_to(lbl_bar_air_front_title, bar_air_front, LV_ALIGN_OUT_TOP_LEFT, 0, 0);

    lv_obj_t *lbl_bar_air_front_value = ui_unit_label_create(scr,
                                      &BarlowCondensed_BoldItalic_32,
                                      &BarlowCondensed_Italic_20, "Bar");
    lv_obj_add_style(lbl_bar_air_front_value, &ui_style_level_warn, UI_STATE_WARN);
    lv_obj_add_style(lbl_bar_air_front_value, &ui_style_level_crit, UI_STATE_CRIT);
    lv_obj_bind_state_if_lt(lbl_bar_air_front_value, &ui_subj_air_pressure_front, UI_STATE_WARN, UI_VOLTAGE_ACCU_HV_WARN_LOW);
    lv_obj_bind_state_if_lt(lbl_bar_air_front_value, &ui_subj_air_pressure_front, UI_STATE_CRIT, UI_VOLTAGE_ACCU_HV_CRIT_LOW);
    // lv_obj_set_size(lbl_bar_air_front_value, 60, 30);
    ui_unit_label_bind_value(lbl_bar_air_front_value, &ui_subj_air_pressure_front, "%.2f");
    lv_obj_align_to(lbl_bar_air_front_value, bar_air_front, LV_ALIGN_OUT_RIGHT_MID, 20, 0);

    /* ── Air Pressure Rear ────────────────────────────────────────────── */

    lv_obj_t * bar_air_rear = lv_bar_create(scr);
    lv_obj_remove_style_all(bar_air_rear);
    lv_obj_add_style(bar_air_rear, &ui_style_slider_main, LV_PART_MAIN);
    lv_obj_add_style(bar_air_rear, &ui_style_slider_indicator, LV_PART_INDICATOR);
    lv_bar_set_range(bar_air_rear, 0, 100);
    lv_bar_bind_value(bar_air_rear, &ui_subj_air_pressure_rear);
    lv_obj_set_size(bar_air_rear, 300, 20);
    lv_obj_align(bar_air_rear, LV_ALIGN_TOP_MID, -70, 230);

    lv_obj_t *lbl_bar_air_rear_title = lv_label_create(scr);
    lv_obj_add_style(lbl_bar_air_rear_title, &ui_style_label_subtitle, 0);
    lv_label_set_text(lbl_bar_air_rear_title, "Air Pressure Rear");
    lv_obj_align_to(lbl_bar_air_rear_title, bar_air_rear, LV_ALIGN_OUT_TOP_LEFT, 00, 0);

    lv_obj_t *lbl_bar_air_rear_value = ui_unit_label_create(scr,
                                      &BarlowCondensed_BoldItalic_32,
                                      &BarlowCondensed_Italic_20, "Bar");
    lv_obj_add_style(lbl_bar_air_rear_value, &ui_style_level_warn, UI_STATE_WARN);
    lv_obj_add_style(lbl_bar_air_rear_value, &ui_style_level_crit, UI_STATE_CRIT);
    lv_obj_bind_state_if_lt(lbl_bar_air_rear_value, &ui_subj_air_pressure_rear, UI_STATE_WARN, UI_VOLTAGE_ACCU_HV_WARN_LOW);
    lv_obj_bind_state_if_lt(lbl_bar_air_rear_value, &ui_subj_air_pressure_rear, UI_STATE_CRIT, UI_VOLTAGE_ACCU_HV_CRIT_LOW);
    // lv_obj_set_size(lbl_bar_air_rear_value, 60, 30);
    ui_unit_label_bind_value(lbl_bar_air_rear_value, &ui_subj_air_pressure_rear, "%.2f");
    lv_obj_align_to(lbl_bar_air_rear_value, bar_air_rear, LV_ALIGN_OUT_RIGHT_MID, 20, 0);
}

/* ── Public Function Implementations ─────────────────────────────────────────────────────────── */

lv_obj_t *screen_debug_pressure_create(lv_subject_t *status_subjects)
{
    /* ── Screen base ─────────────────────────────────────────────────────── */

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_add_style(scr, &ui_style_screen, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* ── Widgets ─────────────────────────────────────────────────────────── */

    ui_header_create(scr, "DBG PRESSURE", status_subjects);
    build_bars(scr);

    return scr;
}

lv_group_t *screen_debug_pressure_get_right_encoder_group(void)
{
    return NULL;
}

lv_group_t *screen_debug_pressure_get_left_button_group(void)
{
    return NULL;
}

lv_group_t *screen_debug_pressure_get_right_button_group(void)
{
    return NULL;
}

