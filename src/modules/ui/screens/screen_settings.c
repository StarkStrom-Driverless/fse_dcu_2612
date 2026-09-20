/**
 * @file
 * @brief       Settings screen implementation
 *
 * @ingroup     dcu_ui_screens
 *
 * @details     A slider-per-setting editor for the values in the generated
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
 *              SETTING_DEBUG_BITS is deliberately absent: the debug bits are
 *              edited on DBG CUSTOM, which is the screen that also shows what
 *              each bit means. Two editors for one value were confusing, and
 *              this is the screen the driver sees.
 *
 *              ### Focus instead of a roller
 *              The other input screens put a single roller in the encoder
 *              group and force edit mode. Here the group holds every slider and
 *              stays in navigate mode, so a turn of the encoder walks the focus
 *              down the list. The focused slider carries LV_STATE_FOCUS_KEY and
 *              picks up ui_style_slider_focused (gold indicator); the rest keep
 *              the green one.
 *
 *              ### Why the group is filled bottom-up
 *              LVGL walks a group in insertion order, and the right encoder
 *              reports a clockwise turn as lv_group_focus_next(). Wired up the
 *              obvious way the focus therefore ran *against* the direction the
 *              encoder was turned. Inserting the sliders in reverse puts the
 *              visual order back in step with the encoder without touching the
 *              indev driver, which the roller screens rely on.
 *
 *              ### Applying a change
 *              adjust_focused() clamps against settings_schema[id] and
 *              publishes UI_INPUT_SETTING_SELECTED. The row subject — which the
 *              value label is bound to — and the slider position are only
 *              updated once the publish succeeded, so a dropped event cannot
 *              leave the display showing a value the settings service never
 *              received. The settings service clamps again and is the real
 *              owner; the clamp here only keeps the screen from offering a
 *              value that would be rejected.
 *
 *              ### "Saved" notice
 *              The settings service coalesces flash writes behind a delay, so a
 *              change is not persistent the moment it is made. When the write
 *              lands, the service publishes SETTINGS_EVT_SAVED, the App Layer
 *              forwards it as UI_CMD_SETTINGS_SAVED and ui.c calls
 *              screen_settings_notify_saved() — but only while this screen is
 *              the active one. The notice then shows for SAVED_NOTICE_MS.
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
 * @copyright   Copyright (c) 2026 Mario Wegmann.
 *              SPDX-License-Identifier: Apache-2.0
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

#include "modules/ui/ui.h"
#include "modules/ui/ui_layout.h"
#include "modules/ui/ui_styles.h"
#include "modules/ui/widgets/ui_header.h"
#include "modules/ui/widgets/ui_hintbar.h"
#include "services/event_bus/event_bus.h"
#include "services/event_bus/events.h"
#include "services/settings/settings.h"

/* ── Zephyr Logging ──────────────────────────────────────────────────────────────────────────── */

LOG_MODULE_REGISTER(screen_settings, CONFIG_LOG_DEFAULT_LEVEL);


/* ── Private Macros & Constants ──────────────────────────────────────────────────────────────── */

/** @brief Height of one row, in layout units (see ui_layout_u()). */
#define ROW_H_U                 3

/** @brief Share of a row's width taken by the caption, in percent. */
#define ROW_LABEL_PCT           40

/** @brief Width of the value column, in layout units — room for "100". */
#define ROW_VALUE_W_U           4

/** @brief Gap between caption, slider and value, in layout units. */
#define ROW_PAD_COL_U           1

/** @brief Height of the slider track, in layout units. */
#define SLIDER_H_U              1

/** @brief Width of the "−" / "+" buttons, in layout units. */
#define ADJ_BTN_W_U             5

/** @brief Height of the "−" / "+" buttons, in layout units. */
#define ADJ_BTN_H_U             5

/** @brief Space between a button and the row list, in layout units. */
#define ADJ_BTN_GAP_U           1

/** @brief How long the "saved" notice stays up, in milliseconds. */
#define SAVED_NOTICE_MS         1500

/**
 * @brief Distance of the "saved" notice above the hint bar, in layout units.
 *
 * The notice is the one thing on the screen that is not part of the flow — it
 * comes and goes, and a hidden object taking space would move the list.
 */
#define SAVED_NOTICE_GAP_U      1


/* ── Row Table ───────────────────────────────────────────────────────────────────────────────── */

