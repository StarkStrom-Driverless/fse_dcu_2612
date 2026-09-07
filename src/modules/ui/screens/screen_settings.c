/**
 * @file        screen_settings.c
 * @brief       Settings screen implementation
 *
 * @ingroup     dcu_ui_screens
 *
 * @details     A row-per-setting editor for the values in the generated
 *              settings schema. The contract and the layout sketch are in
 *              screen_settings.h.
 *
 *              ### Rows come from the schema, captions come from here
 *              k_rows[] pairs each enum setting_id with a human caption and a
 *              step size. The schema (settings_schema[]) still owns the bounds
 *              and the defaults; this table only adds what the generator has no
 *              way to know — a readable name and how far one button press
 *              should move the value. A setting added to dcu_app.yaml but not
 *              to k_rows[] simply does not appear; the reverse does not
 *              compile.
 *
 *              ### Focus instead of a roller
 *              The other input screens put a single roller in the encoder
 *              group and force edit mode. Here the group holds every row and
 *              stays in navigate mode, so a turn of the encoder walks the
 *              focus down the list. The focused row carries LV_STATE_FOCUS_KEY
 *              and picks up ui_style_btn_focused (gold); the rest stay white.
 *
 *              ### Applying a change
 *              adjust_focused() clamps against settings_schema[id] and
 *              publishes UI_INPUT_SETTING_SELECTED. The row subject — which the
 *              value label is bound to — is only updated once the publish
 *              succeeded, so a dropped event cannot leave the display showing a
 *              value the settings service never received. The settings service
 *              clamps again and is the real owner; the clamp here only keeps
 *              the screen from offering a value that would be rejected.
 *
 *              ### Threading
 *              screen_settings_create() runs in the LVGL thread and calls
 *              settings_get() to seed the rows. That read is mutex-protected
 *              and safe from any thread (see services/settings/settings.h);
 *              the write path stays on ui_input_chan → App → settings_set().
 *
 * @author      Mario Wegmann <mario.wegmann@web.de>
 * @date        Created: 2026-09-01
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
 * 0.1.0    2026-09-01  Mario Wegmann   Initial creation
 * ─────────────────────────────────────────────────────────────────────────────────────────────────
 */

/* ── Corresponding Header ────────────────────────────────────────────────────────────────────── */

#include "modules/ui/screens/screen_settings.h"

/* ── Standard Includes ───────────────────────────────────────────────────────────────────────── */

#include <stdint.h>

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
#include "services/settings/settings.h"

/* ── Zephyr Logging ──────────────────────────────────────────────────────────────────────────── */

LOG_MODULE_REGISTER(screen_settings, CONFIG_LOG_DEFAULT_LEVEL);


/* ── Private Macros & Constants ──────────────────────────────────────────────────────────────── */

/** @brief Y position of the row list, below the header and the page indicator. */
#define LIST_TOP_Y              70

/** @brief Row-list width as a percentage of the screen width. */
#define LIST_WIDTH_PCT          66

/** @brief Horizontal padding inside a row, in pixels. */
#define ROW_PAD_H               10

/** @brief Vertical padding inside a row, in pixels. */
#define ROW_PAD_V               3

/** @brief Gap between two rows, in pixels. */
#define LIST_PAD_ROW            4

/** @brief Width of the "−" / "+" buttons, in pixels. */
#define ADJ_BTN_W               54

/** @brief Height of the "−" / "+" buttons, in pixels. */
#define ADJ_BTN_H               46

/** @brief Inset of the "−" / "+" buttons from the screen edge, in pixels. */
#define ADJ_BTN_MARGIN_X        12

/**
 * @brief Vertical nudge of the "−" / "+" buttons from the screen centre.
 *
 * A few pixels down so the pair clears the header and lines up with the middle
 * of the row list rather than with the geometric centre of the screen.
 */
#define ADJ_BTN_OFFSET_Y        14


/* ── Row Table ───────────────────────────────────────────────────────────────────────────────── */

/**
 * @brief One editable row: a schema setting, its caption and its step size.
 *
 * The bounds and the default are not repeated here — they live in
 * settings_schema[id]. Keep the order aligned with enum setting_id so the list
 * reads in schema order, though nothing enforces it.
 */
