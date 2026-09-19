/**
 * @file        screen_checklist.c
 * @brief       Pre-RTD screen implementation — the Ready-to-Drive button
 *
 * @ingroup     dcu_ui_screens
 *
 * @details     Builds the pre-RTD screen. Its only widget is the RTD button;
 *              the checklist the file is named after is still to come.
 *
 *              The button reports the physical press, nothing more: it
 *              publishes UI_INPUT_RTD_PRESSED on LV_EVENT_PRESSED and
 *              UI_INPUT_RTD_RELEASED on every way a press can end. The hold
 *              time and the CAN bit are decided outside the UI (see
 *              app_state_is_rtd_request_active()), so this file cannot make
 *              the request outlive the driver's thumb.
 *
 *              ### Every way a press ends
 *              | Event              | When                                    |
 *              |--------------------|-----------------------------------------|
 *              | LV_EVENT_RELEASED  | The driver lets go                      |
 *              | LV_EVENT_PRESS_LOST| LVGL abandons the press                 |
 *              | LV_EVENT_DELETE    | The screen is torn down while held      |
 *
 *              The last one matters: LVGL delivers the release to whatever the
 *              RTD pad's group holds at that moment, and after a screen change
 *              that is no longer this button. Without it the App Layer would
 *              never hear the release. It keeps a second backstop anyway — a
 *              screen change away from PRE_RTD clears the button state.
 *
 *              ### Color
 *              Driven through two user states, because the button is not
 *              checkable and LVGL's own states do not describe "on the bus":
 *
 *              | State          | Style                 | Meaning                 |
 *              |----------------|-----------------------|-------------------------|
 *              | —              | ui_style_btn_default  | Not pressed (white)     |
 *              | LV_STATE_USER_1| ui_style_btn_pending  | Held, not yet sent (gold) |
 *              | LV_STATE_USER_2| ui_style_btn_checked  | Held and sent (green)   |
 *
 *              ui_style_btn_focused is deliberately not added: the button is
 *              its group's only member and permanently focused, so the focus
 *              color would hide the white "not pressed" state.
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

/** @brief RTD button — held to request Ready-to-Drive. NULL once deleted. */
static lv_obj_t   *s_btn_rtd;

/** @brief True between LV_EVENT_PRESSED and the end of that press. */
static bool        s_rtd_pressed;

/** @brief Last UI_CMD_RTD_TX_STATE: RTD_Button = 1 is on the bus. */
static bool        s_rtd_on_bus;

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
static void rtd_button_refresh(void);
static void rtd_publish(enum ui_input_type type);
static void btn_rtd_event_cb(lv_event_t *e);


/* ── Private Function Implementations ───────────────────────────────────────────────────────── */

/**
 * @brief Build the RTD button.
 *
 * One callback for the press and for all three ways a press can end; see the
 * file description for why LV_EVENT_DELETE is among them.
 *
 * @param scr  Screen object to build into.
 */
static void build_buttons(lv_obj_t *scr)
{

    /* ── RTD button ────────────────────────────────────────────────────── */

    s_btn_rtd = lv_button_create(scr);
    lv_obj_remove_style_all(s_btn_rtd);
    lv_obj_add_style(s_btn_rtd, &ui_style_btn_default, 0);
    lv_obj_add_style(s_btn_rtd, &ui_style_btn_pending, LV_STATE_USER_1);
    lv_obj_add_style(s_btn_rtd, &ui_style_btn_checked, LV_STATE_USER_2);
    lv_obj_set_size(s_btn_rtd, BTN_WIDTH, BTN_HEIGHT);
    lv_obj_align(s_btn_rtd, LV_ALIGN_BOTTOM_MID, BTN_HALF_SPACING, -BTN_BOTTOM_MARGIN);

    lv_obj_t *lbl_rtd = lv_label_create(s_btn_rtd);
    lv_obj_add_style(lbl_rtd, &ui_style_label_subtitle, 0);
    lv_label_set_text(lbl_rtd, "RTD");
    lv_obj_align(lbl_rtd, LV_ALIGN_CENTER, 0, 0);

    lv_obj_add_event_cb(s_btn_rtd, btn_rtd_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_btn_rtd, btn_rtd_event_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(s_btn_rtd, btn_rtd_event_cb, LV_EVENT_PRESS_LOST, NULL);
    lv_obj_add_event_cb(s_btn_rtd, btn_rtd_event_cb, LV_EVENT_DELETE, NULL);
}

/**
 * @brief Set the button color from the press and the bus state.
 *
 * Green needs both: a report of RTD_Button = 1 that arrives after the release
 * must not light the button again.
 */
static void rtd_button_refresh(void)
{
    if (s_btn_rtd == NULL) {
        return;
    }

    lv_obj_set_state(s_btn_rtd, LV_STATE_USER_1, s_rtd_pressed && !s_rtd_on_bus);
    lv_obj_set_state(s_btn_rtd, LV_STATE_USER_2, s_rtd_pressed && s_rtd_on_bus);
}

/**
 * @brief Publish an RTD press or release on ui_input_chan.
 * @param type  UI_INPUT_RTD_PRESSED or UI_INPUT_RTD_RELEASED.
 */
static void rtd_publish(enum ui_input_type type)
{
    struct ui_input_event evt = { .type = type };

    int ret = zbus_chan_pub(&ui_input_chan, &evt, K_NO_WAIT);
    if (ret != 0) {
        LOG_WRN("RTD button event %d publish failed: %d", (int)type, ret);
    }
}

/**
 * @brief RTD button handler for the press and for every end of a press.
 *
 * A press clears the bus flag: no frame of this press can have carried
 * RTD_Button = 1 yet, and a stale report from an earlier press must not show
 * green straight away.
 *
 * The end of a press is published only if a press was recorded, so deleting
 * the screen without the button held stays silent.
 *
 * @param e  LV_EVENT_PRESSED, _RELEASED, _PRESS_LOST or _DELETE.
 */
static void btn_rtd_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_PRESSED) {
        s_rtd_pressed = true;
        s_rtd_on_bus  = false;
        rtd_button_refresh();
        rtd_publish(UI_INPUT_RTD_PRESSED);
        return;
    }

    /* RELEASED, PRESS_LOST or DELETE — the press is over either way. */
    bool was_pressed = s_rtd_pressed;

    s_rtd_pressed = false;
    s_rtd_on_bus  = false;

    if (code == LV_EVENT_DELETE) {
        s_btn_rtd = NULL;
    } else {
        rtd_button_refresh();
    }

    if (was_pressed) {
        rtd_publish(UI_INPUT_RTD_RELEASED);
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

    /* A fresh screen starts released, whatever the previous instance saw. */
    s_rtd_pressed = false;
    s_rtd_on_bus  = false;
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

void screen_checklist_set_rtd_tx(bool on_bus)
{
    s_rtd_on_bus = on_bus;
    rtd_button_refresh();
}


