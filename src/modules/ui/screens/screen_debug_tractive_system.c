/**
 * @file        screen_debug_tractive_system.c
 * @brief       Debug screen for the tractive system
 *
 * @ingroup     dcu_ui_screens
 *
 * @details     Implementation; the contract is in screen_debug_tractive_system.h.
 *
 *              Builds six bar-plus-readout pairs in two columns: tractive-system
 *              voltage, highest motor temperature and inverter temperature on
 *              the left; both accelerator pedal position sensors and the brake
 *              pedal position on the right. Each is bound to its generated
 *              subject, so the screen keeps no state.
 *
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

#include "modules/ui/screens/screen_debug_tractive_system.h"

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

LOG_MODULE_REGISTER(screen_debug_tractive_system, CONFIG_LOG_DEFAULT_LEVEL);


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
    /* ── Tractive System Voltage ────────────────────────────────────────── */

    lv_obj_t * bar_ts_voltage = lv_bar_create(scr);
    lv_obj_remove_style_all(bar_ts_voltage);
    lv_obj_add_style(bar_ts_voltage, &ui_style_slider_main, LV_PART_MAIN);
    lv_obj_add_style(bar_ts_voltage, &ui_style_slider_indicator, LV_PART_INDICATOR);
    lv_bar_set_range(bar_ts_voltage, 0, 500);
    lv_bar_bind_value(bar_ts_voltage, &ui_subj_voltage_tractive_system);
    lv_obj_set_size(bar_ts_voltage, 140, 20);
    lv_obj_align(bar_ts_voltage, LV_ALIGN_TOP_MID, -150, 80);

    lv_obj_t *lbl_bar_ts_voltage_title = lv_label_create(scr);
    lv_obj_add_style(lbl_bar_ts_voltage_title, &ui_style_label_subtitle, 0);
    lv_label_set_text(lbl_bar_ts_voltage_title, "Tractive System Voltage");
    lv_obj_align_to(lbl_bar_ts_voltage_title, bar_ts_voltage, LV_ALIGN_OUT_TOP_LEFT, 0, 0);

    lv_obj_t *lbl_bar_ts_voltage_value = ui_unit_label_create(scr,
                                      &BarlowCondensed_BoldItalic_32,
                                      &BarlowCondensed_Italic_20, "V");
    lv_obj_add_style(lbl_bar_ts_voltage_value, &ui_style_level_warn, UI_STATE_WARN);
    lv_obj_add_style(lbl_bar_ts_voltage_value, &ui_style_level_crit, UI_STATE_CRIT);
    lv_obj_bind_state_if_lt(lbl_bar_ts_voltage_value, &ui_subj_voltage_tractive_system, UI_STATE_WARN, UI_VOLTAGE_ACCU_HV_WARN_LOW);
    lv_obj_bind_state_if_lt(lbl_bar_ts_voltage_value, &ui_subj_voltage_tractive_system, UI_STATE_CRIT, UI_VOLTAGE_ACCU_HV_CRIT_LOW);
    // lv_obj_set_size(lbl_bar_ts_voltage_value, 60, 30);
    ui_unit_label_bind_value(lbl_bar_ts_voltage_value, &ui_subj_voltage_tractive_system, "%d");
    lv_obj_align_to(lbl_bar_ts_voltage_value, bar_ts_voltage, LV_ALIGN_OUT_RIGHT_MID, 20, 0);

    /* ── MAX Motor Temperature ──────────────────────────────────────────── */

    lv_obj_t * bar_temp_motor_max = lv_bar_create(scr);
    lv_obj_remove_style_all(bar_temp_motor_max);
    lv_obj_add_style(bar_temp_motor_max, &ui_style_slider_main, LV_PART_MAIN);
    lv_obj_add_style(bar_temp_motor_max, &ui_style_slider_indicator, LV_PART_INDICATOR);
    lv_bar_set_range(bar_temp_motor_max, 0, 80);
    lv_bar_bind_value(bar_temp_motor_max, &ui_subj_temperature_motor);
    lv_obj_set_size(bar_temp_motor_max, 140, 20);
    lv_obj_align(bar_temp_motor_max, LV_ALIGN_TOP_MID, -150, 130);

    lv_obj_t *lbl_bar_temp_motor_max_title = lv_label_create(scr);
    lv_obj_add_style(lbl_bar_temp_motor_max_title, &ui_style_label_subtitle, 0);
    lv_label_set_text(lbl_bar_temp_motor_max_title, "Highest Motor Temperature");
    lv_obj_align_to(lbl_bar_temp_motor_max_title, bar_temp_motor_max, LV_ALIGN_OUT_TOP_LEFT, 00, 0);

    lv_obj_t *lbl_bar_temp_motor_max_value = ui_unit_label_create(scr,
                                      &BarlowCondensed_BoldItalic_32,
                                      &BarlowCondensed_Italic_20, "°C");
    lv_obj_add_style(lbl_bar_temp_motor_max_value, &ui_style_level_warn, UI_STATE_WARN);
    lv_obj_add_style(lbl_bar_temp_motor_max_value, &ui_style_level_crit, UI_STATE_CRIT);
    lv_obj_bind_state_if_lt(lbl_bar_temp_motor_max_value, &ui_subj_temperature_motor, UI_STATE_WARN, UI_VOLTAGE_ACCU_HV_WARN_LOW);
    lv_obj_bind_state_if_lt(lbl_bar_temp_motor_max_value, &ui_subj_temperature_motor, UI_STATE_CRIT, UI_VOLTAGE_ACCU_HV_CRIT_LOW);
    // lv_obj_set_size(lbl_bar_temp_motor_max_value, 60, 30);
    ui_unit_label_bind_value(lbl_bar_temp_motor_max_value, &ui_subj_temperature_motor, "%d");
    lv_obj_align_to(lbl_bar_temp_motor_max_value, bar_temp_motor_max, LV_ALIGN_OUT_RIGHT_MID, 20, 0);

    /* ── MAX Inverter Temperature ───────────────────────────────────────── */

    lv_obj_t * bar_temp_inv_max = lv_bar_create(scr);
    lv_obj_remove_style_all(bar_temp_inv_max);
    lv_obj_add_style(bar_temp_inv_max, &ui_style_slider_main, LV_PART_MAIN);
    lv_obj_add_style(bar_temp_inv_max, &ui_style_slider_indicator, LV_PART_INDICATOR);
    lv_bar_set_range(bar_temp_inv_max, 0, 80);
    lv_bar_bind_value(bar_temp_inv_max, &ui_subj_temperature_inverter);
    lv_obj_set_size(bar_temp_inv_max, 140, 20);
    lv_obj_align(bar_temp_inv_max, LV_ALIGN_TOP_MID, -150, 180);

    lv_obj_t *lbl_bar_temp_inv_max_title = lv_label_create(scr);
    lv_obj_add_style(lbl_bar_temp_inv_max_title, &ui_style_label_subtitle, 0);
    lv_label_set_text(lbl_bar_temp_inv_max_title, "Inverter Temperature");
    lv_obj_align_to(lbl_bar_temp_inv_max_title, bar_temp_inv_max, LV_ALIGN_OUT_TOP_LEFT, 0, 0);

    lv_obj_t *lbl_bar_temp_inv_max_value = ui_unit_label_create(scr,
                                      &BarlowCondensed_BoldItalic_32,
                                      &BarlowCondensed_Italic_20, "°C");
    lv_obj_add_style(lbl_bar_temp_inv_max_value, &ui_style_level_warn, UI_STATE_WARN);
    lv_obj_add_style(lbl_bar_temp_inv_max_value, &ui_style_level_crit, UI_STATE_CRIT);
    lv_obj_bind_state_if_lt(lbl_bar_temp_inv_max_value, &ui_subj_temperature_inverter, UI_STATE_WARN, UI_VOLTAGE_ACCU_HV_WARN_LOW);
    lv_obj_bind_state_if_lt(lbl_bar_temp_inv_max_value, &ui_subj_temperature_inverter, UI_STATE_CRIT, UI_VOLTAGE_ACCU_HV_CRIT_LOW);
    // lv_obj_set_size(lbl_bar_temp_inv_max_value, 60, 30);
    ui_unit_label_bind_value(lbl_bar_temp_inv_max_value, &ui_subj_temperature_inverter, "%d");
    lv_obj_align_to(lbl_bar_temp_inv_max_value, bar_temp_inv_max, LV_ALIGN_OUT_RIGHT_MID, 20, 0);

    /* ── APPS Left Position ─────────────────────────────────────────────── */

    lv_obj_t * bar_apps_l_pos = lv_bar_create(scr);
    lv_obj_remove_style_all(bar_apps_l_pos);
    lv_obj_add_style(bar_apps_l_pos, &ui_style_slider_main, LV_PART_MAIN);
    lv_obj_add_style(bar_apps_l_pos, &ui_style_slider_indicator, LV_PART_INDICATOR);
    lv_bar_set_range(bar_apps_l_pos, 0, 15000);
    lv_bar_bind_value(bar_apps_l_pos, &ui_subj_apps_left_position);
    lv_obj_set_size(bar_apps_l_pos, 140, 20);
    lv_obj_align(bar_apps_l_pos, LV_ALIGN_TOP_MID, 80, 80);

    lv_obj_t *lbl_bar_apps_l_pos_title = lv_label_create(scr);
    lv_obj_add_style(lbl_bar_apps_l_pos_title, &ui_style_label_subtitle, 0);
    lv_label_set_text(lbl_bar_apps_l_pos_title, "Left Accelerator Pedal Position");
    lv_obj_align_to(lbl_bar_apps_l_pos_title, bar_apps_l_pos, LV_ALIGN_OUT_TOP_LEFT, 00, 0);

    lv_obj_t *lbl_bar_apps_l_pos_value = ui_unit_label_create(scr,
                                      &BarlowCondensed_BoldItalic_32,
                                      &BarlowCondensed_Italic_20, "");
    lv_obj_add_style(lbl_bar_apps_l_pos_value, &ui_style_level_warn, UI_STATE_WARN);
    lv_obj_add_style(lbl_bar_apps_l_pos_value, &ui_style_level_crit, UI_STATE_CRIT);
    lv_obj_bind_state_if_lt(lbl_bar_apps_l_pos_value, &ui_subj_apps_left_position, UI_STATE_WARN, UI_VOLTAGE_ACCU_HV_WARN_LOW);
    lv_obj_bind_state_if_lt(lbl_bar_apps_l_pos_value, &ui_subj_apps_left_position, UI_STATE_CRIT, UI_VOLTAGE_ACCU_HV_CRIT_LOW);
    // lv_obj_set_size(lbl_bar_apps_l_pos_value, 60, 30);
    ui_unit_label_bind_value(lbl_bar_apps_l_pos_value, &ui_subj_apps_left_position, "%d");
    lv_obj_align_to(lbl_bar_apps_l_pos_value, bar_apps_l_pos, LV_ALIGN_OUT_RIGHT_MID, 20, 0);

    /* ── APPS Right Position ────────────────────────────────────────────── */

    lv_obj_t * bar_apps_r_pos = lv_bar_create(scr);
    lv_obj_remove_style_all(bar_apps_r_pos);
    lv_obj_add_style(bar_apps_r_pos, &ui_style_slider_main, LV_PART_MAIN);
    lv_obj_add_style(bar_apps_r_pos, &ui_style_slider_indicator, LV_PART_INDICATOR);
    lv_bar_set_range(bar_apps_r_pos, 0, 15000);
    lv_bar_bind_value(bar_apps_r_pos, &ui_subj_apps_right_position);
    lv_obj_set_size(bar_apps_r_pos, 140, 20);
    lv_obj_align(bar_apps_r_pos, LV_ALIGN_TOP_MID, 80, 130);

    lv_obj_t *lbl_bar_apps_r_pos_title = lv_label_create(scr);
    lv_obj_add_style(lbl_bar_apps_r_pos_title, &ui_style_label_subtitle, 0);
    lv_label_set_text(lbl_bar_apps_r_pos_title, "Right Accelerator Pedal Position");
    lv_obj_align_to(lbl_bar_apps_r_pos_title, bar_apps_r_pos, LV_ALIGN_OUT_TOP_LEFT, 00, 0);

    lv_obj_t *lbl_bar_apps_r_pos_value = ui_unit_label_create(scr,
                                      &BarlowCondensed_BoldItalic_32,
                                      &BarlowCondensed_Italic_20, "");
    lv_obj_add_style(lbl_bar_apps_r_pos_value, &ui_style_level_warn, UI_STATE_WARN);
    lv_obj_add_style(lbl_bar_apps_r_pos_value, &ui_style_level_crit, UI_STATE_CRIT);
    lv_obj_bind_state_if_lt(lbl_bar_apps_r_pos_value, &ui_subj_apps_right_position, UI_STATE_WARN, UI_VOLTAGE_ACCU_HV_WARN_LOW);
    lv_obj_bind_state_if_lt(lbl_bar_apps_r_pos_value, &ui_subj_apps_right_position, UI_STATE_CRIT, UI_VOLTAGE_ACCU_HV_CRIT_LOW);
    // lv_obj_set_size(lbl_bar_apps_r_pos_value, 60, 30);
    ui_unit_label_bind_value(lbl_bar_apps_r_pos_value, &ui_subj_apps_right_position, "%d");
    lv_obj_align_to(lbl_bar_apps_r_pos_value, bar_apps_r_pos, LV_ALIGN_OUT_RIGHT_MID, 20, 0);

    /* ── BPPS Position ──────────────────────────────────────────────────── */

    lv_obj_t * bar_bpps_pos = lv_bar_create(scr);
    lv_obj_remove_style_all(bar_bpps_pos);
    lv_obj_add_style(bar_bpps_pos, &ui_style_slider_main, LV_PART_MAIN);
    lv_obj_add_style(bar_bpps_pos, &ui_style_slider_indicator, LV_PART_INDICATOR);
    lv_bar_set_range(bar_bpps_pos, 0, 100);
    lv_bar_bind_value(bar_bpps_pos, &ui_subj_bpps_position);
    lv_obj_set_size(bar_bpps_pos, 140, 20);
    lv_obj_align(bar_bpps_pos, LV_ALIGN_TOP_MID, 80, 180);

    lv_obj_t *lbl_bar_bpps_pos_title = lv_label_create(scr);
    lv_obj_add_style(lbl_bar_bpps_pos_title, &ui_style_label_subtitle, 0);
    lv_label_set_text(lbl_bar_bpps_pos_title, "Brake Pedal Position");
    lv_obj_align_to(lbl_bar_bpps_pos_title, bar_bpps_pos, LV_ALIGN_OUT_TOP_LEFT, 00, 0);

    lv_obj_t *lbl_bar_bpps_pos_value = ui_unit_label_create(scr,
                                      &BarlowCondensed_BoldItalic_32,
                                      &BarlowCondensed_Italic_20, "");
    lv_obj_add_style(lbl_bar_bpps_pos_value, &ui_style_level_warn, UI_STATE_WARN);
    lv_obj_add_style(lbl_bar_bpps_pos_value, &ui_style_level_crit, UI_STATE_CRIT);
    lv_obj_bind_state_if_lt(lbl_bar_bpps_pos_value, &ui_subj_bpps_position, UI_STATE_WARN, UI_VOLTAGE_ACCU_HV_WARN_LOW);
    lv_obj_bind_state_if_lt(lbl_bar_bpps_pos_value, &ui_subj_bpps_position, UI_STATE_CRIT, UI_VOLTAGE_ACCU_HV_CRIT_LOW);
    // lv_obj_set_size(lbl_bar_bpps_pos_value, 60, 30);
    ui_unit_label_bind_value(lbl_bar_bpps_pos_value, &ui_subj_bpps_position, "%d");
    lv_obj_align_to(lbl_bar_bpps_pos_value, bar_bpps_pos, LV_ALIGN_OUT_RIGHT_MID, 20, 0);
}

/* ── Public Function Implementations ─────────────────────────────────────────────────────────── */

lv_obj_t *screen_debug_tractive_system_create(lv_subject_t *status_subjects)
{
    /* ── Screen base ─────────────────────────────────────────────────────── */

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_add_style(scr, &ui_style_screen, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* ── Widgets ─────────────────────────────────────────────────────────── */

    ui_header_create(scr, "DBG TS", status_subjects);
    build_bars(scr);

    return scr;
}

lv_group_t *screen_debug_tractive_system_get_right_encoder_group(void)
{
    return NULL;
}

lv_group_t *screen_debug_tractive_system_get_left_button_group(void)
{
    return NULL;
}

lv_group_t *screen_debug_tractive_system_get_right_button_group(void)
{
    return NULL;
}