static const struct {
    enum setting_id id;    /**< Which schema entry this row edits.            */
    const char     *label; /**< Caption shown on the left of the row.         */
    uint8_t         step;  /**< Value change per "−" / "+" press.             */
} k_rows[] = {
    { SETTING_DEBUG_BITS,         "Debug Bits",         1 },
    { SETTING_ASR_SETTING,        "ASR",                1 },
    { SETTING_REKUP_SETTING,      "Recuperation",       1 },
    { SETTING_TORQUEVECT_SETTING, "Torque Vectoring",   1 },
    { SETTING_PWRLIMIT_SETTING,   "Power Limit",        1 },
    { SETTING_DISPLAY_BRIGHTNESS, "Display Brightness", 5 },
};

/** @brief Number of editable rows. */
#define ROW_COUNT       ARRAY_SIZE(k_rows)


/* ── Private Variables ───────────────────────────────────────────────────────────────────────── */

/**
 * @brief One int subject per row, holding the value the label shows.
 *
 * Initialised once, guarded by s_subjects_init; re-seeded from settings_get()
 * on every build. File-scope because the labels bind to them while the screen
 * is constructed and LVGL keeps the binding until the screen is destroyed.
 */
static lv_subject_t s_value[ROW_COUNT];

/** @brief Guard so the subjects above are initialised exactly once. */
static bool         s_subjects_init;

/**
 * @brief Index of the focused row — survives screen destroy/recreate.
 *
 * Written by row_focused_cb() and read in the factory to restore the focus on
 * the next visit.
 */
static uint8_t      s_sel_idx;

/** @brief Row objects, indexed like k_rows[]. Rebuilt on every visit. */
static lv_obj_t   *s_rows[ROW_COUNT];

/** @brief "−" button — lowers the focused setting. */
static lv_obj_t   *s_btn_minus;

/** @brief "+" button — raises the focused setting. */
static lv_obj_t   *s_btn_plus;

/** @brief Input group for the right encoder — holds every row, navigate mode. */
static lv_group_t *s_right_encoder_group;

/** @brief Input group for the left button pad — holds the "−" button. */
static lv_group_t *s_left_button_group;

/** @brief Input group for the right button pad — holds the "+" button. */
static lv_group_t *s_right_button_group;


/**
 * @brief What each control does on this screen; see @ref ui_hint_input.
 *
 * Static storage: ui_hintbar_create() keeps the pointers rather than copying
 * the strings. Controls left out here are dropped from the bar.
 */
static const char *const k_hints[UI_HINT_INPUT_COUNT] = {
    [UI_HINT_ENC_LEFT]  = "Screen",
    [UI_HINT_BTN_LEFT]  = "-",
    [UI_HINT_BTN_RIGHT] = "+",
    [UI_HINT_ENC_RIGHT] = "Setting",
};

/* ── Private Function Prototypes ─────────────────────────────────────────────────────────────── */

static void build_list(lv_obj_t *scr);
static void build_buttons(lv_obj_t *scr);
static void row_focused_cb(lv_event_t *e);
static void btn_minus_event_cb(lv_event_t *e);
static void btn_plus_event_cb(lv_event_t *e);
static void adjust_focused(int direction);


/* ── Private Function Implementations ───────────────────────────────────────────────────────── */

/**
 * @brief Remember which row is focused so the next visit can restore it.
 *
 * @param e  LV_EVENT_FOCUSED from a row; its user data is the row index.
 */
static void row_focused_cb(lv_event_t *e)
{
    s_sel_idx = (uint8_t)(uintptr_t)lv_obj_get_user_data(lv_event_get_target_obj(e));
}

/**
 * @brief Build one row: a focusable button with the caption and the value label.
 *
 * The value label binds straight to s_value[i], so it follows the subject
 * without a per-row callback.
 *
 * @param list  Row-list container.
 * @param i     Row index into k_rows[].
 */
