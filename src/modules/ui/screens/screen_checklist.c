/**
 * @file        screen_checklist.c
 * @brief       Pre-RTD screen implementation — the Ready-to-Drive button
 *
 * @ingroup     dcu_ui_screens
 *
 * @details     Builds the pre-RTD screen. Its only widget is the RTD button;
 *              the checklist the file is named after is still to come.
 *
 *              The button listens for LV_EVENT_LONG_PRESSED and
 *              LV_EVENT_RELEASED, not for LV_EVENT_CLICKED, and publishes
 *              UI_INPUT_RTD_REQUEST / UI_INPUT_RTD_RELEASE respectively. The
 *              App Layer maps them to operating mode RTD and DEBUG, and the
 *              CAN module transmits that as the RTD_Button bit — so the bit on
 *              the bus follows the driver's thumb, and letting go always clears
 *              the request.
 *
 *              Requiring a long press rather than a click is deliberate: a
 *              brush against the button must not put the car into RTD.
 *
 *              The visual state is driven explicitly through LV_STATE_USER_1
 *              instead of LVGL's LV_STATE_CHECKED, because the button is not
 *              checkable — it has no state of its own to toggle.
 *
 * @author      Mario Wegmann <mario.wegmann@web.de>
 * @date        Created: 2026-06-08
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
#include "modules/ui/widgets/ui_header.h"
#include "modules/ui/widgets/ui_hintbar.h"
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
 * @brief Horizontal offset of the button from the screen centre, in pixels.
 *
 * The name is a leftover from the two-button layout this screen was copied
 * from, where it was half the centre-to-centre distance. With one button it is
 * simply how far right of centre that button sits — the second, negative
 * offset is used only by the commented-out counterpart below.
 */
#define BTN_HALF_SPACING        55

/**
 * @brief Bottom margin for the button row, in pixels.
 *
 * Measured from the top of the hint bar, not from the screen edge — the
 * bar owns the bottom UI_HINTBAR_H pixels, and anything anchored to
 * LV_ALIGN_BOTTOM_* without adding it lands underneath.
 */
#define BTN_BOTTOM_MARGIN       (UI_HINTBAR_H + 20)


/* ── Private Variables ───────────────────────────────────────────────────────────────────────── */

/** @brief RTD button — long-pressed to request Ready-to-Drive. */
static lv_obj_t   *s_btn_rtd;

/** @brief Input group for the right encoder. Created empty — no focusable widget. */
static lv_group_t *s_right_encoder_group;

/** @brief Input group for the left button pad. Never created; stays NULL. */
static lv_group_t *s_left_button_group;

/** @brief Input group for the right button pad. Never created; stays NULL. */
static lv_group_t *s_right_button_group;

/** @brief Input group for the dedicated RTD button pad — holds the RTD button. */
static lv_group_t *s_rtd_button_group;


/**
 * @brief What each control does on this screen; see @ref ui_hint_input.
 *
 * Static storage: ui_hintbar_create() keeps the pointers rather than copying
 * the strings. Controls left out here are dimmed in the bar.
 */
static const char *const k_hints[UI_HINT_INPUT_COUNT] = {
    [UI_HINT_ENC_LEFT] = "Screen",
    [UI_HINT_BTN_MID]  = "RTD",
};

/* ── Private Function Prototypes ─────────────────────────────────────────────────────────────── */
static void build_buttons(lv_obj_t *scr);
static void btn_rtd_event_cb(lv_event_t *e);


/* ── Private Function Implementations ───────────────────────────────────────────────────────── */

/**
 * @brief Build the RTD button.
 *
 * Only LV_EVENT_LONG_PRESSED is handled: a brush against the button must not
 * put the car into RTD, and there is no release to react to — the request
 * latches (see btn_rtd_event_cb()).
 *
 * @param scr  Screen object to build into.
 */
static void build_buttons(lv_obj_t *scr)
{

    /* ── RTD button ────────────────────────────────────────────────────── */

    s_btn_rtd = lv_button_create(scr);
    lv_obj_remove_style_all(s_btn_rtd);
    lv_obj_add_style(s_btn_rtd, &ui_style_btn_default, 0);
    lv_obj_add_style(s_btn_rtd, &ui_style_btn_checked, LV_STATE_USER_1);
    lv_obj_add_style(s_btn_rtd, &ui_style_btn_focused, LV_STATE_FOCUS_KEY);
    lv_obj_set_size(s_btn_rtd, BTN_WIDTH, BTN_HEIGHT);
    lv_obj_align(s_btn_rtd, LV_ALIGN_BOTTOM_MID, BTN_HALF_SPACING, -BTN_BOTTOM_MARGIN);

    lv_obj_t *lbl_rtd = lv_label_create(s_btn_rtd);
    lv_obj_add_style(lbl_rtd, &ui_style_label_subtitle, 0);
    lv_label_set_text(lbl_rtd, "RTD");
    lv_obj_align(lbl_rtd, LV_ALIGN_CENTER, 0, 0);

    lv_obj_add_event_cb(s_btn_rtd, btn_rtd_event_cb, LV_EVENT_LONG_PRESSED, NULL);
}

/**
 * @brief RTD button long-press handler.
 *
 * Publishes UI_INPUT_RTD_REQUEST. The App Layer's state machine latches
 * OPERATING_MODE_RTD (→ CAN rtd_button = 1) and loads the EV driving screen,
 * which replaces this one. There is no release event and no way back to DEBUG
 * short of a power cycle.
 *
 * The visual state is set first so the button flashes armed even if the
 * publish fails; the screen is torn down a moment later regardless.
 *
 * @param e  LV_EVENT_LONG_PRESSED from the RTD button.
 */
static void btn_rtd_event_cb(lv_event_t *e)
{
    lv_obj_set_state(lv_event_get_target_obj(e), LV_STATE_USER_1, true);

    struct ui_input_event evt = { .type = UI_INPUT_RTD_REQUEST };

    int ret = zbus_chan_pub(&ui_input_chan, &evt, K_NO_WAIT);
    if (ret != 0) {
        LOG_WRN("RTD request publish failed: %d", ret);
    } else {
        LOG_DBG("RTD requested");
    }
}


/* ── Public Function Implementations ─────────────────────────────────────────────────────────── */

lv_obj_t *screen_checklist_create(lv_subject_t *status_subjects)
{
    /* ── Screen base ─────────────────────────────────────────────────────── */

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_add_style(scr, &ui_style_screen, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* ── Widgets ─────────────────────────────────────────────────────────── */

    ui_header_create(scr, "PRE RTD", status_subjects);
    build_buttons(scr);

    /* ── Input groups ────────────────────────────────────────────────────── */

    /*
     * The encoder group is created empty, so the encoder stays attached and a
     * widget added here later needs no change in ui.c.  The RTD button lives in
     * its own group, bound to the dedicated keypad_rtd pad in ui.c, and stays
     * in edit mode as its only member.
     */
    s_right_encoder_group = lv_group_create();
    lv_group_set_editing(s_right_encoder_group, true);

    s_rtd_button_group = lv_group_create();
    lv_group_add_obj(s_rtd_button_group, s_btn_rtd);
    lv_group_set_editing(s_rtd_button_group, true);

    ui_hintbar_create(scr, k_hints);

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

lv_group_t *screen_checklist_get_rtd_button_group(void)
{
    return s_rtd_button_group;
}


