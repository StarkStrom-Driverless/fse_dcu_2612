/**
 * @file
 * @brief       Generic value screen implementation
 *
 * @ingroup     dcu_ui_screens
 *
 * @details     A scratch screen for values with no fixed meaning, so an
 *              engineer can bind something to them between runs and read or
 *              set it from the wheel without a firmware change.
 *
 *              ### Read column (left)
 *
 *              DCU_Custom_Wert_1 and _2 as plain labels, bound straight to
 *              their generated subjects. Raw integers, no unit and no
 *              thresholds — whatever the values mean today is written down
 *              somewhere else, not here.
 *
 *              ### Send column (right)
 *
 *                – Roller     : the values 0…7, scrolled by the right encoder.
 *                               Scrolling publishes nothing.
 *
 *                – Label      : "Current Debug Bits: …", driven by an observer
 *                               on ui_tx_subj_debug_bits, so it shows the last
 *                               confirmed value rather than the highlighted one.
 *
 *                – SEND BITS  : publishes UI_INPUT_DEBUG_BITS_SELECTED with the
 *                               highlighted value.  The App Layer forwards it to
 *                               the settings service, which clamps and owns it;
 *                               the CAN module reads it back on its next cycle.
 *
 *              The screen is two columns of equal width in the content area
 *              (ui_layout.h): the read column on the left, the send column on the
 *              right. Nothing is placed by a coordinate.
 *
 *              ### Range and ownership
 *              The roller offers 0…7 because Debug_SETTING is three bits wide
 *              in the DBC.  The bound is not enforced here — the settings
 *              service clamps against the generated schema, so the roller only
 *              has to avoid offering values that would be rejected.
 *
 *              ### State across visits
 *              The screen is destroyed on leaving, so the roller position is
 *              kept in the file-scope subject s_roller_sel and restored on the
 *              next visit.  On the very first visit after boot it starts on the
 *              stored value rather than on 0 — settings_get() is the only place
 *              the persisted debug bits can be read back from.
 *
 *              The confirmed value lives in the generated TX subject, which
 *              nothing outside this file writes, so it is re-seeded from
 *              settings_get() on every build: the settings service owns the
 *              value and the label has to agree with it. The read labels need
 *              no handling at all — they rebind on creation and the next CAN
 *              snapshot fills them.
 *
 * @author      Mario Wegmann <mario.wegmann@web.de>
 * @date        Created: 2026-06-09
 *
 * @copyright   Copyright (c) 2026 Mario Wegmann.
 *              SPDX-License-Identifier: Apache-2.0
 */

/* ── Corresponding Header ────────────────────────────────────────────────────────────────────── */

#include "modules/ui/screens/screen_debug_custom.h"

/* ── Zephyr Includes ─────────────────────────────────────────────────────────────────────────── */

#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/logging/log.h>

/* ── Project Includes ────────────────────────────────────────────────────────────────────────── */

#include "modules/ui/ui_layout.h"
#include "modules/ui/ui_styles.h"
#include "modules/ui/widgets/ui_header.h"
#include "modules/ui/widgets/ui_hintbar.h"
#include "services/event_bus/event_bus.h"
#include "services/event_bus/events.h"
#include "services/settings/settings.h"
#include "generated/ui_subjects_gen.h"
#include "generated/ui_tx_subjects_gen.h"

/* ── Zephyr Logging ──────────────────────────────────────────────────────────────────────────── */

LOG_MODULE_REGISTER(screen_debug_custom, CONFIG_LOG_DEFAULT_LEVEL);


/* ── Private Macros & Constants ──────────────────────────────────────────────────────────────── */

/**
 * @brief Roller option string — the selectable debug values.
 *
 * The index is the value: line 0 is 0, line 7 is 7. Eight entries, because
 * Debug_SETTING is three bits wide in the DBC.
 */
#define ROLLER_OPTIONS \
    "0\n"              \
    "1\n"              \
    "2\n"              \
    "3\n"              \
    "4\n"              \
    "5\n"              \
    "6\n"              \
    "7"                \

/** @brief Number of roller rows visible simultaneously (one above/below the selection). */
#define ROLLER_VISIBLE_ROWS     3U

