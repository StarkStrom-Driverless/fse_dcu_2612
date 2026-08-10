/**
 * @file        screen_debug_lv_accu.c
 * @brief       Debug screen for low voltage accumulator
 *
 * @details     
 *
 * @author      Mario Wegmann <mario.wegmann@web.de>
 * @date        Created: 2026-06-25
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
 * 0.1.0    2026-06-25  Mario Wegmann   Initial creation
 * ─────────────────────────────────────────────────────────────────────────────────────────────────
 */

/* ── Corresponding Header ────────────────────────────────────────────────────────────────────── */

#include "modules/ui/screens/screen_debug_lv_accu.h"

/* ── Zephyr Includes ─────────────────────────────────────────────────────────────────────────── */

#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/logging/log.h>

/* ── Project Includes ────────────────────────────────────────────────────────────────────────── */

#include "app/app_state.h"
#include "modules/ui/ui_styles.h"
#include "modules/ui/widgets/ui_unit_label.h"
#include "services/event_bus/event_bus.h"
#include "services/event_bus/events.h"
#include "generated/ui_subjects_gen.h"

/* ── Zephyr Logging ──────────────────────────────────────────────────────────────────────────── */

LOG_MODULE_REGISTER(screen_debug_lv_accu, CONFIG_LOG_DEFAULT_LEVEL);


/* ── Private Macros & Constants ──────────────────────────────────────────────────────────────── */

/* ── Private Variables ───────────────────────────────────────────────────────────────────────── */

/* ── Private Function Prototypes ─────────────────────────────────────────────────────────────── */

static void build_header(lv_obj_t *scr);
static void build_bars(lv_obj_t *scr);

/* ── Private Function Implementations ───────────────────────────────────────────────────────── */

static void build_header(lv_obj_t *scr)
{
    lv_obj_t *header = lv_obj_create(scr);
    lv_obj_remove_style_all(header);
    lv_obj_add_style(header, &ui_style_header, 0);
    lv_obj_set_width(header,  lv_pct(100));
    lv_obj_set_height(header, lv_pct(15));
    lv_obj_align(header, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(header);
    lv_obj_add_style(title, &ui_style_label_title, 0);
    /* White text: gradient ends in UI_C_DARK, dark-on-dark would be illegible. */
    // lv_obj_set_style_text_color(title, UI_C_WHITE, 0);
    lv_label_set_text(title, "DBG LV ACCU");
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 10, 0);
}