static void build_row(lv_obj_t *list, uint8_t i)
{
    lv_obj_t *row = lv_button_create(list);
    lv_obj_remove_style_all(row);
    lv_obj_add_style(row, &ui_style_btn_default, 0);
    lv_obj_add_style(row, &ui_style_btn_focused, LV_STATE_FOCUS_KEY);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(row, ROW_PAD_H, 0);
    lv_obj_set_style_pad_ver(row, ROW_PAD_V, 0);
    lv_obj_set_user_data(row, (void *)(uintptr_t)i);

    /* Caption on the left, value hard against the right edge. */
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *lbl_name = lv_label_create(row);
    lv_obj_add_style(lbl_name, &ui_style_label_subtitle, 0);
    lv_label_set_text(lbl_name, k_rows[i].label);

    lv_obj_t *lbl_value = lv_label_create(row);
    lv_obj_add_style(lbl_value, &ui_style_label_subtitle, 0);
    lv_label_bind_text(lbl_value, &s_value[i], "%d");

    lv_obj_add_event_cb(row, row_focused_cb, LV_EVENT_FOCUSED, NULL);

    s_rows[i] = row;
}

/**
 * @brief Build the row list — one row per k_rows[] entry, in a flex column.
 *
 * @param scr  Screen object to build into.
 */
static void build_list(lv_obj_t *scr)
{
    lv_obj_t *list = lv_obj_create(scr);
    lv_obj_remove_style_all(list);
    lv_obj_set_width(list, lv_pct(LIST_WIDTH_PCT));
    lv_obj_set_height(list, LV_SIZE_CONTENT);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, LIST_TOP_Y);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, LIST_PAD_ROW, 0);
    lv_obj_clear_flag(list, LV_OBJ_FLAG_SCROLLABLE);

    for (uint8_t i = 0U; i < (uint8_t)ROW_COUNT; i++) {
        build_row(list, i);
    }
}

/**
 * @brief Build one edit button.
 *
 * @param scr    Screen object to build into.
 * @param text   Button caption ("−" or "+").
 * @param align  LV_ALIGN_LEFT_MID or LV_ALIGN_RIGHT_MID.
 * @param dx     Horizontal offset for @p align.
 * @param cb     Click handler.
 * @return       The button object.
 */
static lv_obj_t *build_button(lv_obj_t *scr, const char *text,
                              lv_align_t align, int32_t dx, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_button_create(scr);
    lv_obj_remove_style_all(btn);
    lv_obj_add_style(btn, &ui_style_btn_default, 0);
    lv_obj_add_style(btn, &ui_style_btn_checked, LV_STATE_PRESSED);
    lv_obj_add_style(btn, &ui_style_btn_focused, LV_STATE_FOCUS_KEY);
    lv_obj_set_size(btn, ADJ_BTN_W, ADJ_BTN_H);
    lv_obj_align(btn, align, dx, ADJ_BTN_OFFSET_Y);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_obj_add_style(lbl, &ui_style_label_title, 0);
    lv_label_set_text(lbl, text);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);

    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    return btn;
}

/**
 * @brief Build the "−" and "+" buttons flanking the row list.
 *
 * @param scr  Screen object to build into.
 */
static void build_buttons(lv_obj_t *scr)
{
    s_btn_minus = build_button(scr, "-", LV_ALIGN_LEFT_MID,
                               ADJ_BTN_MARGIN_X, btn_minus_event_cb);
    s_btn_plus  = build_button(scr, "+", LV_ALIGN_RIGHT_MID,
                               -ADJ_BTN_MARGIN_X, btn_plus_event_cb);
}

/**
 * @brief "−" click handler — lower the focused setting by its step.
 * @param e  LV_EVENT_CLICKED from the button. Unused.
 */
static void btn_minus_event_cb(lv_event_t *e)
{
    ARG_UNUSED(e);
    adjust_focused(-1);
}

/**
 * @brief "+" click handler — raise the focused setting by its step.
 * @param e  LV_EVENT_CLICKED from the button. Unused.
 */
static void btn_plus_event_cb(lv_event_t *e)
{
    ARG_UNUSED(e);
    adjust_focused(+1);
}

