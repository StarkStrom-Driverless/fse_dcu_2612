/**
 * @file        screen_mission_select.c
 * @brief       Mission selection screen implementation
 *
 * @details     Builds the mission selection screen with:
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
 * @date        Created: 2026-06-02
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
 * 0.1.0    2026-06-02  Mario Wegmann   Initial creation
 * ─────────────────────────────────────────────────────────────────────────────────────────────────
 */

/* ── Corresponding Header ────────────────────────────────────────────────────────────────────── */

#include "modules/ui/screen_mission_select.h"

/* ── Zephyr Includes ─────────────────────────────────────────────────────────────────────────── */

#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/logging/log.h>

/* ── Project Includes ────────────────────────────────────────────────────────────────────────── */

#include "modules/ui/ui_styles.h"
#include "services/event_bus/event_bus.h"
#include "services/event_bus/events.h"

/* ── Zephyr Logging ──────────────────────────────────────────────────────────────────────────── */

LOG_MODULE_REGISTER(screen_mission_select, CONFIG_LOG_DEFAULT_LEVEL);


/* ── Private Macros & Constants ──────────────────────────────────────────────────────────────── */

/**
 * @brief Roller option string.
 *
 * Index maps directly to enum mission_id (both start at 0):
 *   0 → MISSION_NONE          "---"
 *   1 → MISSION_ACCELERATION
 *   2 → MISSION_SKIDPAD
 *   3 → MISSION_AUTOCROSS
 *   4 → MISSION_ENDURANCE
 *   5 → MISSION_INSPECTION
 *   6 → MISSION_MANUAL_DRIVING
 */
#define ROLLER_OPTIONS          \
    "None\n"                    \
    "Acceleration\n"            \
    "Skidpad\n"                 \
    "Trackdrive\n"              \
    "Braketest\n"               \
    "Inspection\n"              \
    "Autocross\n"               \
    "Manual Driving"            \

/** @brief Number of roller rows visible simultaneously (one above/below the selection). */
#define ROLLER_VISIBLE_ROWS     3U

/** @brief Width of the roller widget in pixels. */
#define ROLLER_WIDTH            220

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

/** @brief Mission roller — user scrolls with the right encoder. */
static lv_obj_t   *s_roller;

/** @brief OK button — confirms the roller selection (sends mission over CAN). */
static lv_obj_t   *s_btn_ok;

/** @brief RTD button — requests Ready-to-Drive with the last-known drive mode. */
static lv_obj_t   *s_btn_rtd;

/** @brief LVGL input group for the right encoder. */
static lv_group_t *s_right_encoder_group;

/** @brief LVGL input group for the right encoder. */
static lv_group_t *s_left_button_group;

/** @brief LVGL input group for the right encoder. */
static lv_group_t *s_right_button_group;


/* ── Private Function Prototypes ─────────────────────────────────────────────────────────────── */

static void build_header(lv_obj_t *scr);
static void build_roller(lv_obj_t *scr);
static void build_buttons(lv_obj_t *scr);
static void btn_ok_event_cb(lv_event_t *e);
static void btn_rtd_event_cb(lv_event_t *e);


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
    lv_obj_set_style_text_color(title, UI_C_WHITE, 0);
    lv_label_set_text(title, "MISSION");
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 10, 0);
}