/**
 * @brief One editable row: a schema setting, its caption and its step size.
 *
 * The bounds and the default are not repeated here — they live in
 * settings_schema[id]. Keep the order aligned with enum setting_id so the list
 * reads in schema order, though nothing enforces it.
 *
 * SETTING_DEBUG_BITS is edited on DBG CUSTOM and left out on purpose.
 */
static const struct {
    enum setting_id id;    /**< Which schema entry this row edits.            */
    const char     *label; /**< Caption shown on the left of the row.         */
    uint8_t         step;  /**< Value change per "−" / "+" press.             */
} k_rows[] = {
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
 * Initialized once, guarded by s_subjects_init; re-seeded from settings_get()
 * on every build. File-scope because the labels bind to them while the screen
 * is constructed and LVGL keeps the binding until the screen is destroyed.
 */
static lv_subject_t s_value[ROW_COUNT];

/** @brief Guard so the subjects above are initialized exactly once. */
static bool         s_subjects_init;

/**
 * @brief Index of the focused row — survives screen destroy/recreate.
 *
 * Written by row_focused_cb() and read in the factory to restore the focus on
 * the next visit.
 */
static uint8_t      s_sel_idx;

/** @brief Slider objects, indexed like k_rows[]. Rebuilt on every visit. */
static lv_obj_t   *s_sliders[ROW_COUNT];

/** @brief "−" button — lowers the focused setting. */
static lv_obj_t   *s_btn_minus;

/** @brief "+" button — raises the focused setting. */
static lv_obj_t   *s_btn_plus;

/** @brief "Saved" notice, hidden until the settings reach the flash. */
static lv_obj_t   *s_lbl_saved;

/** @brief One-shot timer that takes the notice back down. Paused while idle. */
static lv_timer_t *s_saved_timer;

/** @brief Input group for the right encoder — holds every slider, navigate mode. */
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
    [UI_HINT_ENC_LEFT]  = "Switch Screen",
    [UI_HINT_BTN_LEFT]  = "-",
    [UI_HINT_BTN_RIGHT] = "+",
    [UI_HINT_ENC_RIGHT] = "Select Setting",
};

/* ── Private Function Prototypes ─────────────────────────────────────────────────────────────── */

static void build_body(lv_obj_t *scr);
static void build_saved_notice(lv_obj_t *scr);
static void row_focused_cb(lv_event_t *e);
static void btn_minus_event_cb(lv_event_t *e);
static void btn_plus_event_cb(lv_event_t *e);
static void screen_deleted_cb(lv_event_t *e);
static void saved_timer_cb(lv_timer_t *timer);
static void adjust_focused(int direction);


/* ── Private Function Implementations ───────────────────────────────────────────────────────── */

/**
 * @brief Remember which row is focused so the next visit can restore it.
 *
 * @param e  LV_EVENT_FOCUSED from a slider; its user data is the row index.
 */
static void row_focused_cb(lv_event_t *e)
{
    s_sel_idx = (uint8_t)(uintptr_t)lv_obj_get_user_data(lv_event_get_target_obj(e));
}

/**
 * @brief Build one row: caption, slider and value label in a flex row.
 *
 * The value label binds straight to s_value[i], so it follows the subject
 * without a per-row callback. The slider is the focusable part — it is what
 * goes into the encoder group, and its user data carries the row index.
 *
 * @param list  Row-list container.
 * @param i     Row index into k_rows[].
 */
