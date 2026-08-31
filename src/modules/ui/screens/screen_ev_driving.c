/**
 * @file        screen_ev_driving.c
 * @brief       EV driving screen implementation
 *
 * @ingroup     dcu_ui_screens
 *
 * @details     Builds the EV driving screen with:
 *
 *                – Two sliders   : torque gain front (left edge) and rear
 *                                  (right edge). Only the rear one is
 *                                  reachable, through the right encoder.
 *
 *                – One bar       : HV accumulator voltage along the bottom.
 *                                  A bar rather than a slider because the
 *                                  value comes from the vehicle — there is
 *                                  nothing for the driver to set.
 *
 *                – Three readouts: HV accumulator, inverter and motor
 *                                  temperature, bound to their generated
 *                                  subjects through the ui_quantity widget.
 *
 *                – PWR Limit     : checkable button mirroring
 *                                  ui_tx_subj_pwrlimit_setting.
 *
 *                – TQ Vect       : checkable button mirroring
 *                                  ui_tx_subj_torquevect_setting; also
 *                                  publishes UI_INPUT_TORQUE_VECT_ON/_OFF.
 *
 *              ### Buttons follow the value, not the press
 *              Each button observes its TX subject, and the click handler only
 *              writes to that subject. The visual state therefore reflects the
 *              stored value rather than the last press — the same pattern the
 *              other screens use for their confirmed-value labels.
 *
 *              ### What is not wired up
 *              The torque-gain sliders keep their positions across visits, in
 *              s_sldr_left_val and s_sldr_right_val, but nothing reads those
 *              subjects: the values reach neither a setting nor a CAN signal.
 *              The left slider is in no input group at all, so it cannot be
 *              moved. Both are placeholders for the torque-gain settings.
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
#include "modules/ui/widgets/ui_quantity.h"
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
 * @brief Half the centre-to-centre distance between the two buttons, in pixels.
 *
 * The buttons are placed at -BTN_HALF_SPACING and +BTN_HALF_SPACING from the
 * screen centre, so they sit 2 × 100 = 200 px apart centre to centre, leaving
 * a 100 px gap between two BTN_WIDTH-wide buttons.
 */
#define BTN_HALF_SPACING        100

/** @brief Bottom margin for the button row (pixels from screen bottom). */
#define BTN_BOTTOM_MARGIN       0


/* ── Private Variables ───────────────────────────────────────────────────────────────────────── */

/**
 * @brief Torque-gain slider positions — survive screen destroy/recreate.
 *
 * Initialised once, guarded by s_subjects_init, so the sliders come back where
 * the driver left them. Nothing outside this file reads them yet.
 */
static lv_subject_t s_sldr_left_val;  /**< TQG F slider value. */
static lv_subject_t s_sldr_right_val; /**< TQG R slider value. */

/** @brief Guard so the two subjects above are initialised exactly once. */
static bool         s_subjects_init;

/** @brief Torque gain front — vertical slider at the left edge. Not reachable. */
static lv_obj_t   *s_sldr_left;

/** @brief Torque gain rear — vertical slider at the right edge, on the encoder. */
static lv_obj_t   *s_sldr_right;

/**
 * @brief HV accumulator voltage — horizontal bar across the bottom.
 *
 * An lv_bar, not an lv_slider: the value comes from the vehicle and there is
 * nothing here for the driver to set. A bar has no knob and no input handling,
 * so the widget cannot be dragged or focused even by accident.
 */
static lv_obj_t   *s_bar_middle;

/** @brief TQ Vect button — toggles torque vectoring. */
static lv_obj_t   *s_btn_right;

/** @brief "ON"/"OFF" text inside the TQ Vect button. */
static lv_obj_t   *s_lbl_btn_right_value;

/** @brief PWR Limit button — toggles the power limit. */
static lv_obj_t   *s_btn_left;

/** @brief Reserved: value text for the PWR Limit button. Never created. */
static lv_obj_t   *s_lbl_btn_left_value;

/** @brief Input group for the right encoder — holds the TQG R slider. */
static lv_group_t *s_right_encoder_group;

/** @brief Input group for the left button pad — holds the PWR Limit button. */
static lv_group_t *s_left_button_group;

/** @brief Input group for the right button pad — holds the TQ Vect button. */
static lv_group_t *s_right_button_group;