static void build_roller(lv_obj_t *scr)
{
    s_roller = lv_roller_create(scr);

    lv_roller_set_options(s_roller, ROLLER_OPTIONS, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(s_roller, ROLLER_VISIBLE_ROWS);
    lv_roller_set_selected(s_roller, 0, LV_ANIM_OFF); /* default: MISSION_NONE */

    lv_obj_set_width(s_roller, ROLLER_WIDTH);

    /* ── Main part: all items ──────────────────────────────────────────── */
    lv_obj_set_style_bg_color(s_roller,     UI_C_WHITE,                    LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_roller,       LV_OPA_COVER,                  LV_PART_MAIN);
    lv_obj_set_style_text_font(s_roller,    &BarlowCondensed_BoldItalic_18, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_roller,   UI_C_DARK,                     LV_PART_MAIN);
    lv_obj_set_style_border_width(s_roller, 2,                             LV_PART_MAIN);
    lv_obj_set_style_border_color(s_roller, UI_C_DARK,                     LV_PART_MAIN);
    lv_obj_set_style_radius(s_roller,       0,                             LV_PART_MAIN);

    /* ── Selected part: centre row highlight ───────────────────────────── */
    lv_obj_set_style_bg_color(s_roller,   UI_C_ACCENT,                    LV_PART_SELECTED);
    lv_obj_set_style_bg_opa(s_roller,     LV_OPA_COVER,                   LV_PART_SELECTED);
    lv_obj_set_style_text_font(s_roller,  &BarlowCondensed_BoldItalic_18,  LV_PART_SELECTED);
    lv_obj_set_style_text_color(s_roller, UI_C_DARK,                       LV_PART_SELECTED);

    /* Vertically centred in the content area below the 15 % header. */
    lv_obj_align(s_roller, LV_ALIGN_CENTER, 0, -30);
}

static void build_buttons(lv_obj_t *scr)
{
    /* ── OK button ─────────────────────────────────────────────────────── */

    // s_btn_ok = lv_button_create(scr);
    // lv_obj_remove_style_all(s_btn_ok);
    // lv_obj_add_style(s_btn_ok, &ui_style_btn_default, 0);
    // lv_obj_add_style(s_btn_ok, &ui_style_btn_checked, LV_STATE_CHECKED);
    // lv_obj_add_style(s_btn_ok, &ui_style_btn_focused, LV_STATE_FOCUS_KEY);
    // lv_obj_set_size(s_btn_ok, BTN_WIDTH, BTN_HEIGHT);
    // lv_obj_align(s_btn_ok, LV_ALIGN_BOTTOM_MID, -BTN_HALF_SPACING, -BTN_BOTTOM_MARGIN);

    // lv_obj_t *lbl_ok = lv_label_create(s_btn_ok);
    // lv_obj_add_style(lbl_ok, &ui_style_label_subtitle, 0);
    // lv_label_set_text(lbl_ok, "OK");
    // lv_obj_align(lbl_ok, LV_ALIGN_CENTER, 0, 0);

    // lv_obj_add_event_cb(s_btn_ok, btn_ok_event_cb, LV_EVENT_CLICKED, NULL);

    /* ── RTD button ────────────────────────────────────────────────────── */

    s_btn_rtd = lv_button_create(scr);
    lv_obj_remove_style_all(s_btn_rtd);
    lv_obj_add_style(s_btn_rtd, &ui_style_btn_default, 0);
    lv_obj_add_style(s_btn_rtd, &ui_style_btn_checked, LV_STATE_CHECKED);
    lv_obj_add_style(s_btn_rtd, &ui_style_btn_focused, LV_STATE_FOCUS_KEY);
    lv_obj_set_size(s_btn_rtd, BTN_WIDTH, BTN_HEIGHT);
    lv_obj_align(s_btn_rtd, LV_ALIGN_BOTTOM_MID, BTN_HALF_SPACING, -BTN_BOTTOM_MARGIN);

    lv_obj_t *lbl_rtd = lv_label_create(s_btn_rtd);
    lv_obj_add_style(lbl_rtd, &ui_style_label_subtitle, 0);
    lv_label_set_text(lbl_rtd, "SELECT");
    lv_obj_align(lbl_rtd, LV_ALIGN_CENTER, 0, 0);

    lv_obj_add_flag(s_btn_rtd, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_add_event_cb(s_btn_rtd, btn_rtd_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
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
    ARG_UNUSED(e);

    uint16_t idx = lv_roller_get_selected(s_roller);

    struct ui_input_event evt = {
        .type         = UI_INPUT_MISSION_SELECTED,
        .data.mission = (enum mission_id)idx,
    };

    int ret = zbus_chan_pub(&ui_input_chan, &evt, K_NO_WAIT);
    if (ret != 0) {
        LOG_WRN("UI_INPUT_MISSION_SELECTED publish failed (mission=%u): %d",
                (unsigned)idx, ret);
    } else {
        LOG_DBG("Mission selected: %u", (unsigned)idx);
    }
}

/**
 * @brief RTD button click handler.
 *
 * Publishes UI_INPUT_RTD_REQUEST.  The App Layer responds by sending
 * CAN_TX_CMD_SEND_RTD_REQUEST using the last-known drive mode.
 *
 * Can be pressed without having first confirmed a mission; in that case the
 * CAN module uses drive mode 0 (MISSION_NONE).
 */
static void btn_rtd_event_cb(lv_event_t *e)
{
    lv_obj_t * button = lv_event_get_target_obj(e);
    uint16_t idx = lv_roller_get_selected(s_roller);

    struct ui_input_event evt = {
        .type         = UI_INPUT_MISSION_SELECTED,
        .data.mission = (enum mission_id)idx,
    };

    if (lv_obj_has_state(button, LV_STATE_CHECKED) == true) {
        int ret = zbus_chan_pub(&ui_input_chan, &evt, K_NO_WAIT);
        if (ret != 0) {
            LOG_WRN("UI_INPUT_MISSION_SELECTED publish failed (mission=%u): %d",
                    (unsigned)idx, ret);
        } else {
            LOG_DBG("Mission selected: %u", (unsigned)idx);
            LOG_INF("Mission selected: %u", (unsigned)idx);
        }
    } 
}


/* ── Public Function Implementations ─────────────────────────────────────────────────────────── */

lv_obj_t *screen_mission_select_create(void)
{
    /* ── Screen base ─────────────────────────────────────────────────────── */

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_add_style(scr, &ui_style_screen, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* ── Widgets ─────────────────────────────────────────────────────────── */

    build_header(scr);
    build_roller(scr);
    build_buttons(scr);

    /* ── Input group (right encoder) ─────────────────────────────────────── */

    /*
     * Tab order: roller → OK → RTD.
     *
     * ui.c assigns this group to the right encoder indev on screen entry:
     *   lv_indev_set_group(right_encoder_indev, screen_mission_select_get_group())
     * and removes it on screen leave:
     *   lv_indev_set_group(right_encoder_indev, NULL)
     */
    s_right_encoder_group = lv_group_create();
    lv_group_add_obj(s_right_encoder_group, s_roller);
    lv_group_set_editing(s_right_encoder_group, true);

    s_right_button_group = lv_group_create();
    lv_group_add_obj(s_right_button_group, s_btn_rtd);
    lv_group_set_editing(s_right_button_group, true);

    return scr;
}

lv_group_t *screen_mission_select_get_right_encoder_group(void)
{
    return s_right_encoder_group;
}

lv_group_t *screen_mission_select_get_left_button_group(void)
{
    return s_left_button_group;
}

lv_group_t *screen_mission_select_get_right_button_group(void)
{
    return s_right_button_group;
}