static void build_bars(lv_obj_t *scr)
{
    /* ── LV Accu Voltage ────────────────────────────────────────────────── */

    lv_obj_t * bar_lv_accu_volt = lv_bar_create(scr);
    lv_obj_remove_style_all(bar_lv_accu_volt);
    lv_obj_add_style(bar_lv_accu_volt, &ui_style_slider_main, LV_PART_MAIN);
    lv_obj_add_style(bar_lv_accu_volt, &ui_style_slider_indicator, LV_PART_INDICATOR);
    lv_bar_set_range(bar_lv_accu_volt, 0, 36);
    lv_bar_bind_value(bar_lv_accu_volt, &ui_subj_lv_accu_voltage);
    lv_obj_set_size(bar_lv_accu_volt, 300, 20);
    lv_obj_align(bar_lv_accu_volt, LV_ALIGN_TOP_MID, -70, 80);

    lv_obj_t *lbl_bar_lv_accu_volt_title = lv_label_create(scr);
    lv_obj_add_style(lbl_bar_lv_accu_volt_title, &ui_style_label_subtitle, 0);
    lv_label_set_text(lbl_bar_lv_accu_volt_title, "LV Accu Voltage");
    lv_obj_align_to(lbl_bar_lv_accu_volt_title, bar_lv_accu_volt, LV_ALIGN_OUT_TOP_LEFT, 0, 0);

    lv_obj_t *lbl_bar_lv_accu_volt_value = ui_unit_label_create(scr,
                                      &BarlowCondensed_BoldItalic_32,
                                      &BarlowCondensed_Italic_20, "V");
    lv_obj_add_style(lbl_bar_lv_accu_volt_value, &ui_style_level_warn, UI_STATE_WARN);
    lv_obj_add_style(lbl_bar_lv_accu_volt_value, &ui_style_level_crit, UI_STATE_CRIT);
    lv_obj_bind_state_if_lt(lbl_bar_lv_accu_volt_value, &ui_subj_lv_accu_voltage, UI_STATE_WARN, UI_LV_ACCU_VOLTAGE_WARN_LOW);
    lv_obj_bind_state_if_lt(lbl_bar_lv_accu_volt_value, &ui_subj_lv_accu_voltage, UI_STATE_CRIT, UI_LV_ACCU_VOLTAGE_CRIT_LOW);
    // lv_obj_set_size(lbl_bar_lv_accu_volt_value, 60, 30);
    ui_unit_label_bind_value(lbl_bar_lv_accu_volt_value, &ui_subj_lv_accu_voltage, "%.2f");
    lv_obj_align_to(lbl_bar_lv_accu_volt_value, bar_lv_accu_volt, LV_ALIGN_OUT_RIGHT_MID, 20, 0);

    /* ── LV Accu temperature ────────────────────────────────────────────── */

    // lv_obj_t * bar_hv_accu_temp = lv_bar_create(scr);
    // lv_obj_remove_style_all(bar_hv_accu_temp);
    // lv_obj_add_style(bar_hv_accu_temp, &ui_style_slider_main, LV_PART_MAIN);
    // lv_obj_add_style(bar_hv_accu_temp, &ui_style_slider_indicator, LV_PART_INDICATOR);
    // lv_bar_set_range(bar_hv_accu_temp, 0, 100);
    // lv_bar_bind_value(bar_hv_accu_temp, &ui_subj_temperature_accu_hv);
    // lv_obj_set_size(bar_hv_accu_temp, 300, 20);
    // lv_obj_align(bar_hv_accu_temp, LV_ALIGN_TOP_MID, -70, 130);

    // lv_obj_t *lbl_bar_hv_accu_temp_title = lv_label_create(scr);
    // lv_obj_add_style(lbl_bar_hv_accu_temp_title, &ui_style_label_subtitle, 0);
    // lv_label_set_text(lbl_bar_hv_accu_temp_title, "HV Accu Temperature");
    // lv_obj_align_to(lbl_bar_hv_accu_temp_title, bar_hv_accu_temp, LV_ALIGN_OUT_TOP_LEFT, 00, 0);

    // lv_obj_t *lbl_bar_hv_accu_temp_value = ui_unit_label_create(scr,
    //                                   &BarlowCondensed_BoldItalic_32,
    //                                   &BarlowCondensed_Italic_20, "°C");
    // lv_obj_add_style(lbl_bar_hv_accu_temp_value, &ui_style_level_warn, UI_STATE_WARN);
    // lv_obj_add_style(lbl_bar_hv_accu_temp_value, &ui_style_level_crit, UI_STATE_CRIT);
    // lv_obj_bind_state_if_lt(lbl_bar_hv_accu_temp_value, &ui_subj_temperature_accu_hv, UI_STATE_WARN, UI_VOLTAGE_ACCU_HV_WARN_LOW);
    // lv_obj_bind_state_if_lt(lbl_bar_hv_accu_temp_value, &ui_subj_temperature_accu_hv, UI_STATE_CRIT, UI_VOLTAGE_ACCU_HV_CRIT_LOW);
    // // lv_obj_set_size(lbl_bar_hv_accu_temp_value, 60, 30);
    // ui_unit_label_bind_value(lbl_bar_hv_accu_temp_value, &ui_subj_temperature_accu_hv, "%d");
    // lv_obj_align_to(lbl_bar_hv_accu_temp_value, bar_hv_accu_temp, LV_ALIGN_OUT_RIGHT_MID, 20, 0);

    // /* ── Air Pressure Front ───────────────────────────────────────────── */

    // lv_obj_t * bar_air_front = lv_bar_create(scr);
    // lv_obj_remove_style_all(bar_air_front);
    // lv_obj_add_style(bar_air_front, &ui_style_slider_main, LV_PART_MAIN);
    // lv_obj_add_style(bar_air_front, &ui_style_slider_indicator, LV_PART_INDICATOR);
    // lv_bar_set_range(bar_air_front, 0, 100);
    // lv_bar_bind_value(bar_air_front, &ui_subj_air_pressure_front);
    // lv_obj_set_size(bar_air_front, 300, 20);
    // lv_obj_align(bar_air_front, LV_ALIGN_TOP_MID, -70, 180);

    // lv_obj_t *lbl_bar_air_front_title = lv_label_create(scr);
    // lv_obj_add_style(lbl_bar_air_front_title, &ui_style_label_subtitle, 0);
    // lv_label_set_text(lbl_bar_air_front_title, "Air Pressure Front");
    // lv_obj_align_to(lbl_bar_air_front_title, bar_air_front, LV_ALIGN_OUT_TOP_LEFT, 0, 0);

    // lv_obj_t *lbl_bar_air_front_value = ui_unit_label_create(scr,
    //                                   &BarlowCondensed_BoldItalic_32,
    //                                   &BarlowCondensed_Italic_20, "Bar");
    // lv_obj_add_style(lbl_bar_air_front_value, &ui_style_level_warn, UI_STATE_WARN);
    // lv_obj_add_style(lbl_bar_air_front_value, &ui_style_level_crit, UI_STATE_CRIT);
    // lv_obj_bind_state_if_lt(lbl_bar_air_front_value, &ui_subj_air_pressure_front, UI_STATE_WARN, UI_VOLTAGE_ACCU_HV_WARN_LOW);
    // lv_obj_bind_state_if_lt(lbl_bar_air_front_value, &ui_subj_air_pressure_front, UI_STATE_CRIT, UI_VOLTAGE_ACCU_HV_CRIT_LOW);
    // // lv_obj_set_size(lbl_bar_air_front_value, 60, 30);
    // ui_unit_label_bind_value(lbl_bar_air_front_value, &ui_subj_air_pressure_front, "%.2f");
    // lv_obj_align_to(lbl_bar_air_front_value, bar_air_front, LV_ALIGN_OUT_RIGHT_MID, 20, 0);

    // /* ── Air Pressure Rear ────────────────────────────────────────────── */

    // lv_obj_t * bar_air_rear = lv_bar_create(scr);
    // lv_obj_remove_style_all(bar_air_rear);
    // lv_obj_add_style(bar_air_rear, &ui_style_slider_main, LV_PART_MAIN);
    // lv_obj_add_style(bar_air_rear, &ui_style_slider_indicator, LV_PART_INDICATOR);
    // lv_bar_set_range(bar_air_rear, 0, 100);
    // lv_bar_bind_value(bar_air_rear, &ui_subj_air_pressure_rear);
    // lv_obj_set_size(bar_air_rear, 300, 20);
    // lv_obj_align(bar_air_rear, LV_ALIGN_TOP_MID, -70, 230);

    // lv_obj_t *lbl_bar_air_rear_title = lv_label_create(scr);
    // lv_obj_add_style(lbl_bar_air_rear_title, &ui_style_label_subtitle, 0);
    // lv_label_set_text(lbl_bar_air_rear_title, "Air Pressure Rear");
    // lv_obj_align_to(lbl_bar_air_rear_title, bar_air_rear, LV_ALIGN_OUT_TOP_LEFT, 00, 0);

    // lv_obj_t *lbl_bar_air_rear_value = ui_unit_label_create(scr,
    //                                   &BarlowCondensed_BoldItalic_32,
    //                                   &BarlowCondensed_Italic_20, "Bar");
    // lv_obj_add_style(lbl_bar_air_rear_value, &ui_style_level_warn, UI_STATE_WARN);
    // lv_obj_add_style(lbl_bar_air_rear_value, &ui_style_level_crit, UI_STATE_CRIT);
    // lv_obj_bind_state_if_lt(lbl_bar_air_rear_value, &ui_subj_air_pressure_rear, UI_STATE_WARN, UI_VOLTAGE_ACCU_HV_WARN_LOW);
    // lv_obj_bind_state_if_lt(lbl_bar_air_rear_value, &ui_subj_air_pressure_rear, UI_STATE_CRIT, UI_VOLTAGE_ACCU_HV_CRIT_LOW);
    // // lv_obj_set_size(lbl_bar_air_rear_value, 60, 30);
    // ui_unit_label_bind_value(lbl_bar_air_rear_value, &ui_subj_air_pressure_rear, "%.2f");
    // lv_obj_align_to(lbl_bar_air_rear_value, bar_air_rear, LV_ALIGN_OUT_RIGHT_MID, 20, 0);
}

/* ── Public Function Implementations ─────────────────────────────────────────────────────────── */

lv_obj_t *screen_debug_lv_accu_create(void)
{
    /* ── Screen base ─────────────────────────────────────────────────────── */

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_add_style(scr, &ui_style_screen, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* ── Widgets ─────────────────────────────────────────────────────────── */

    build_header(scr);
    build_bars(scr);

    return scr;
}

lv_group_t *screen_debug_lv_accu_get_right_encoder_group(void)
{
    return NULL;
}

lv_group_t *screen_debug_lv_accu_get_left_button_group(void)
{
    return NULL;
}

lv_group_t *screen_debug_lv_accu_get_right_button_group(void)
{
    return NULL;
}