/* ── Private Function Prototypes ─────────────────────────────────────────────────────────────── */
static void build_sliders(lv_obj_t *scr);
static void build_buttons(lv_obj_t *scr);
static void btn_left_event_cb(lv_event_t *e);
static void btn_right_event_cb(lv_event_t *e);
static void tq_vect_observer_cb(lv_observer_t *observer, lv_subject_t *subject);
static void pwr_limit_observer_cb(lv_observer_t *observer, lv_subject_t *subject);


/* ── Private Function Implementations ───────────────────────────────────────────────────────── */

/**
 * @brief Mirror the TQG F slider into its subject so it survives a rebuild.
 * @param e  LV_EVENT_VALUE_CHANGED from the slider.
 */
static void sldr_left_value_changed_cb(lv_event_t *e)
{
    lv_subject_set_int(&s_sldr_left_val,
                       lv_slider_get_value(lv_event_get_target_obj(e)));
}

/**
 * @brief Mirror the TQG R slider into its subject so it survives a rebuild.
 * @param e  LV_EVENT_VALUE_CHANGED from the slider.
 */
static void sldr_right_value_changed_cb(lv_event_t *e)
{
    lv_subject_set_int(&s_sldr_right_val,
                       lv_slider_get_value(lv_event_get_target_obj(e)));
}

/**
 * @brief Apply the stored torque-vectoring setting to the TQ Vect button.
 *
 * Sets the button's checked state and its ON/OFF caption, recoloring the
 * caption because the checked style paints the button green and dark text
 * would disappear on it.
 *
 * @param observer  Observer whose target object is the button.
 * @param subject   ui_tx_subj_torquevect_setting.
 */
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

/**
 * @brief Apply the stored power-limit setting to the PWR Limit button.
 *
 * State only — the button has no value caption.
 *
 * @param observer  Observer whose target object is the button.
 * @param subject   ui_tx_subj_pwrlimit_setting.
 */
static void pwr_limit_observer_cb(lv_observer_t *observer, lv_subject_t *subject)
{
    lv_obj_t *btn = lv_observer_get_target_obj(observer);
    if (lv_subject_get_int(subject)) {
        lv_obj_add_state(btn, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(btn, LV_STATE_CHECKED);
    }
}

/**
 * @brief Build the two torque-gain sliders, the HV bar and its readout.
 *
 * The sliders are restored from their subjects. The bar is bound straight to
 * the received signal and needs no state of its own — nothing can change it
 * but the vehicle.
 *
 * @param scr  Screen object to build into.
 */
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

    /* ── Bar Middle — HV accumulator, display only ──────────────────────── */

    /*
     * Styled with the shared slider styles like every other bar in the tree:
     * ui_style_slider_main paints the track, ui_style_slider_indicator the
     * fill. The names are about the visual role, not the widget type.
     */
    s_bar_middle = lv_bar_create(scr);
    lv_obj_remove_style_all(s_bar_middle);
    lv_obj_add_style(s_bar_middle, &ui_style_slider_main, LV_PART_MAIN);
    lv_obj_add_style(s_bar_middle, &ui_style_slider_indicator, LV_PART_INDICATOR);
    lv_obj_set_size(s_bar_middle, 300, 20);
    lv_bar_set_range(s_bar_middle, 0, 500);
    lv_bar_bind_value(s_bar_middle, &ui_subj_voltage_accu_hv);
    lv_obj_align(s_bar_middle, LV_ALIGN_BOTTOM_MID, 0, -60);

    lv_obj_t *lbl_bar_middle_title = lv_label_create(scr);
    lv_obj_add_style(lbl_bar_middle_title, &ui_style_label_subtitle, 0);
    lv_label_set_text(lbl_bar_middle_title, "HV SoC");
    lv_obj_align_to(lbl_bar_middle_title, s_bar_middle, LV_ALIGN_OUT_TOP_LEFT, 0, 0);

    lv_obj_t *lbl_bar_middle_value = ui_quantity_create(scr,
                                      &BarlowCondensed_BoldItalic_32,
                                      &BarlowCondensed_Italic_20, "V");
    lv_obj_add_style(lbl_bar_middle_value, &ui_style_level_warn, UI_STATE_WARN);
    lv_obj_add_style(lbl_bar_middle_value, &ui_style_level_crit, UI_STATE_CRIT);
    lv_obj_bind_state_if_lt(lbl_bar_middle_value, &ui_subj_voltage_accu_hv, UI_STATE_WARN, UI_VOLTAGE_ACCU_HV_WARN_LOW);
    lv_obj_bind_state_if_lt(lbl_bar_middle_value, &ui_subj_voltage_accu_hv, UI_STATE_CRIT, UI_VOLTAGE_ACCU_HV_CRIT_LOW);
    lv_obj_set_size(lbl_bar_middle_value, 60, 30);
    ui_quantity_bind_value(lbl_bar_middle_value, &ui_subj_voltage_accu_hv, "%d");
    lv_obj_align_to(lbl_bar_middle_value, s_bar_middle, LV_ALIGN_OUT_TOP_RIGHT, 0, 0);
    
}