static void build_row(lv_obj_t *list, uint8_t i)
{
    const setting_desc_t *desc = &settings_schema[k_rows[i].id];

    lv_obj_t *row = lv_obj_create(list);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), ui_layout_u(ROW_H_U));
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, ui_layout_u(ROW_PAD_COL_U), 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    /* Caption — a fixed share of the row, so every slider starts at the same x. */
    lv_obj_t *lbl_name = lv_label_create(row);
    lv_obj_add_style(lbl_name, &ui_style_label_subtitle, 0);
    lv_obj_set_width(lbl_name, lv_pct(ROW_LABEL_PCT));
    lv_label_set_text(lbl_name, k_rows[i].label);

    /*
     * The slider is read-only as far as touch goes — the "−" / "+" buttons are
     * the only way to move it — but it stays clickable because an object has to
     * be to join an input group and take LV_STATE_FOCUS_KEY. The knob is made
     * invisible: there is nothing to drag, and the bar alone reads better at
     * this height.
     */
    lv_obj_t *slider = lv_slider_create(row);
    lv_obj_remove_style_all(slider);
    lv_obj_add_style(slider, &ui_style_slider_main, LV_PART_MAIN);
    lv_obj_add_style(slider, &ui_style_slider_indicator, LV_PART_INDICATOR);
    lv_obj_add_style(slider, &ui_style_slider_focused,
                     LV_PART_INDICATOR | LV_STATE_FOCUS_KEY);
    lv_obj_set_style_bg_opa(slider, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_flex_grow(slider, 1);
    lv_obj_set_height(slider, ui_layout_u(SLIDER_H_U));
    lv_slider_set_range(slider, (int32_t)desc->min, (int32_t)desc->max);
    lv_slider_set_value(slider, lv_subject_get_int(&s_value[i]), LV_ANIM_OFF);
    lv_obj_set_user_data(slider, (void *)(uintptr_t)i);
    lv_obj_add_event_cb(slider, row_focused_cb, LV_EVENT_FOCUSED, NULL);

    /* Value hard against the right edge. */
    lv_obj_t *lbl_value = lv_label_create(row);
    lv_obj_add_style(lbl_value, &ui_style_label_subtitle, 0);
    lv_obj_set_width(lbl_value, ui_layout_u(ROW_VALUE_W_U));
    lv_obj_set_style_text_align(lbl_value, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_bind_text(lbl_value, &s_value[i], "%d");

    s_sliders[i] = slider;
}

/**
 * @brief Build the row list — one row per k_rows[] entry, in a flex column.
 *
 * Takes the width the two buttons leave and is as tall as its rows. The gap to
 * the buttons is padding, not margin: a flex item that grows does not count its
 * margin, and the list would push the "+" button off the screen.
 *
 * @param content  Content area to build into.
 */
static void build_list(lv_obj_t *content)
{
    lv_obj_t *list = lv_obj_create(content);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, 0, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(list, 1);
    lv_obj_set_style_pad_hor(list, ui_layout_u(ADJ_BTN_GAP_U), 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(list, LV_OBJ_FLAG_SCROLLABLE);

    for (uint8_t i = 0U; i < (uint8_t)ROW_COUNT; i++) {
        build_row(list, i);
    }
}

/**
 * @brief Build one edit button.
 *
 * @param parent  Content area to build into.
 * @param text    Button caption ("−" or "+").
 * @param cb      Click handler.
 * @return        The button object.
 */
static lv_obj_t *build_button(lv_obj_t *parent, const char *text, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_add_style(btn, &ui_style_btn_default, 0);
    lv_obj_add_style(btn, &ui_style_btn_checked, LV_STATE_PRESSED);
    lv_obj_add_style(btn, &ui_style_btn_focused, LV_STATE_FOCUS_KEY);
    lv_obj_set_size(btn, ui_layout_u(ADJ_BTN_W_U), ui_layout_u(ADJ_BTN_H_U));

    lv_obj_t *lbl = lv_label_create(btn);
    lv_obj_add_style(lbl, &ui_style_label_title, 0);
    lv_label_set_text(lbl, text);
    lv_obj_center(lbl);

    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    return btn;
}

/**
 * @brief Build the content: "−" button, row list, "+" button, side by side.
 *
 * The row list starts at the top of the content area; the buttons are centered
 * on its height. The list takes the width that is left between them.
 *
 * @param scr  Screen object to build into.
 */
static void build_body(lv_obj_t *scr)
{
    lv_obj_t *content = ui_layout_content_create(scr);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_START);

    s_btn_minus = build_button(content, "-", btn_minus_event_cb);
    build_list(content);
    s_btn_plus  = build_button(content, "+", btn_plus_event_cb);
}

/**
 * @brief Build the hidden "saved" notice and the timer that hides it again.
 *
 * The timer is created paused rather than on demand, so
 * screen_settings_notify_saved() only has to reset and resume it and there is
 * exactly one place that owns the timer's lifetime — screen_deleted_cb().
 *
 * @param scr  Screen object to build into.
 */
static void build_saved_notice(lv_obj_t *scr)
{
    s_lbl_saved = lv_label_create(scr);
    lv_obj_add_style(s_lbl_saved, &ui_style_label_subtitle, 0);
    lv_obj_set_style_text_color(s_lbl_saved, UI_C_GREEN, 0);
    lv_label_set_text(s_lbl_saved, "Settings saved");
    lv_obj_align(s_lbl_saved, LV_ALIGN_BOTTOM_MID, 0,
                 -(ui_layout_hintbar_h() + ui_layout_u(SAVED_NOTICE_GAP_U)));
    lv_obj_add_flag(s_lbl_saved, LV_OBJ_FLAG_HIDDEN);

    s_saved_timer = lv_timer_create(saved_timer_cb, SAVED_NOTICE_MS, NULL);
    lv_timer_pause(s_saved_timer);
}

/**
 * @brief Timer callback — take the "saved" notice back down.
 *
 * @param timer  Unused.
 */
static void saved_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    if (s_lbl_saved != NULL) {
        lv_obj_add_flag(s_lbl_saved, LV_OBJ_FLAG_HIDDEN);
    }
    lv_timer_pause(s_saved_timer);
}