/** @brief Width of the send button, in layout units (see ui_layout_u()). */
#define BTN_W_U             10

/** @brief Height of the send button, in layout units. */
#define BTN_H_U             5

/** @brief Space between the two read rows, in layout units. */
#define READ_ROW_GAP_U      3


/* ── Private Variables ───────────────────────────────────────────────────────────────────────── */

/**
 * @brief Currently highlighted roller index — survives screen destroy/recreate.
 *
 * Initialized once, guarded by s_subjects_init, and never re-initialized: that
 * is what carries the highlight across the create/delete cycle.
 */
static lv_subject_t s_roller_sel;

/** @brief Guard so the subject above is initialized exactly once. */
static bool         s_subjects_init;

/** @brief Debug-value roller — user scrolls with the right encoder. */
static lv_obj_t   *s_roller;

/** @brief "Current Debug Bits: …" label; driven by an observer on the TX subject. */
static lv_obj_t   *s_lbl_confirmed;

/** @brief SEND BITS button — confirms the highlighted value. */
static lv_obj_t   *s_btn_send;

/** @brief Input group for the right encoder — holds the roller. */
static lv_group_t *s_right_encoder_group;

/** @brief Input group for the left button pad. Never created; stays NULL. */
static lv_group_t *s_left_button_group;

/** @brief Input group for the right button pad — holds SEND BITS. */
static lv_group_t *s_right_button_group;


/**
 * @brief What each control does on this screen; see @ref ui_hint_input.
 *
 * Static storage: ui_hintbar_create() keeps the pointers rather than copying
 * the strings. Controls left out here are dimmed in the bar.
 */
static const char *const k_hints[UI_HINT_INPUT_COUNT] = {
    [UI_HINT_ENC_LEFT]  = "Switch Screen",
    [UI_HINT_BTN_RIGHT] = "Send Bits",
    [UI_HINT_ENC_RIGHT] = "Select Bits",
};

/* ── Private Function Prototypes ─────────────────────────────────────────────────────────────── */

static void build_roller(lv_obj_t *parent);
static void build_send_button(lv_obj_t *parent);
static void btn_send_event_cb(lv_event_t *e);


/* ── Private Function Implementations ───────────────────────────────────────────────────────── */

/**
 * @brief Mirror the roller's position into s_roller_sel so it survives a rebuild.
 *
 * @param e  LV_EVENT_VALUE_CHANGED from the roller.
 */
static void roller_value_changed_cb(lv_event_t *e)
{
    lv_subject_set_int(&s_roller_sel, (int32_t)lv_roller_get_selected(lv_event_get_target_obj(e)));
}

/**
 * @brief Update the "Current Debug Bits" label from the confirmed value.
 *
 * @param observer  Observer whose target object is the label.
 * @param subject   ui_tx_subj_debug_bits.
 */
static void confirmed_bits_observer_cb(lv_observer_t *observer, lv_subject_t *subject)
{
    lv_obj_t *lbl = lv_observer_get_target_obj(observer);
    lv_label_set_text_fmt(lbl, "Current Debug Bits: %d", (int)lv_subject_get_int(subject));
}

/**
 * @brief Build the value roller and the confirmed-value label.
 *
 * @param scr  Screen object to build into.
 */
