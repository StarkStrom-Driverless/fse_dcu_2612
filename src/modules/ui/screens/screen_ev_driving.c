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
#include "modules/ui/widgets/ui_unit_label.h"
#include "services/event_bus/event_bus.h"
#include "services/event_bus/events.h"
#include "generated/ui_subjects_gen.h"

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
#define BTN_HALF_SPACING        55

/** @brief Bottom margin for the button row (pixels from screen bottom). */
#define BTN_BOTTOM_MARGIN       20


/* ── Private Variables ───────────────────────────────────────────────────────────────────────── */

/** @brief Left slider */
static lv_obj_t   *s_sldr_left;

/** @brief Right slider */
static lv_obj_t   *s_sldr_right;


/** @brief OK button */
static lv_obj_t   *s_btn_ok;

/** @brief RTD button — requests Ready-to-Drive with the last-known drive mode. */
// static lv_obj_t   *s_btn_esc;

/** @brief LVGL input group for the right encoder. */
static lv_group_t *s_right_encoder_group;

/** @brief LVGL input group for the left encoder. */
static lv_group_t *s_left_button_group;

/** @brief LVGL input group for the right button. */
static lv_group_t *s_right_button_group;


/* ── Private Function Prototypes ─────────────────────────────────────────────────────────────── */

static void build_header(lv_obj_t *scr);
static void build_sliders(lv_obj_t *scr);
static void build_buttons(lv_obj_t *scr);
static void btn_ok_event_cb(lv_event_t *e);
// static void btn_esc_event_cb(lv_event_t *e);


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
    lv_label_set_text(title, "EV DRIVING");
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 10, 0);
}


static void build_sliders(lv_obj_t *scr)
{
    s_sldr_left = lv_slider_create(scr);
    lv_obj_remove_style_all(s_sldr_left);
    lv_obj_add_style(s_sldr_left, &ui_style_slider_main, LV_PART_MAIN);
    lv_obj_add_style(s_sldr_left, &ui_style_slider_indicator, LV_PART_INDICATOR);
    lv_obj_set_size(s_sldr_left, 40, 200);
    lv_obj_set_pos(s_sldr_left, 10, 70);

    lv_obj_t *s_lbl_sldr_left_title = lv_label_create(scr);
    lv_obj_add_style(s_lbl_sldr_left_title, &ui_style_label_subtitle, 0);
    lv_label_set_text(s_lbl_sldr_left_title, "TQG F");
    lv_obj_align_to(s_lbl_sldr_left_title, s_sldr_left, LV_ALIGN_OUT_TOP_MID, 0, 0);

    
}

static void build_labels(lv_obj_t *scr)
{
    lv_obj_t *lbl_mean_power = lv_label_create(scr);
    lv_obj_add_style(lbl_mean_power, &ui_style_label_value_lg, 0);
    lv_obj_add_style(lbl_mean_power, &ui_style_level_warn, UI_STATE_WARN);
    lv_obj_add_style(lbl_mean_power, &ui_style_level_crit, UI_STATE_CRIT);
    lv_label_bind_text(lbl_mean_power, &ui_subj_power_average, "%d");
    lv_obj_bind_state_if_gt(lbl_mean_power, &ui_subj_power_average, UI_STATE_WARN, UI_POWER_AVERAGE_WARN_HIGH);
    lv_obj_bind_state_if_gt(lbl_mean_power, &ui_subj_power_average, UI_STATE_CRIT, UI_POWER_AVERAGE_CRIT_HIGH);
    lv_obj_align(lbl_mean_power, LV_ALIGN_LEFT_MID, 10, 0);

    lv_obj_t *lbl_hv_volt_akku = ui_unit_label_create(scr,
                                      &BarlowCondensed_BoldItalic_100,
                                      &BarlowCondensed_Italic_44, "V");
    lv_obj_add_style(lbl_hv_volt_akku, &ui_style_level_warn, UI_STATE_WARN);
    lv_obj_add_style(lbl_hv_volt_akku, &ui_style_level_crit, UI_STATE_CRIT);
    ui_unit_label_bind_value(lbl_hv_volt_akku, &ui_subj_voltage_accu_hv, "%d");
    lv_obj_bind_state_if_lt(lbl_hv_volt_akku, &ui_subj_voltage_accu_hv, UI_STATE_WARN, UI_VOLTAGE_ACCU_HV_WARN_LOW);
    lv_obj_bind_state_if_lt(lbl_hv_volt_akku, &ui_subj_voltage_accu_hv, UI_STATE_CRIT, UI_VOLTAGE_ACCU_HV_CRIT_LOW);
    lv_obj_align(lbl_hv_volt_akku, LV_ALIGN_LEFT_MID, 150, 0);

    lv_obj_t *lbl_hv_volt_ts = lv_label_create(scr);
    lv_obj_add_style(lbl_hv_volt_ts, &ui_style_label_value_lg, 0);
    lv_obj_set_style_text_color(lbl_hv_volt_ts, UI_C_DARK, 0);
    lv_label_bind_text(lbl_hv_volt_ts, &ui_subj_voltage_tractive_system, "%d");
    lv_obj_align(lbl_hv_volt_ts, LV_ALIGN_LEFT_MID, 300, 0);
}

