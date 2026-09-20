/**
 * @file        screen_checklist.c
 * @brief       EV checklist screen implementation — readouts and the RTD button
 *
 * @ingroup     dcu_ui_screens
 *
 * @details     Builds the screen registered for SCREEN_PRE_RTD, titled
 *              "EV CHECKLIST". The contract and the layout sketch are in
 *              screen_checklist.h.
 *
 *              ### Readouts
 *              build_bars() creates six bar rows in two columns of three: brake
 *              pressure, air pressure and accumulator voltage, front / HV on the
 *              left, rear / LV on the right. build_bar_row() builds one row from
 *              a signal descriptor — a bar bound to the signal's subject, its
 *              caption above and a ui_quantity to its right. Caption, unit,
 *              format, bar range and limits all come from the descriptor, which
 *              dbc/dcu_app.yaml declares; only the geometry is decided in this
 *              file. Everything follows the bus through the subjects, and the
 *              screen keeps no state for it. Nothing is evaluated: the readouts
 *              do not influence the RTD button.
 *
 *              ### The RTD button
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
 * @version     0.2.0
 *
 * @copyright   Copyright (c) 2026 Mario Wegmann.
 *              SPDX-License-Identifier: Apache-2.0
 */

/*
 * ─────────────────────────────────────────────────────────────────────────────────────────────────
 * Revision History
 * Version  Date        Author          Description
 * 0.1.0    2026-06-08  Mario Wegmann   Initial creation
 * 0.2.0    2026-09-20  Mario Wegmann   Pressure and voltage readouts added; header
 *                                      retitled "EV CHECKLIST"
 * ─────────────────────────────────────────────────────────────────────────────────────────────────
 */

/* ── Corresponding Header ────────────────────────────────────────────────────────────────────── */

#include "modules/ui/screens/screen_checklist.h"

/* ── Zephyr Includes ─────────────────────────────────────────────────────────────────────────── */

#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/logging/log.h>

/* ── Project Includes ────────────────────────────────────────────────────────────────────────── */

#include "modules/ui/ui_layout.h"
#include "modules/ui/ui_styles.h"
#include "modules/ui/widgets/ui_header.h"
#include "modules/ui/widgets/ui_hintbar.h"
#include "modules/ui/widgets/ui_quantity.h"
#include "services/event_bus/event_bus.h"
#include "services/event_bus/events.h"
#include "generated/ui_subjects_gen.h"

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
#define BTN_HALF_SPACING        70

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
    [UI_HINT_ENC_LEFT] = "Switch Screen",
    [UI_HINT_BTN_MID]  = "Hold: Send RTD",
};

/* ── Private Function Prototypes ─────────────────────────────────────────────────────────────── */
static void build_buttons(lv_obj_t *scr);
static void rtd_button_refresh(void);
static void rtd_publish(enum ui_input_type type);
static void btn_rtd_event_cb(lv_event_t *e);


/* ── Private Function Implementations ───────────────────────────────────────────────────────── */

/** @brief Height of every bar, in layout units (see ui_layout_u()). */
#define BAR_H_U             2

/** @brief Space between the bar and the value beside it, in layout units. */
#define BAR_VALUE_GAP_U     1

/**
 * @brief Least width the value gets, in layout units.
 *
 * The value grows with its digits. Without a floor the bar next to it would
 * shrink and grow with them, on every change of digit count.
 */
#define VALUE_MIN_W_U       8

/**
 * @brief Build one bar row: the caption on top, under it the bar with the value.
 *
 * Everything about the signal — unit, decimals, bar range and limits — comes
 * from its descriptor, which the YAML declares. The row places itself: it is as
 * wide as its parent, the bar takes whatever the value leaves, and nothing here
 * names a coordinate.
 *
 * Bar and value sit in one line and are centred on the same axis, so the value
 * stands level with the bar and not with the caption above it.
 *
 * @param parent   Column to build into.
 * @param desc     The signal to show.
 * @param caption  Text over the bar; the descriptor's label or short_label.
 */