static void build_roller(lv_obj_t *parent)
{
    s_roller = lv_roller_create(parent);

    lv_roller_set_options(s_roller, ROLLER_OPTIONS, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(s_roller, ROLLER_VISIBLE_ROWS);
    lv_roller_set_selected(s_roller, (uint16_t)lv_subject_get_int(&s_roller_sel), LV_ANIM_OFF);

    lv_obj_set_width(s_roller, lv_pct(100));

    /* ── Main part: all items ──────────────────────────────────────────── */
    lv_obj_set_style_bg_color(s_roller,     UI_C_WHITE,                     LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_roller,       LV_OPA_COVER,                   LV_PART_MAIN);
    lv_obj_set_style_text_font(s_roller,    &BarlowCondensed_BoldItalic_18, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_roller,   UI_C_DARK,                      LV_PART_MAIN);
    lv_obj_set_style_border_width(s_roller, 2,                              LV_PART_MAIN);
    lv_obj_set_style_border_color(s_roller, UI_C_DARK,                      LV_PART_MAIN);
    lv_obj_set_style_radius(s_roller,       0,                              LV_PART_MAIN);

    /* ── Selected part: center row highlight ───────────────────────────── */
    lv_obj_set_style_bg_color(s_roller,   UI_C_ACCENT,                     LV_PART_SELECTED);
    lv_obj_set_style_bg_opa(s_roller,     LV_OPA_COVER,                    LV_PART_SELECTED);
    lv_obj_set_style_text_font(s_roller,  &BarlowCondensed_BoldItalic_18,  LV_PART_SELECTED);
    lv_obj_set_style_text_color(s_roller, UI_C_DARK,                       LV_PART_SELECTED);

    lv_obj_add_event_cb(s_roller, roller_value_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* ── Confirmed bits label (observer-driven) ─────────────────────────── */

    s_lbl_confirmed = lv_label_create(parent);
    lv_obj_add_style(s_lbl_confirmed, &ui_style_label_subtitle, 0);
    lv_subject_add_observer_obj(&ui_tx_subj_debug_bits, confirmed_bits_observer_cb,
                                s_lbl_confirmed, NULL);
}

/**
 * @brief Build one read row: a caption with the live value underneath.
 *
 * The caption and the number format come from the signal's descriptor; only the
 * position is decided here.
 *
 * @param scr   Screen object to build into.
 * @param y     Y position of the caption, from the top of the screen.
 * @param desc  The signal to show, e.g. &ui_sig_custom_1.
 */
static void build_read_row(lv_obj_t *parent, const struct ui_signal_desc *desc)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *lbl_caption = lv_label_create(row);
    lv_obj_add_style(lbl_caption, &ui_style_label_subtitle, 0);
    lv_label_set_text(lbl_caption, desc->label);

    lv_obj_t *lbl_value = lv_label_create(row);
    lv_obj_add_style(lbl_value, &ui_style_label_title, 0);
    lv_label_bind_text(lbl_value, desc->subject, desc->fmt);
}

/**
 * @brief Build the read column — the two generic values coming from the vehicle.
 *
 * Plain labels bound straight to their subjects: no unit, no thresholds, no
 * formatting beyond the raw integer. The values have no fixed meaning, so there
 * is nothing to dress them up with.
 *
 * The captions name the DBC signals; the subjects carry the app_names from
 * dbc/dcu_app.yaml, which map one to one onto them.
 *
 * The rows stand one under the other from the top of the column, READ_ROW_GAP_U
 * apart.
 *
 * @param col  Column to build into.
 */
static void build_read_column(lv_obj_t *col)
{
    lv_obj_set_style_pad_top(col, ui_layout_u(1), 0);
    lv_obj_set_style_pad_row(col, ui_layout_u(READ_ROW_GAP_U), 0);

    build_read_row(col, &ui_sig_custom_1);
    build_read_row(col, &ui_sig_custom_2);
}

/**
 * @brief Build the SEND BITS button.
 *
 * @param scr  Screen object to build into.
 */
static void build_send_button(lv_obj_t *parent)
{
    /* ── Send button ─────────────────────────────────────────────────────── */

    s_btn_send = lv_button_create(parent);
    lv_obj_remove_style_all(s_btn_send);
    lv_obj_add_style(s_btn_send, &ui_style_btn_default, 0);
    lv_obj_add_style(s_btn_send, &ui_style_btn_checked, LV_STATE_PRESSED);
    lv_obj_add_style(s_btn_send, &ui_style_btn_focused, LV_STATE_FOCUS_KEY);
    lv_obj_set_size(s_btn_send, ui_layout_u(BTN_W_U), ui_layout_u(BTN_H_U));

    lv_obj_t *lbl_send = lv_label_create(s_btn_send);
    lv_obj_add_style(lbl_send, &ui_style_label_subtitle, 0);
    lv_label_set_text(lbl_send, "SEND BITS");
    lv_obj_align(lbl_send, LV_ALIGN_CENTER, 0, 0);

    lv_obj_add_event_cb(s_btn_send, btn_send_event_cb, LV_EVENT_CLICKED, NULL);
}

/**
 * @brief SEND BITS click handler — confirm the highlighted debug value.
 *
 * Publishes UI_INPUT_DEBUG_BITS_SELECTED. The App Layer hands the value to the
 * settings service; the CAN module reads it from there on its next TX cycle.
 * No CAN frame is built here.
 *
 * The TX subject — which drives the label — is only updated once the publish
 * succeeded, so a dropped event cannot leave the display claiming a value the
 * vehicle was never sent.
 *
 * The roller index is the value, so it goes into the event unchanged. Narrowing
 * it to uint8_t is safe for the same reason the roller offers exactly eight
 * entries: Debug_SETTING is three bits wide.
 *
 * @param e  LV_EVENT_CLICKED from the button. Unused.
 */
static void btn_send_event_cb(lv_event_t *e)
{
    ARG_UNUSED(e);
    uint16_t idx = lv_roller_get_selected(s_roller);

    struct ui_input_event evt = {
        .type            = UI_INPUT_DEBUG_BITS_SELECTED,
        .data.debug_bits = (uint8_t)idx,
    };

    int ret = zbus_chan_pub(&ui_input_chan, &evt, K_NO_WAIT);
    if (ret != 0) {
        LOG_WRN("UI_INPUT_DEBUG_BITS_SELECTED publish failed (bits=%u): %d",
                (unsigned)idx, ret);
    } else {
        lv_subject_set_int(&ui_tx_subj_debug_bits, (int32_t)idx);
        LOG_DBG("Debug bits selected: %u", (unsigned)idx);
    }
}


/* ── Public Function Implementations ─────────────────────────────────────────────────────────── */

lv_obj_t *screen_debug_custom_create(lv_subject_t *status_subjects)
{
    /* ── Screen base ─────────────────────────────────────────────────────── */

    uint8_t stored = settings_get(SETTING_DEBUG_BITS);

    /*
     * First visit after boot: start the highlight on the value that is actually
     * being sent, not on 0. Later visits keep whatever the engineer scrolled to.
     */
    if (!s_subjects_init) {
        lv_subject_init_int(&s_roller_sel, (int32_t)stored);
        s_subjects_init = true;
    }

    /*
     * The confirmed-value label is driven by the generated TX subject, which
     * nothing else writes. Re-seed it on every build so it agrees with the
     * settings service — which owns the value and may have been changed from
     * SETTINGS or restored from flash in the meantime.
     */
    lv_subject_set_int(&ui_tx_subj_debug_bits, (int32_t)stored);

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_add_style(scr, &ui_style_screen, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* ── Widgets ─────────────────────────────────────────────────────────── */

    ui_header_create(scr, "DBG CUSTOM", status_subjects);

    /*
     * Two columns: what is received on the left, what is sent on the right.
     * The right one spreads the roller, the confirmed value and the send button
     * over its height and keeps an outline's width free above and below for the
     * button.
     */
    lv_obj_t *content = ui_layout_content_create(scr);
    lv_obj_t *left    = ui_layout_column_create(content, 50);
    lv_obj_t *right   = ui_layout_column_create(content, 50);

    lv_obj_set_flex_align(right, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_ver(right, UI_BTN_OUTLINE_W, 0);

    build_read_column(left);
    build_roller(right);
    build_send_button(right);

    /* ── Input groups ────────────────────────────────────────────────────── */

    /*
     * One member each, so there is nothing to tab between and both groups stay
     * in edit mode: a turn of the encoder scrolls the roller, a press of the
     * right pad clicks the button.
     */
    s_right_encoder_group = lv_group_create();
    lv_group_add_obj(s_right_encoder_group, s_roller);
    lv_group_set_editing(s_right_encoder_group, true);

    s_right_button_group = lv_group_create();
    lv_group_add_obj(s_right_button_group, s_btn_send);
    lv_group_set_editing(s_right_button_group, true);

    ui_hintbar_create(scr, k_hints);

    return scr;
}

lv_group_t *screen_debug_custom_get_right_encoder_group(void)
{
    return s_right_encoder_group;
}

lv_group_t *screen_debug_custom_get_left_button_group(void)
{
    return s_left_button_group;
}

lv_group_t *screen_debug_custom_get_right_button_group(void)
{
    return s_right_button_group;
}