static void build_buttons(lv_obj_t *scr)
{
    /* ── OK button ─────────────────────────────────────────────────────── */

    s_btn_ok = lv_button_create(scr);
    lv_obj_remove_style_all(s_btn_ok);
    lv_obj_add_style(s_btn_ok, &ui_style_btn_default, 0);
    lv_obj_add_style(s_btn_ok, &ui_style_btn_checked, LV_STATE_PRESSED);
    lv_obj_set_size(s_btn_ok, BTN_WIDTH, BTN_HEIGHT);
    lv_obj_align(s_btn_ok, LV_ALIGN_BOTTOM_MID, BTN_HALF_SPACING, -BTN_BOTTOM_MARGIN);

    lv_obj_t *lbl_ok = lv_label_create(s_btn_ok);
    lv_obj_add_style(lbl_ok, &ui_style_label_subtitle, 0);
    lv_label_set_text(lbl_ok, "SET MISSION");
    lv_obj_align(lbl_ok, LV_ALIGN_CENTER, 0, 0);

    lv_obj_add_event_cb(s_btn_ok, btn_ok_event_cb, LV_EVENT_CLICKED, NULL);

    /* ── ESC button ────────────────────────────────────────────────────── */

    // s_btn_esc = lv_button_create(scr);
    // lv_obj_remove_style_all(s_btn_esc);
    // lv_obj_add_style(s_btn_esc, &ui_style_btn_default, 0);
    // lv_obj_add_style(s_btn_esc, &ui_style_btn_checked, LV_STATE_PRESSED);
    // lv_obj_add_style(s_btn_esc, &ui_style_btn_focused, LV_STATE_FOCUS_KEY);
    // lv_obj_set_size(s_btn_esc, BTN_WIDTH, BTN_HEIGHT);
    // lv_obj_align(s_btn_esc, LV_ALIGN_BOTTOM_MID, -BTN_HALF_SPACING, -BTN_BOTTOM_MARGIN);

    // lv_obj_t *lbl_esc = lv_label_create(s_btn_esc);
    // lv_obj_add_style(lbl_esc, &ui_style_label_subtitle, 0);
    // lv_label_set_text(lbl_esc, "UNSET MISSION");
    // lv_obj_align(lbl_esc, LV_ALIGN_CENTER, 0, 0);

    // lv_obj_add_flag(s_btn_esc, LV_OBJ_FLAG_CHECKABLE);
    // lv_obj_add_event_cb(s_btn_esc, btn_esc_event_cb, LV_EVENT_CLICKED, NULL);
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
static void btn_ok_event_cb(lv_event_t *e)
{
    // uint16_t  idx    = lv_roller_get_selected(s_roller);
    // char buf[32];

    // lv_roller_get_selected_str(s_roller, buf, sizeof(buf));

    // struct ui_input_event evt = {
    //     .type         = UI_INPUT_MISSION_SELECTED,
    //     .data.mission = (enum mission_id)idx,
    // };

    // int ret = zbus_chan_pub(&ui_input_chan, &evt, K_NO_WAIT);
    // if (ret != 0) {
    //     LOG_WRN("UI_INPUT_MISSION_SELECTED publish failed (mission=%u): %d",
    //             (unsigned)idx, ret);
    // } else {
    //     lv_label_set_text_fmt(s_roller_lbl, "Current Mission %s", buf);
    //     LOG_DBG("Mission selected: %u", (unsigned)idx);
    // }
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
// static void btn_esc_event_cb(lv_event_t *e)
// {
//     uint16_t  idx    = lv_roller_get_selected(s_roller);
//     char buf[32];

//     lv_roller_get_selected_str(s_roller, buf, sizeof(buf));

//     struct ui_input_event evt = {
//         .type         = UI_INPUT_MISSION_SELECTED,
//         .data.mission = (enum mission_id)idx,
//     };

//     int ret = zbus_chan_pub(&ui_input_chan, &evt, K_NO_WAIT);
//     if (ret != 0) {
//         LOG_WRN("UI_INPUT_MISSION_SELECTED publish failed (mission=%u): %d",
//                 (unsigned)idx, ret);
//     } else {
//         lv_label_set_text_fmt(s_roller_lbl, "Selected Mission %s", buf);
//         LOG_DBG("Mission selected: %u", (unsigned)idx);
//     }
// }


/* ── Public Function Implementations ─────────────────────────────────────────────────────────── */

lv_obj_t *screen_ev_driving_create(void)
{
    /* ── Screen base ─────────────────────────────────────────────────────── */

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_add_style(scr, &ui_style_screen, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* ── Widgets ─────────────────────────────────────────────────────────── */

    build_header(scr);
    build_sliders(scr);
    build_labels(scr);
    build_buttons(scr);

    /* ── Input group (right encoder) ─────────────────────────────────────── */

    /*
     * Tab order: roller → OK → RTD.
     *
     * ui.c assigns this group to the right encoder indev on screen entry:
     *   lv_indev_set_group(right_encoder_indev, screen_ev_driving_get_group())
     * and removes it on screen leave:
     *   lv_indev_set_group(right_encoder_indev, NULL)
     */
    // s_right_encoder_group = lv_group_create();
    // lv_group_add_obj(s_right_encoder_group, s_roller);
    // lv_group_set_editing(s_right_encoder_group, true);

    s_right_button_group = lv_group_create();
    lv_group_add_obj(s_right_button_group, s_btn_ok);
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