/**
 * @brief Build the three temperature readouts across the middle of the screen.
 *
 * Each is a ui_quantity bound to its generated subject. The level styles
 * are attached but the state bindings are commented out, so none of the three
 * changes color yet — the thresholds they were bound to belonged to the HV
 * voltage signal, not to a temperature.
 *
 * @param scr  Screen object to build into.
 */
static void build_labels(lv_obj_t *scr)
{
    /* ── Label HV Accu Temp ─────────────────────────────────────────────── */
    lv_obj_t *lbl_temp_hv_accu_value = ui_quantity_create(scr,
                                      &BarlowCondensed_BoldItalic_80,
                                      &BarlowCondensed_Italic_44, "°C");
    lv_obj_add_style(lbl_temp_hv_accu_value, &ui_style_level_warn, UI_STATE_WARN);
    lv_obj_add_style(lbl_temp_hv_accu_value, &ui_style_level_crit, UI_STATE_CRIT);
    // lv_obj_bind_state_if_lt(lbl_temp_hv_accu_value, &ui_subj_voltage_accu_hv, UI_STATE_WARN, UI_VOLTAGE_ACCU_HV_WARN_LOW);
    // lv_obj_bind_state_if_lt(lbl_temp_hv_accu_value, &ui_subj_voltage_accu_hv, UI_STATE_CRIT, UI_VOLTAGE_ACCU_HV_CRIT_LOW);
    // lv_obj_set_size(lbl_temp_hv_accu_value, 60, 30);
    ui_quantity_bind_value(lbl_temp_hv_accu_value, &ui_subj_temperature_accu_hv, "%d");
    // lv_obj_align_to(lbl_temp_hv_accu_value, s_bar_middle, LV_ALIGN_OUT_TOP_RIGHT, 0, 0);
    lv_obj_set_pos(lbl_temp_hv_accu_value, 65, 100);

    lv_obj_t *lbl_temp_hv_accu_title = lv_label_create(scr);
    lv_obj_add_style(lbl_temp_hv_accu_title, &ui_style_label_subtitle, 0);
    lv_label_set_text(lbl_temp_hv_accu_title, "HV Accu Temp");
    lv_obj_align_to(lbl_temp_hv_accu_title, lbl_temp_hv_accu_value, LV_ALIGN_OUT_TOP_MID, 0, 0);
    
    /* ── Label Inverter Temp ────────────────────────────────────────────── */

    lv_obj_t *lbl_temp_inverter_value = ui_quantity_create(scr,
                                      &BarlowCondensed_BoldItalic_80,
                                      &BarlowCondensed_Italic_44, "°C");
    lv_obj_add_style(lbl_temp_inverter_value, &ui_style_level_warn, UI_STATE_WARN);
    lv_obj_add_style(lbl_temp_inverter_value, &ui_style_level_crit, UI_STATE_CRIT);
    // lv_obj_bind_state_if_lt(lbl_temp_inverter_value, &ui_subj_voltage_accu_hv, UI_STATE_WARN, UI_VOLTAGE_ACCU_HV_WARN_LOW);
    // lv_obj_bind_state_if_lt(lbl_temp_inverter_value, &ui_subj_voltage_accu_hv, UI_STATE_CRIT, UI_VOLTAGE_ACCU_HV_CRIT_LOW);
    // lv_obj_set_size(lbl_temp_inverter_value, 60, 30);
    ui_quantity_bind_value(lbl_temp_inverter_value, &ui_subj_temperature_inverter, "%d");
    // lv_obj_align_to(lbl_temp_inverter_value, s_bar_middle, LV_ALIGN_OUT_TOP_RIGHT, 0, 0);
    lv_obj_set_pos(lbl_temp_inverter_value, 185, 100);

    lv_obj_t *lbl_temp_inverter_title = lv_label_create(scr);
    lv_obj_add_style(lbl_temp_inverter_title, &ui_style_label_subtitle, 0);
    lv_label_set_text(lbl_temp_inverter_title, "Inverter Temp");
    lv_obj_align_to(lbl_temp_inverter_title, lbl_temp_inverter_value, LV_ALIGN_OUT_TOP_MID, 0, 0); 
    
    /* ── Label Motor Temp ───────────────────────────────────────────────── */

    lv_obj_t *lbl_temp_motor_value = ui_quantity_create(scr,
                                      &BarlowCondensed_BoldItalic_80,
                                      &BarlowCondensed_Italic_44, "°C");
    lv_obj_add_style(lbl_temp_motor_value, &ui_style_level_warn, UI_STATE_WARN);
    lv_obj_add_style(lbl_temp_motor_value, &ui_style_level_crit, UI_STATE_CRIT);
    // lv_obj_bind_state_if_lt(lbl_temp_motor_value, &ui_subj_voltage_accu_hv, UI_STATE_WARN, UI_VOLTAGE_ACCU_HV_WARN_LOW);
    // lv_obj_bind_state_if_lt(lbl_temp_motor_value, &ui_subj_voltage_accu_hv, UI_STATE_CRIT, UI_VOLTAGE_ACCU_HV_CRIT_LOW);
    // lv_obj_set_size(lbl_temp_motor_value, 60, 30);
    ui_quantity_bind_value(lbl_temp_motor_value, &ui_subj_temperature_motor, "%d");
    // lv_obj_align_to(lbl_temp_motor_value, s_bar_middle, LV_ALIGN_OUT_TOP_RIGHT, 0, 0);
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

    // lv_obj_t *lbl_hv_volt_akku = ui_quantity_create(scr,
    //                                   &BarlowCondensed_BoldItalic_100,
    //                                   &BarlowCondensed_Italic_44, "V");
    // lv_obj_add_style(lbl_hv_volt_akku, &ui_style_level_warn, UI_STATE_WARN);
    // lv_obj_add_style(lbl_hv_volt_akku, &ui_style_level_crit, UI_STATE_CRIT);
    // lv_obj_bind_state_if_lt(lbl_hv_volt_akku, &ui_subj_voltage_accu_hv, UI_STATE_WARN, UI_VOLTAGE_ACCU_HV_WARN_LOW);
    // lv_obj_bind_state_if_lt(lbl_hv_volt_akku, &ui_subj_voltage_accu_hv, UI_STATE_CRIT, UI_VOLTAGE_ACCU_HV_CRIT_LOW);
    // ui_quantity_bind_value(lbl_hv_volt_akku, &ui_subj_voltage_accu_hv, "%d");
    // lv_obj_align(lbl_hv_volt_akku, LV_ALIGN_LEFT_MID, 150, 0);

    // lv_obj_t *lbl_hv_volt_ts = lv_label_create(scr);
    // lv_obj_add_style(lbl_hv_volt_ts, &ui_style_label_value_lg, 0);
    // lv_obj_set_style_text_color(lbl_hv_volt_ts, UI_C_DARK, 0);
    // lv_label_bind_text(lbl_hv_volt_ts, &ui_subj_voltage_tractive_system, "%d");
    // lv_obj_align(lbl_hv_volt_ts, LV_ALIGN_LEFT_MID, 300, 0);
}