static void build_bar_row(lv_obj_t *parent, const struct ui_signal_desc *desc,
                          const char *caption)
{
    /* Row: the caption, and under it the line with bar and value. */
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *lbl_title = lv_label_create(row);
    lv_obj_add_style(lbl_title, &ui_style_label_subtitle, 0);
    lv_label_set_long_mode(lbl_title, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(lbl_title, lv_pct(100));
    lv_label_set_text(lbl_title, caption);

    /*
     * Line: [ bar ][ value ] on one axis.
     *
     * The bar's outline is drawn outside its box, and a container clips what
     * lies outside itself, so the line has to keep room for it: on the left
     * through padding, above and below through a minimum height that is the
     * bar plus an outline on each side. On the right the gap to the value is
     * room enough. The value is usually the taller of the two and the minimum
     * then costs nothing.
     */
    lv_obj_t *line = lv_obj_create(row);
    lv_obj_remove_style_all(line);
    lv_obj_set_size(line, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(line, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(line, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(line, ui_layout_u(BAR_VALUE_GAP_U), 0);
    lv_obj_set_style_pad_left(line, UI_SLIDER_OUTLINE_W, 0);
    lv_obj_set_style_min_height(line,
                                ui_layout_u(BAR_H_U) + 2 * UI_SLIDER_OUTLINE_W, 0);

    /*
     * The line is as tall as its tallest child, which is the value, and the bar
     * is centred in it — so it would hang below the caption by the difference.
     * Pull the line up by the space above the bar, and the bar sits directly
     * under the caption; the value rises into the free space beside it.
     */
    int32_t bar_h  = ui_layout_u(BAR_H_U);
    int32_t line_h = LV_MAX(lv_font_get_line_height(&BarlowCondensed_BoldItalic_32),
                            bar_h + 2 * UI_SLIDER_OUTLINE_W);
    lv_obj_set_style_margin_top(line, -((line_h - bar_h) / 2), 0);

    /* Bar: takes what the value leaves. */
    lv_obj_t *bar = lv_bar_create(line);
    lv_obj_remove_style_all(bar);
    lv_obj_add_style(bar, &ui_style_slider_main, LV_PART_MAIN);
    lv_obj_add_style(bar, &ui_style_slider_indicator, LV_PART_INDICATOR);
    if (desc->flags & UI_SIG_RANGE) {
        lv_bar_set_range(bar, (int32_t)desc->range_min, (int32_t)desc->range_max);
    }
    lv_bar_bind_value(bar, desc->subject);
    lv_obj_set_size(bar, 0, bar_h);
    lv_obj_set_flex_grow(bar, 1);

    /* Value: as wide as its digits, but never narrower than VALUE_MIN_W_U. */
    lv_obj_t *qty = ui_quantity_create(line,
                                       &BarlowCondensed_BoldItalic_32,
                                       &BarlowCondensed_Italic_20, desc->unit);
    lv_obj_set_style_min_width(qty, ui_layout_u(VALUE_MIN_W_U), 0);
    ui_quantity_bind_signal(qty, desc);
}

/**
 * @brief Build every bar row into the screen's content area.
 *
 * Two columns of three. Left: brake pressure front, air pressure front, HV
 * battery voltage. Right: the same for the rear, and the LV battery voltage.
 * The captions are the short ones — a column is too narrow for the full
 * labels.
 *
 * @param scr  Screen object to build into.
 */
static void build_bars(lv_obj_t *scr)
{
    lv_obj_t *content = ui_layout_content_create(scr);
    lv_obj_t *left    = ui_layout_column_create(content, 50);
    lv_obj_t *right   = ui_layout_column_create(content, 50);

    build_bar_row(left, &ui_sig_brake_pressure_front, ui_sig_brake_pressure_front.label);
    build_bar_row(left, &ui_sig_air_pressure_front, ui_sig_air_pressure_front.label);
    build_bar_row(left, &ui_sig_voltage_accu_hv, ui_sig_voltage_accu_hv.label);

    build_bar_row(right, &ui_sig_brake_pressure_rear, ui_sig_brake_pressure_rear.label);
    build_bar_row(right, &ui_sig_air_pressure_rear, ui_sig_air_pressure_rear.label);
    build_bar_row(right, &ui_sig_lv_accu_voltage, ui_sig_lv_accu_voltage.label);
}

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
    lv_label_set_text(lbl_rtd, "SEND RTD");
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

    ui_header_create(scr, "EV CHECKLIST", status_subjects);

    build_bars(scr);

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


