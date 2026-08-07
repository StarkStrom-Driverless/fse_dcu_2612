/**
 * @file        screen_ev_driving.c
 * @brief       EV driving screen implementation
 *
 * @details     Builds the ev driving screen with:
 *
 *                – Roller  : lists all seven FS disciplines; controlled by the
 *                            right encoder via an lv_group_t.  Rolling does NOT
 *                            publish any Zbus event — the selection is only
 *                            transmitted when OK or RTD is pressed.
 *
 *                – OK button  : confirms the roller's current selection and
 *                               publishes UI_INPUT_MISSION_SELECTED to
 *                               ui_input_chan.  The App Layer then calls
 *                               app_state_set_mission() and sends the mission
 *                               over CAN (CAN_TX_CMD_SEND_MISSION).
 *
 *                – RTD button : requests Ready-to-Drive by publishing
 *                               UI_INPUT_RTD_REQUEST to ui_input_chan.  The
 *                               App Layer sends CAN_TX_CMD_SEND_RTD_REQUEST
 *                               using the last-known drive mode.  Can be
 *                               pressed without having first confirmed a mission
 *                               (MISSION_NONE drive mode = 0 is then used).
 *
 *              Right encoder interaction (LVGL group)
 *              ───────────────────────────────────────
 *              Tab order: [Roller] → [OK] → [RTD] (wraps around)
 *
 *              Roller focused, NAVIGATE mode  : encoder moves focus to next obj
 *              Roller focused, EDIT mode       : encoder scrolls mission list
 *              Toggle NAVIGATE ↔ EDIT          : physical OK button (LV_KEY_ENTER)
 *              Button focused                  : LV_KEY_ENTER → LV_EVENT_CLICKED
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

/* ── Corresponding Header ────────────────────────────────────────────────────────────────────── */

#include "modules/ui/screens/screen_ev_driving.h"

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
#include "generated/ui_tx_subjects_gen.h"

/* ── Zephyr Logging ──────────────────────────────────────────────────────────────────────────── */

LOG_MODULE_REGISTER(screen_ev_driving, CONFIG_LOG_DEFAULT_LEVEL);


/* ── Private Macros & Constants ──────────────────────────────────────────────────────────────── */

/** @brief Width of each action button in pixels. */
#define BTN_WIDTH               100

/** @brief Height of each action button in pixels. */
#define BTN_HEIGHT              50

/**
 * @brief Half the centre-to-centre distance between the two buttons.
 *
 * Layout: |←BTN_WIDTH→| 10px gap |←BTN_WIDTH→|
 *          centre-to-centre = BTN_WIDTH + 10 = 110 px → half = 55 px
 */
#define BTN_HALF_SPACING        100

/** @brief Bottom margin for the button row (pixels from screen bottom). */
#define BTN_BOTTOM_MARGIN       0


/* ── Private Variables ───────────────────────────────────────────────────────────────────────── */

static lv_subject_t s_sldr_left_val;  /* TQG F slider value */
static lv_subject_t s_sldr_right_val; /* TQG R slider value */
static bool         s_subjects_init;

/** @brief Left slider */
static lv_obj_t   *s_sldr_left;

/** @brief Right slider */
static lv_obj_t   *s_sldr_right;

/** @brief Middle slider */
static lv_obj_t   *s_sldr_middle;


/** @brief OK button */
static lv_obj_t   *s_btn_right;

static lv_obj_t   *s_lbl_btn_right_value;

/** @brief RTD button — requests Ready-to-Drive with the last-known drive mode. */
static lv_obj_t   *s_btn_left;

static lv_obj_t   *s_lbl_btn_left_value;

/** @brief LVGL input group for the right encoder. */
static lv_group_t *s_right_encoder_group;

/** @brief LVGL input group for the left encoder. */
static lv_group_t *s_left_button_group;

/** @brief LVGL input group for the right button. */
static lv_group_t *s_right_button_group;


/* ── Private Function Prototypes ─────────────────────────────────────────────────────────────── */
static void build_sliders(lv_obj_t *scr);
static void build_buttons(lv_obj_t *scr);
static void btn_left_event_cb(lv_event_t *e);
static void btn_right_event_cb(lv_event_t *e);
static void tq_vect_observer_cb(lv_observer_t *observer, lv_subject_t *subject);
static void pwr_limit_observer_cb(lv_observer_t *observer, lv_subject_t *subject);