/**
 * @brief Build the PWR Limit and TQ Vect buttons and subscribe them to their subjects.
 *
 * @param scr  Screen object to build into.
 */
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
 * @brief PWR Limit click handler — store the new power-limit state.
 *
 * Writes the button's checked state to ui_tx_subj_pwrlimit_setting; the
 * observer then applies it back to the button.
 *
 * @note Local only. Unlike the TQ Vect button this publishes no ui_input
 *       event, so the App Layer never learns about it and the value reaches
 *       neither the settings service nor the CAN bus.
 *
 * @param e  LV_EVENT_CLICKED from the button.
 */
static void btn_left_event_cb(lv_event_t *e)
{
    bool on = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
    lv_subject_set_int(&ui_tx_subj_pwrlimit_setting, on ? 1 : 0);

}

/**
 * @brief TQ Vect click handler — publish and store the new torque-vectoring state.
 *
 * Publishes UI_INPUT_TORQUE_VECT_ON or _OFF, then writes the value to
 * ui_tx_subj_torquevect_setting so the button's appearance follows.
 *
 * The subject is only updated once the publish succeeded, so a dropped event
 * cannot leave the button claiming a state the rest of the system does not
 * share.
 *
 * @note The App Layer currently ignores both events, so the setting stays
 *       inside the UI.
 *
 * @param e  LV_EVENT_VALUE_CHANGED from the checkable button.
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