/**
 * @brief LV_EVENT_DELETE handler — drop the timer with the screen.
 *
 * The label dies with the screen; the timer does not and would otherwise fire
 * into freed memory. Nulling both pointers also makes
 * screen_settings_notify_saved() a no-op while the screen is gone.
 *
 * @param e  Unused.
 */
static void screen_deleted_cb(lv_event_t *e)
{
    (void)e;

    if (s_saved_timer != NULL) {
        lv_timer_delete(s_saved_timer);
        s_saved_timer = NULL;
    }
    s_lbl_saved = NULL;
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
 * only on a successful publish, updates the row subject and the slider so the
 * display follows. A press that would leave the value unchanged (already at a
 * bound, or the focus lost) does nothing.
 *
 * @param direction  -1 to lower, +1 to raise.
 */
static void adjust_focused(int direction)
{
    lv_obj_t *slider = lv_group_get_focused(s_right_encoder_group);
    if (slider == NULL) {
        return;
    }

    uint8_t               i    = (uint8_t)(uintptr_t)lv_obj_get_user_data(slider);
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
        lv_slider_set_value(slider, next, LV_ANIM_OFF);
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
     * This runs before build_body(), which reads the subjects to place the
     * sliders.
     */
    for (uint8_t i = 0U; i < (uint8_t)ROW_COUNT; i++) {
        lv_subject_set_int(&s_value[i], (int32_t)settings_get(k_rows[i].id));
    }

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_add_style(scr, &ui_style_screen, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(scr, screen_deleted_cb, LV_EVENT_DELETE, NULL);

    /* ── Widgets ─────────────────────────────────────────────────────────── */

    ui_header_create(scr, "SETTINGS", status_subjects);
    build_body(scr);
    build_saved_notice(scr);

    /* ── Input groups ────────────────────────────────────────────────────── */

    /*
     * The encoder group holds every slider and stays out of edit mode, so a
     * turn of the encoder moves the focus down the list. The two button groups
     * have one member each and stay in edit mode, so a press of a pad always
     * clicks its button.
     *
     * Bottom-up insertion: lv_group_focus_next() is what a clockwise turn ends
     * up calling, and with top-down insertion that walked the list the wrong
     * way round. See the file header.
     */
    /*
     * Take a copy first: lv_group_add_obj() focuses the very first member it is
     * given, which fires row_focused_cb() and overwrites s_sel_idx before the
     * restore below could use it.
     */
    uint8_t restore_idx = s_sel_idx;

    s_right_encoder_group = ui_group_create(scr);
    for (uint8_t i = (uint8_t)ROW_COUNT; i > 0U; i--) {
        lv_group_add_obj(s_right_encoder_group, s_sliders[i - 1U]);
    }
    lv_group_set_editing(s_right_encoder_group, false);
    if (restore_idx < (uint8_t)ROW_COUNT) {
        lv_group_focus_obj(s_sliders[restore_idx]);
    }

    s_left_button_group = ui_group_create(scr);
    lv_group_add_obj(s_left_button_group, s_btn_minus);
    lv_group_set_editing(s_left_button_group, true);

    s_right_button_group = ui_group_create(scr);
    lv_group_add_obj(s_right_button_group, s_btn_plus);
    lv_group_set_editing(s_right_button_group, true);

    ui_hintbar_create(scr, k_hints);

    return scr;
}

void screen_settings_notify_saved(void)
{
    if ((s_lbl_saved == NULL) || (s_saved_timer == NULL)) {
        return;   /* screen not built, or already destroyed */
    }

    lv_obj_clear_flag(s_lbl_saved, LV_OBJ_FLAG_HIDDEN);
    lv_timer_reset(s_saved_timer);
    lv_timer_resume(s_saved_timer);
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