/* ── Private Function Implementations ───────────────────────────────────────────────────────── */

static void sldr_left_value_changed_cb(lv_event_t *e)
{
    lv_subject_set_int(&s_sldr_left_val,
                       lv_slider_get_value(lv_event_get_target_obj(e)));
}

static void sldr_right_value_changed_cb(lv_event_t *e)
{
    lv_subject_set_int(&s_sldr_right_val,
                       lv_slider_get_value(lv_event_get_target_obj(e)));
}

static void tq_vect_observer_cb(lv_observer_t *observer, lv_subject_t *subject)
{
    lv_obj_t *btn = lv_observer_get_target_obj(observer);
    bool on = lv_subject_get_int(subject) != 0;

    if (on) {
        lv_obj_add_state(btn, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(btn, LV_STATE_CHECKED);
    }
    lv_label_set_text(s_lbl_btn_right_value, on ? "ON" : "OFF");
    lv_obj_set_style_text_color(s_lbl_btn_right_value,
                                on ? UI_C_WHITE : UI_C_DARK, 0);
}

static void pwr_limit_observer_cb(lv_observer_t *observer, lv_subject_t *subject)
{
    lv_obj_t *btn = lv_observer_get_target_obj(observer);
    if (lv_subject_get_int(subject)) {
        lv_obj_add_state(btn, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(btn, LV_STATE_CHECKED);
    }
}

static void build_sliders(lv_obj_t *scr)
{
    /* ── Slider Left ────────────────────────────────────────────────────── */

    s_sldr_left = lv_slider_create(scr);
    lv_obj_remove_style_all(s_sldr_left);
    lv_obj_add_style(s_sldr_left, &ui_style_slider_main, LV_PART_MAIN);
    lv_obj_add_style(s_sldr_left, &ui_style_slider_indicator, LV_PART_INDICATOR);
    lv_obj_set_size(s_sldr_left, 40, 200);
    lv_obj_set_pos(s_sldr_left, 10, 70);

    lv_slider_set_value(s_sldr_left, (int32_t)lv_subject_get_int(&s_sldr_left_val), LV_ANIM_OFF);
    lv_obj_add_event_cb(s_sldr_left, sldr_left_value_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *lbl_sldr_left_title = lv_label_create(scr);
    lv_obj_add_style(lbl_sldr_left_title, &ui_style_label_subtitle, 0);
    lv_label_set_text(lbl_sldr_left_title, "TQG F");
    lv_obj_align_to(lbl_sldr_left_title, s_sldr_left, LV_ALIGN_OUT_TOP_MID, 0, 0);

    /* ── Slider Right ───────────────────────────────────────────────────── */

    s_sldr_right = lv_slider_create(scr);
    lv_obj_remove_style_all(s_sldr_right);
    lv_obj_add_style(s_sldr_right, &ui_style_slider_main, LV_PART_MAIN);
    lv_obj_add_style(s_sldr_right, &ui_style_slider_indicator, LV_PART_INDICATOR);
    lv_obj_set_size(s_sldr_right, 40, 200);
    lv_obj_set_pos(s_sldr_right, 430, 70);

    lv_slider_set_value(s_sldr_right, (int32_t)lv_subject_get_int(&s_sldr_right_val), LV_ANIM_OFF);
    lv_obj_add_event_cb(s_sldr_right, sldr_right_value_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *lbl_sldr_right_title = lv_label_create(scr);
    lv_obj_add_style(lbl_sldr_right_title, &ui_style_label_subtitle, 0);
    lv_label_set_text(lbl_sldr_right_title, "TQG R");
    lv_obj_align_to(lbl_sldr_right_title, s_sldr_right, LV_ALIGN_OUT_TOP_MID, 0, 0);

    /* ── Slider Middle ──────────────────────────────────────────────────── */

    s_sldr_middle = lv_slider_create(scr);
    lv_obj_remove_style_all(s_sldr_middle);
    lv_obj_add_style(s_sldr_middle, &ui_style_slider_main, LV_PART_MAIN);
    lv_obj_add_style(s_sldr_middle, &ui_style_slider_indicator, LV_PART_INDICATOR);
    lv_obj_set_size(s_sldr_middle, 300, 20);
    lv_slider_set_range(s_sldr_middle, 0, 500);
    lv_slider_bind_value(s_sldr_middle, &ui_subj_voltage_accu_hv);
    lv_obj_align(s_sldr_middle, LV_ALIGN_BOTTOM_MID, 0, -60);

    lv_obj_t *lbl_sldr_middle_title = lv_label_create(scr);
    lv_obj_add_style(lbl_sldr_middle_title, &ui_style_label_subtitle, 0);
    lv_label_set_text(lbl_sldr_middle_title, "HV SoC");
    lv_obj_align_to(lbl_sldr_middle_title, s_sldr_middle, LV_ALIGN_OUT_TOP_LEFT, 0, 0);    

    lv_obj_t *lbl_sldr_middle_value = ui_unit_label_create(scr,
                                      &BarlowCondensed_BoldItalic_32,
                                      &BarlowCondensed_Italic_20, "V");
    lv_obj_add_style(lbl_sldr_middle_value, &ui_style_level_warn, UI_STATE_WARN);
    lv_obj_add_style(lbl_sldr_middle_value, &ui_style_level_crit, UI_STATE_CRIT);
    lv_obj_bind_state_if_lt(lbl_sldr_middle_value, &ui_subj_voltage_accu_hv, UI_STATE_WARN, UI_VOLTAGE_ACCU_HV_WARN_LOW);
    lv_obj_bind_state_if_lt(lbl_sldr_middle_value, &ui_subj_voltage_accu_hv, UI_STATE_CRIT, UI_VOLTAGE_ACCU_HV_CRIT_LOW);
    lv_obj_set_size(lbl_sldr_middle_value, 60, 30);
    ui_unit_label_bind_value(lbl_sldr_middle_value, &ui_subj_voltage_accu_hv, "%d");
    lv_obj_align_to(lbl_sldr_middle_value, s_sldr_middle, LV_ALIGN_OUT_TOP_RIGHT, 0, 0);
    
}

static void build_labels(lv_obj_t *scr)
{
    /* ── Label HV Accu Temp ─────────────────────────────────────────────── */
    lv_obj_t *lbl_temp_hv_accu_value = ui_unit_label_create(scr,
                                      &BarlowCondensed_BoldItalic_80,
                                      &BarlowCondensed_Italic_44, "°C");
    lv_obj_add_style(lbl_temp_hv_accu_value, &ui_style_level_warn, UI_STATE_WARN);
    lv_obj_add_style(lbl_temp_hv_accu_value, &ui_style_level_crit, UI_STATE_CRIT);
    // lv_obj_bind_state_if_lt(lbl_temp_hv_accu_value, &ui_subj_voltage_accu_hv, UI_STATE_WARN, UI_VOLTAGE_ACCU_HV_WARN_LOW);
    // lv_obj_bind_state_if_lt(lbl_temp_hv_accu_value, &ui_subj_voltage_accu_hv, UI_STATE_CRIT, UI_VOLTAGE_ACCU_HV_CRIT_LOW);
    // lv_obj_set_size(lbl_temp_hv_accu_value, 60, 30);
    ui_unit_label_bind_value(lbl_temp_hv_accu_value, &ui_subj_temperature_accu_hv, "%d");
    // lv_obj_align_to(lbl_temp_hv_accu_value, s_sldr_middle, LV_ALIGN_OUT_TOP_RIGHT, 0, 0);
    lv_obj_set_pos(lbl_temp_hv_accu_value, 65, 100);

    lv_obj_t *lbl_temp_hv_accu_title = lv_label_create(scr);
    lv_obj_add_style(lbl_temp_hv_accu_title, &ui_style_label_subtitle, 0);
    lv_label_set_text(lbl_temp_hv_accu_title, "HV Accu Temp");
    lv_obj_align_to(lbl_temp_hv_accu_title, lbl_temp_hv_accu_value, LV_ALIGN_OUT_TOP_MID, 0, 0);
    
    /* ── Label Inverter Temp ────────────────────────────────────────────── */

    lv_obj_t *lbl_temp_inverter_value = ui_unit_label_create(scr,
                                      &BarlowCondensed_BoldItalic_80,
                                      &BarlowCondensed_Italic_44, "°C");
    lv_obj_add_style(lbl_temp_inverter_value, &ui_style_level_warn, UI_STATE_WARN);
    lv_obj_add_style(lbl_temp_inverter_value, &ui_style_level_crit, UI_STATE_CRIT);
    // lv_obj_bind_state_if_lt(lbl_temp_inverter_value, &ui_subj_voltage_accu_hv, UI_STATE_WARN, UI_VOLTAGE_ACCU_HV_WARN_LOW);
    // lv_obj_bind_state_if_lt(lbl_temp_inverter_value, &ui_subj_voltage_accu_hv, UI_STATE_CRIT, UI_VOLTAGE_ACCU_HV_CRIT_LOW);
    // lv_obj_set_size(lbl_temp_inverter_value, 60, 30);
    ui_unit_label_bind_value(lbl_temp_inverter_value, &ui_subj_temperature_inverter, "%d");
    // lv_obj_align_to(lbl_temp_inverter_value, s_sldr_middle, LV_ALIGN_OUT_TOP_RIGHT, 0, 0);
    lv_obj_set_pos(lbl_temp_inverter_value, 185, 100);

    lv_obj_t *lbl_temp_inverter_title = lv_label_create(scr);
    lv_obj_add_style(lbl_temp_inverter_title, &ui_style_label_subtitle, 0);
    lv_label_set_text(lbl_temp_inverter_title, "Inverter Temp");
    lv_obj_align_to(lbl_temp_inverter_title, lbl_temp_inverter_value, LV_ALIGN_OUT_TOP_MID, 0, 0); 
    
    /* ── Label Motor Temp ───────────────────────────────────────────────── */

    lv_obj_t *lbl_temp_motor_value = ui_unit_label_create(scr,
                                      &BarlowCondensed_BoldItalic_80,
                                      &BarlowCondensed_Italic_44, "°C");
    lv_obj_add_style(lbl_temp_motor_value, &ui_style_level_warn, UI_STATE_WARN);
    lv_obj_add_style(lbl_temp_motor_value, &ui_style_level_crit, UI_STATE_CRIT);
    // lv_obj_bind_state_if_lt(lbl_temp_motor_value, &ui_subj_voltage_accu_hv, UI_STATE_WARN, UI_VOLTAGE_ACCU_HV_WARN_LOW);
    // lv_obj_bind_state_if_lt(lbl_temp_motor_value, &ui_subj_voltage_accu_hv, UI_STATE_CRIT, UI_VOLTAGE_ACCU_HV_CRIT_LOW);
    // lv_obj_set_size(lbl_temp_motor_value, 60, 30);
    ui_unit_label_bind_value(lbl_temp_motor_value, &ui_subj_temperature_motor, "%d");
    // lv_obj_align_to(lbl_temp_motor_value, s_sldr_middle, LV_ALIGN_OUT_TOP_RIGHT, 0, 0);
    lv_obj_set_pos(lbl_temp_motor_value, 305, 100);

    lv_obj_t *lbl_temp_motor_title = lv_label_create(scr);
    lv_obj_add_style(lbl_temp_motor_title, &ui_style_label_subtitle, 0);
    lv_label_set_text(lbl_temp_motor_title, "Motor Temp");
    lv_obj_align_to(lbl_temp_motor_title, lbl_temp_motor_value, LV_ALIGN_OUT_TOP_MID, 0, 0);  

    // lv_obj_t *lbl_mean_power = lv_label_create(scr);
    // lv_obj_add_style(lbl_mean_power, &ui_style_label_value_lg, 0);
    // lv_obj_add_style(lbl_mean_power, &ui_style_level_warn, UI_STATE_WARN);
    // lv_obj_add_style(lbl_mean_power, &ui_style_level_crit, UI_STATE_CRIT);
    // lv_obj_bind_state_if_gt(lbl_mean_power, &ui_subj_power_average, UI_STATE_WARN, UI_POWER_AVERAGE_WARN_HIGH);
    // lv_obj_bind_state_if_gt(lbl_mean_power, &ui_subj_power_average, UI_STATE_CRIT, UI_POWER_AVERAGE_CRIT_HIGH);
    // lv_label_bind_text(lbl_mean_power, &ui_subj_power_average, "%d");
    // lv_obj_align(lbl_mean_power, LV_ALIGN_LEFT_MID, 10, 0);

    // lv_obj_t *lbl_hv_volt_akku = ui_unit_label_create(scr,
    //                                   &BarlowCondensed_BoldItalic_100,
    //                                   &BarlowCondensed_Italic_44, "V");
    // lv_obj_add_style(lbl_hv_volt_akku, &ui_style_level_warn, UI_STATE_WARN);
    // lv_obj_add_style(lbl_hv_volt_akku, &ui_style_level_crit, UI_STATE_CRIT);
    // lv_obj_bind_state_if_lt(lbl_hv_volt_akku, &ui_subj_voltage_accu_hv, UI_STATE_WARN, UI_VOLTAGE_ACCU_HV_WARN_LOW);
    // lv_obj_bind_state_if_lt(lbl_hv_volt_akku, &ui_subj_voltage_accu_hv, UI_STATE_CRIT, UI_VOLTAGE_ACCU_HV_CRIT_LOW);
    // ui_unit_label_bind_value(lbl_hv_volt_akku, &ui_subj_voltage_accu_hv, "%d");
    // lv_obj_align(lbl_hv_volt_akku, LV_ALIGN_LEFT_MID, 150, 0);

    // lv_obj_t *lbl_hv_volt_ts = lv_label_create(scr);
    // lv_obj_add_style(lbl_hv_volt_ts, &ui_style_label_value_lg, 0);
    // lv_obj_set_style_text_color(lbl_hv_volt_ts, UI_C_DARK, 0);
    // lv_label_bind_text(lbl_hv_volt_ts, &ui_subj_voltage_tractive_system, "%d");
    // lv_obj_align(lbl_hv_volt_ts, LV_ALIGN_LEFT_MID, 300, 0);
}

static void build_buttons(lv_obj_t *scr)
{
    /* ── Power Limit button ─────────────────────────────────────────────── */

    s_btn_left = lv_button_create(scr);
    lv_obj_remove_style_all(s_btn_left);
    lv_obj_add_style(s_btn_left, &ui_style_btn_default, 0);
    lv_obj_add_style(s_btn_left, &ui_style_btn_checked, LV_STATE_PRESSED);
    lv_obj_add_style(s_btn_left, &ui_style_btn_focused, LV_STATE_FOCUS_KEY);
    lv_obj_set_size(s_btn_left, BTN_WIDTH, BTN_HEIGHT);
    lv_obj_align(s_btn_left, LV_ALIGN_BOTTOM_MID, -BTN_HALF_SPACING, -BTN_BOTTOM_MARGIN);

    lv_obj_t *lbl_esc = lv_label_create(s_btn_left);
    lv_obj_add_style(lbl_esc, &ui_style_label_subtitle, 0);
    lv_label_set_text(lbl_esc, "PWR Limit");
    lv_obj_align(lbl_esc, LV_ALIGN_CENTER, 0, 0);

    lv_obj_add_flag(s_btn_left, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_add_event_cb(s_btn_left, btn_left_event_cb, LV_EVENT_CLICKED, NULL);
    lv_subject_add_observer_obj(&ui_tx_subj_pwrlimit_setting, pwr_limit_observer_cb, s_btn_left, NULL);
    

    /* ── Torque Vectoring button ────────────────────────────────────────── */

    s_btn_right = lv_button_create(scr);
    lv_obj_remove_style_all(s_btn_right);
    lv_obj_add_style(s_btn_right, &ui_style_btn_default, 0);
    lv_obj_add_style(s_btn_right, &ui_style_btn_checked, LV_STATE_CHECKED);
    lv_obj_set_size(s_btn_right, BTN_WIDTH, BTN_HEIGHT);
    lv_obj_add_flag(s_btn_right, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_align(s_btn_right, LV_ALIGN_BOTTOM_MID, BTN_HALF_SPACING, -BTN_BOTTOM_MARGIN);

    lv_obj_t *lbl_btn_right_title = lv_label_create(s_btn_right);
    lv_obj_add_style(lbl_btn_right_title, &ui_style_label_subtitle, 0);
    lv_label_set_text(lbl_btn_right_title, "TQ Vect");
    lv_obj_align(lbl_btn_right_title, LV_ALIGN_TOP_LEFT, 0, 0);

    s_lbl_btn_right_value = lv_label_create(s_btn_right);
    lv_obj_add_style(s_lbl_btn_right_value, &ui_style_label_title, 0);
    lv_label_set_text(s_lbl_btn_right_value, "OFF");
    lv_obj_align(s_lbl_btn_right_value, LV_ALIGN_BOTTOM_LEFT, 0, 8);

    lv_obj_add_event_cb(s_btn_right, btn_right_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_subject_add_observer_obj(&ui_tx_subj_torquevect_setting, tq_vect_observer_cb, s_btn_right, NULL);

    

    // lv_obj_t * btn1 = lv_button_create(lv_screen_active());
    // lv_obj_remove_style_all(btn1);
    // lv_obj_add_style(btn1, &style, 0);
    // lv_obj_add_style(btn1, &style_checked, LV_STATE_CHECKED);
    // lv_obj_set_size(btn1, 140, 50);
    // lv_obj_align(btn1, LV_ALIGN_BOTTOM_MID, -80, 0);
    // lv_obj_add_flag(btn1, LV_OBJ_FLAG_CHECKABLE);
    // lv_obj_add_event_cb(btn1, button_event_cb, LV_EVENT_VALUE_CHANGED, (void *)BUTTON_LEFT);

    // label1 = lv_label_create(btn1);
    // lv_obj_set_style_text_font(label1, &BarlowCondensed_BoldItalic_18, 0);
    // lv_label_set_text(label1, "PWR Limit");
    // lv_obj_align(label1, LV_ALIGN_TOP_LEFT, 0, 0);

    // buttonLeftValueLabel = lv_label_create(btn1);
    // lv_obj_set_style_text_font(buttonLeftValueLabel, &BarlowCondensed_BoldItalic_32, 0);
    // lv_label_set_text(buttonLeftValueLabel, "OFF");
    // lv_obj_align(buttonLeftValueLabel, LV_ALIGN_BOTTOM_LEFT, 0, 8);
}

/**
 * @brief ESC button click handler.
 *
 * Publishes UI_INPUT_ESC_REQUEST.  The App Layer responds by sending
 * CAN_TX_CMD_SEND_ESC_REQUEST using the last-known drive mode.
 *
 * Can be pressed without having first confirmed a mission; in that case the
 * CAN module uses drive mode 0 (MISSION_NONE).
 */
static void btn_left_event_cb(lv_event_t *e)
{
    bool on = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
    lv_subject_set_int(&ui_tx_subj_pwrlimit_setting, on ? 1 : 0);

}

/**
 * @brief OK button click handler.
 *
 * Reads the current roller selection and publishes UI_INPUT_MISSION_SELECTED.
 * The App Layer responds by updating the mission state and sending the
 * selected mission over CAN (CAN_TX_CMD_SEND_MISSION).
 *
 * No CAN frame is sent here — this screen only raises the intent.
 */
static void btn_right_event_cb(lv_event_t *e)
{
    bool on = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);

    struct ui_input_event evt = {
        .type = on ? UI_INPUT_TORQUE_VECT_ON : UI_INPUT_TORQUE_VECT_OFF,
    };

    int ret = zbus_chan_pub(&ui_input_chan, &evt, K_NO_WAIT);
    if (ret != 0) {
        LOG_WRN("UI_INPUT_TORQUE_VECT publish failed: %d", ret);
    } else {
        lv_subject_set_int(&ui_tx_subj_torquevect_setting, on ? 1 : 0);
        LOG_DBG("Torque Vectoring toggled: %s", on ? "ON" : "OFF");
    }
}




/* ── Public Function Implementations ─────────────────────────────────────────────────────────── */

lv_obj_t *screen_ev_driving_create(lv_subject_t *status_subjects)
{
    /* ── Screen base ─────────────────────────────────────────────────────── */

    if (!s_subjects_init) {
        lv_subject_init_int(&s_sldr_left_val, 0);
        lv_subject_init_int(&s_sldr_right_val, 0);
        s_subjects_init = true;
    }

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_add_style(scr, &ui_style_screen, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* ── Widgets ─────────────────────────────────────────────────────────── */

    ui_header_create(scr, "EV DRIVING", status_subjects);
    build_sliders(scr);
    build_labels(scr);
    build_buttons(scr);

    s_right_encoder_group = lv_group_create();
    lv_group_add_obj(s_right_encoder_group, s_sldr_right);
    lv_group_set_editing(s_right_encoder_group, true);

    s_left_button_group = lv_group_create();
    lv_group_add_obj(s_left_button_group, s_btn_left);
    lv_group_set_editing(s_left_button_group, true);

    s_right_button_group = lv_group_create();
    lv_group_add_obj(s_right_button_group, s_btn_right);
    lv_group_set_editing(s_right_button_group, true);

    return scr;
}

lv_group_t *screen_ev_driving_get_right_encoder_group(void)
{
    return s_right_encoder_group;
}

lv_group_t *screen_ev_driving_get_left_button_group(void)
{
    return s_left_button_group;
}

lv_group_t *screen_ev_driving_get_right_button_group(void)
{
    return s_right_button_group;
}

