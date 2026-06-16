/**
 * @file        screen_checklist.c
 * @brief       Pre-RTD checklist screen implementation
 *
 * @details     Builds the pre-RTD checklist screen with:
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
 * @date        Created: 2026-06-08
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
 * 0.1.0    2026-06-08  Mario Wegmann   Initial creation
 * ─────────────────────────────────────────────────────────────────────────────────────────────────
 */

/* ── Corresponding Header ────────────────────────────────────────────────────────────────────── */

#include "modules/ui/screens/screen_checklist.h"

/* ── Zephyr Includes ─────────────────────────────────────────────────────────────────────────── */

#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/logging/log.h>

/* ── Project Includes ────────────────────────────────────────────────────────────────────────── */

#include "modules/ui/ui_styles.h"
#include "services/event_bus/event_bus.h"
#include "services/event_bus/events.h"

/* ── Zephyr Logging ──────────────────────────────────────────────────────────────────────────── */

LOG_MODULE_REGISTER(screen_checklist, CONFIG_LOG_DEFAULT_LEVEL);


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
static void build_buttons(lv_obj_t *scr);
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
    lv_label_set_text(title, "PRE RTD");
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 10, 0);
}

static void build_buttons(lv_obj_t *scr)
{

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
    lv_label_set_text(lbl_rtd, "RTD");
    lv_obj_align(lbl_rtd, LV_ALIGN_CENTER, 0, 0);

    lv_obj_add_flag(s_btn_rtd, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_add_event_cb(s_btn_rtd, btn_rtd_event_cb, LV_EVENT_CLICKED, NULL);
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

    struct ui_input_event evt = {
        .type = UI_INPUT_RTD_REQUEST,
    };

    if (lv_obj_has_state(button, LV_STATE_CHECKED) == true) {
        int ret = zbus_chan_pub(&ui_input_chan, &evt, K_NO_WAIT);
        if (ret != 0) {
            LOG_WRN("UI_INPUT_RTD_REQUEST publish failed: %d", ret);
        } else {
            LOG_DBG("RTD requested");
        }
    } 
}


/* ── Public Function Implementations ─────────────────────────────────────────────────────────── */

lv_obj_t *screen_checklist_create(void)
{
    /* ── Screen base ─────────────────────────────────────────────────────── */

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_add_style(scr, &ui_style_screen, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* ── Widgets ─────────────────────────────────────────────────────────── */

    build_header(scr);
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
    lv_group_set_editing(s_right_encoder_group, true);

    s_right_button_group = lv_group_create();
    lv_group_add_obj(s_right_button_group, s_btn_rtd);
    lv_group_set_editing(s_right_button_group, true);

    return scr;
}

lv_group_t *screen_checklist_get_right_encoder_group(void)
{
    return s_right_encoder_group;
}

lv_group_t *screen_checklist_get_left_button_group(void)
{
    return s_left_button_group;
}

lv_group_t *screen_checklist_get_right_button_group(void)
{
    return s_right_button_group;
}