/**
 * @brief Change the focused row's setting by one step in @p direction.
 *
 * Clamps against settings_schema[id], publishes UI_INPUT_SETTING_SELECTED and,
 * only on a successful publish, updates the row subject so the label follows.
 * A press that would leave the value unchanged (already at a bound, or the
 * focus lost) does nothing.
 *
 * @param direction  -1 to lower, +1 to raise.
 */
static void adjust_focused(int direction)
{
    lv_obj_t *row = lv_group_get_focused(s_right_encoder_group);
    if (row == NULL) {
        return;
    }

    uint8_t               i    = (uint8_t)(uintptr_t)lv_obj_get_user_data(row);
    enum setting_id       id   = k_rows[i].id;
    const setting_desc_t *desc = &settings_schema[id];

    int cur  = lv_subject_get_int(&s_value[i]);
    int next = cur + direction * (int)k_rows[i].step;

    if (next < (int)desc->min) {
        next = (int)desc->min;
    } else if (next > (int)desc->max) {
        next = (int)desc->max;
    }

    if (next == cur) {
        return;   /* already at the bound */
    }

    struct ui_input_event evt = {
        .type             = UI_INPUT_SETTING_SELECTED,
        .data.setting.id  = id,
        .data.setting.val = (uint8_t)next,
    };

    int ret = zbus_chan_pub(&ui_input_chan, &evt, K_NO_WAIT);
    if (ret != 0) {
        LOG_WRN("UI_INPUT_SETTING_SELECTED publish failed (%s=%d): %d",
                desc->name, next, ret);
    } else {
        lv_subject_set_int(&s_value[i], next);
        LOG_DBG("Setting %s: %d → %d", desc->name, cur, next);
    }
}


/* ── Public Function Implementations ─────────────────────────────────────────────────────────── */

lv_obj_t *screen_settings_create(lv_subject_t *status_subjects)
{
    /* ── Screen base ─────────────────────────────────────────────────────── */

    if (!s_subjects_init) {
        for (uint8_t i = 0U; i < (uint8_t)ROW_COUNT; i++) {
            lv_subject_init_int(&s_value[i], 0);
        }
        s_subjects_init = true;
    }

    /*
     * Re-seed on every build: persistence may be off, and even when it is not
     * the App Layer can have changed a value while this screen was gone.
     */
    for (uint8_t i = 0U; i < (uint8_t)ROW_COUNT; i++) {
        lv_subject_set_int(&s_value[i], (int32_t)settings_get(k_rows[i].id));
    }

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_add_style(scr, &ui_style_screen, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* ── Widgets ─────────────────────────────────────────────────────────── */

    ui_header_create(scr, "DV SETTINGS", status_subjects);
    build_list(scr);
    build_buttons(scr);

    /* ── Input groups ────────────────────────────────────────────────────── */

    /*
     * The encoder group holds every row and stays out of edit mode, so a turn
     * of the encoder moves the focus down the list. The two button groups have
     * one member each and stay in edit mode, so a press of a pad always clicks
     * its button.
     */
    s_right_encoder_group = lv_group_create();
    for (uint8_t i = 0U; i < (uint8_t)ROW_COUNT; i++) {
        lv_group_add_obj(s_right_encoder_group, s_rows[i]);
    }
    lv_group_set_editing(s_right_encoder_group, false);
    if (s_sel_idx < (uint8_t)ROW_COUNT) {
        lv_group_focus_obj(s_rows[s_sel_idx]);
    }

    s_left_button_group = lv_group_create();
    lv_group_add_obj(s_left_button_group, s_btn_minus);
    lv_group_set_editing(s_left_button_group, true);

    s_right_button_group = lv_group_create();
    lv_group_add_obj(s_right_button_group, s_btn_plus);
    lv_group_set_editing(s_right_button_group, true);

    ui_hintbar_create(scr, k_hints);

    return scr;
}

lv_group_t *screen_settings_get_right_encoder_group(void)
{
    return s_right_encoder_group;
}

lv_group_t *screen_settings_get_left_button_group(void)
{
    return s_left_button_group;
}

lv_group_t *screen_settings_get_right_button_group(void)
{
    return s_right_button_group;
}
