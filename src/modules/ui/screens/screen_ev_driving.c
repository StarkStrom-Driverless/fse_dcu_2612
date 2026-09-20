/**
 * @file        screen_ev_driving.c
 * @brief       EV driving screen implementation
 *
 * @ingroup     dcu_ui_screens
 *
 * @details     Builds the EV driving screen from three columns in the content
 *              area (ui_layout.h), left to right:
 *
 *                – Left column   : the torque-gain front slider (left encoder).
 *
 *                – Middle column : as wide as the sliders leave it, with three
 *                                  blocks spread over its height:
 *                                    · three readouts — HV accumulator,
 *                                      inverter and motor temperature, bound to
 *                                      their signal descriptors through the
 *                                      ui_quantity widget;
 *                                    · the HV accumulator voltage as a bar. A
 *                                      bar rather than a slider because the
 *                                      value comes from the vehicle — there is
 *                                      nothing for the driver to set;
 *                                    · the two buttons below.
 *
 *                – Right column  : the torque-gain rear slider (right encoder).
 *
 *              Nothing is placed by a coordinate. The columns and rows lay their
 *              children out, widths are shares of the space that is there, and
 *              sizes that have to be numbers are in layout units.
 *
 *                – PWR Limit     : checkable button mirroring
 *                                  ui_tx_subj_pwrlimit_setting.
 *
 *                – TQ Vect       : checkable button mirroring
 *                                  ui_tx_subj_torquevect_setting.
 *
 *              The two buttons are built by one function and share one observer
 *              callback — they differ only in caption and in which setting they
 *              stand for. Both publish UI_INPUT_SETTING_SELECTED, the same event
 *              the settings screen sends. The settings service owns the value,
 *              persists it and the CAN module transmits it — the two screens
 *              are two ways to the same store, so they cannot disagree.
 *
 *              ### On/off out of a graded setting
 *              Neither setting is a boolean in the schema: power limit runs 0…7
 *              and torque vectoring 0…3. This screen is the one the driver uses
 *              at speed, so it offers only the two states that matter there —
 *              any non-zero value reads as ON, switching on writes 1 and
 *              switching off writes 0. A level set on DV SETTINGS therefore
 *              survives until the button is used, and is then flattened. Pick
 *              the level on DV SETTINGS, use the button while driving.
 *
 *              ### Buttons follow the value, not the press
 *              Each button observes its TX subject, and the click handler only
 *              writes to that subject. The visual state therefore reflects the
 *              stored value rather than the last press — the same pattern the
 *              other screens use for their confirmed-value labels. A press that
 *              cannot be published snaps the button back, see publish_setting().
 *
 *              ### What is not wired up
 *              The torque-gain sliders keep their positions across visits, in
 *              s_sldr_left_val and s_sldr_right_val, and both are now movable —
 *              TQG F on the left encoder, TQG R on the right — but nothing
 *              reads those subjects yet: the values reach neither a setting nor
 *              a CAN signal.
 *
 * @author      Mario Wegmann <mario.wegmann@web.de>
 * @date        Created: 2026-06-15
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
 * 0.1.0    2026-06-15  Mario Wegmann   Initial creation
 * 0.2.0    2026-09-20  Mario Wegmann   Layout without coordinates: columns, rows and
 *                                      layout units instead of pixel positions
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
#include "modules/ui/ui_layout.h"
#include "modules/ui/ui_styles.h"
#include "modules/ui/widgets/ui_header.h"
#include "modules/ui/widgets/ui_hintbar.h"
#include "modules/ui/widgets/ui_quantity.h"
#include "services/event_bus/event_bus.h"
#include "services/event_bus/events.h"
#include "services/settings/settings.h"
#include "generated/ui_subjects_gen.h"
#include "generated/ui_tx_subjects_gen.h"

/* ── Zephyr Logging ──────────────────────────────────────────────────────────────────────────── */

LOG_MODULE_REGISTER(screen_ev_driving, CONFIG_LOG_DEFAULT_LEVEL);


/* ── Private Macros & Constants ──────────────────────────────────────────────────────────────── */

/** @brief Width of the torque-gain sliders, in layout units (see ui_layout_u()). */
#define SLIDER_W_U          4

/** @brief Height of the HV bar, in layout units. */
#define BAR_H_U             2

/** @brief Width of an on/off button, in layout units. */
#define BTN_W_U             10

/** @brief Height of an on/off button, in layout units. */
#define BTN_H_U             5

/**
 * @brief How far the ON/OFF caption hangs below a button's content area, in pixels.
 *
 * The button's own title sits top left, the caption bottom left. The caption's
 * line box is a few pixels taller than its glyphs, so it is pushed down by this
 * much for the glyphs, not the box, to end at the bottom edge. A font metric,
 * not a layout decision.
 */
#define BTN_VALUE_DROP      8


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

/** @brief Torque gain front — vertical slider in the left column, on the left encoder. */
static lv_obj_t   *s_sldr_left;

/** @brief Torque gain rear — vertical slider in the right column, on the right encoder. */
static lv_obj_t   *s_sldr_right;

/** @brief TQ Vect button — toggles torque vectoring. */
static lv_obj_t   *s_btn_right;

/** @brief PWR Limit button — toggles the power limit. */
static lv_obj_t   *s_btn_left;

/** @brief Input group for the right encoder — holds the TQG R slider. */
static lv_group_t *s_right_encoder_group;

/** @brief Input group for the left encoder — holds the TQG F slider. */
static lv_group_t *s_left_encoder_group;

/** @brief Input group for the left button pad — holds the PWR Limit button. */
static lv_group_t *s_left_button_group;

/** @brief Input group for the right button pad — holds the TQ Vect button. */
static lv_group_t *s_right_button_group;


/**
 * @brief What each control does on this screen; see @ref ui_hint_input.
 *
 * Static storage: ui_hintbar_create() keeps the pointers rather than copying
 * the strings. Controls left out here are dimmed in the bar.
 */
static const char *const k_hints[UI_HINT_INPUT_COUNT] = {
    [UI_HINT_ENC_LEFT]  = "TQ Gain Front",
    [UI_HINT_BTN_LEFT]  = "PWR Limit",
    [UI_HINT_BTN_RIGHT] = "TQ Vect",
    [UI_HINT_ENC_RIGHT] = "TQ Gain Rear",
};

/* ── Private Function Prototypes ─────────────────────────────────────────────────────────────── */
static lv_obj_t *build_slider_column(lv_obj_t *content, const char *title,
                                     lv_subject_t *value, lv_event_cb_t cb);
static void build_middle(lv_obj_t *content);
static void build_temp(lv_obj_t *parent, const struct ui_signal_desc *desc);
static void build_hv_bar(lv_obj_t *parent);
static void build_buttons(lv_obj_t *parent);
static lv_obj_t *build_toggle_button(lv_obj_t *parent, const char *title,
                                     lv_event_cb_t cb, lv_subject_t *subject);
static void publish_setting(enum setting_id id, lv_subject_t *subject, bool on);
static void btn_left_event_cb(lv_event_t *e);
static void btn_right_event_cb(lv_event_t *e);
static void toggle_observer_cb(lv_observer_t *observer, lv_subject_t *subject);


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
 * @brief Apply a stored on/off setting to its toggle button.
 *
 * Shared by both buttons: sets the checked state and the ON/OFF caption, and
 * recolors the caption because the checked style paints the button green and
 * dark text would disappear on it.
 *
 * The caption is found through the button's user data — the observer is handed
 * the button and nothing else, and storing the label there keeps one callback
 * for both buttons. See build_toggle_button().
 *
 * @param observer  Observer whose target object is the button.
 * @param subject   The button's TX subject.
 */
static void toggle_observer_cb(lv_observer_t *observer, lv_subject_t *subject)
{
    lv_obj_t *btn = lv_observer_get_target_obj(observer);
    lv_obj_t *lbl = lv_obj_get_user_data(btn);
    bool      on  = lv_subject_get_int(subject) != 0;

    lv_obj_set_state(btn, LV_STATE_CHECKED, on);
    lv_label_set_text(lbl, on ? "ON" : "OFF");
    lv_obj_set_style_text_color(lbl, on ? UI_C_WHITE : UI_C_DARK, 0);
}

/**
 * @brief Build one torque-gain column: the title over a vertical slider.
 *
 * The column is as wide as the slider and as tall as the content area; the
 * slider takes what the title leaves. The slider is restored from its subject,
 * so it comes back where the driver left it.
 *
 * The column keeps an outline's width free at the sides and the bottom: the
 * slider's outline is drawn outside its box and the column clips it otherwise.
 *
 * @param content  Content area to build into.
 * @param title    Caption over the slider.
 * @param value    Subject the slider position survives in.
 * @param cb       Handler that mirrors the slider into @p value.
 * @return         The slider, for the caller to put into its input group.
 */
static lv_obj_t *build_slider_column(lv_obj_t *content, const char *title,
                                     lv_subject_t *value, lv_event_cb_t cb)
{
    lv_obj_t *col = lv_obj_create(content);
    lv_obj_remove_style_all(col);
    lv_obj_set_size(col, LV_SIZE_CONTENT, lv_pct(100));
    lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_pad_hor(col, UI_SLIDER_OUTLINE_W, 0);
    lv_obj_set_style_pad_bottom(col, UI_SLIDER_OUTLINE_W, 0);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t *lbl_title = lv_label_create(col);
    lv_obj_add_style(lbl_title, &ui_style_label_subtitle, 0);
    lv_label_set_text(lbl_title, title);

    lv_obj_t *slider = lv_slider_create(col);
    lv_obj_remove_style_all(slider);
    lv_obj_add_style(slider, &ui_style_slider_main, LV_PART_MAIN);
    lv_obj_add_style(slider, &ui_style_slider_indicator, LV_PART_INDICATOR);
    lv_obj_set_size(slider, ui_layout_u(SLIDER_W_U), 0);
    lv_obj_set_flex_grow(slider, 1);

    lv_slider_set_value(slider, (int32_t)lv_subject_get_int(value), LV_ANIM_OFF);
    lv_obj_add_event_cb(slider, cb, LV_EVENT_VALUE_CHANGED, NULL);

    return slider;
}

/**
 * @brief Build the middle column: temperatures, HV bar and the two buttons.
 *
 * Takes the width the two slider columns leave. The three blocks are spread
 * over its height, the first at the top and the last at the bottom, so nothing
 * is placed by a coordinate and the gaps follow the space that is there.
 *
 * @param content  Content area to build into.
 */
static void build_middle(lv_obj_t *content)
{
    lv_obj_t *mid = lv_obj_create(content);
    lv_obj_remove_style_all(mid);
    lv_obj_set_size(mid, 0, lv_pct(100));
    lv_obj_set_flex_grow(mid, 1);
    lv_obj_clear_flag(mid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(mid, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(mid, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(mid, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);

    /* A little air between the buttons and the hint bar. */
    lv_obj_set_style_pad_bottom(mid, ui_layout_u(1) / 2, 0);

    /* Row of the three temperatures. */
    lv_obj_t *temps = lv_obj_create(mid);
    lv_obj_remove_style_all(temps);
    lv_obj_set_size(temps, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_clear_flag(temps, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(temps, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(temps, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(temps, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);

    build_temp(temps, &ui_sig_temperature_accu_hv);
    build_temp(temps, &ui_sig_temperature_inverter);
    build_temp(temps, &ui_sig_temperature_motor);

    build_hv_bar(mid);
    build_buttons(mid);
}

/**
 * @brief Build one temperature readout: the short caption over a large value.
 *
 * The caption is centred over the value. The value is a ui_quantity bound to
 * the signal, so unit, format and limit coloring come from the descriptor.
 *
 * @param parent  Row to build into.
 * @param desc    The temperature signal to show.
 */
static void build_temp(lv_obj_t *parent, const struct ui_signal_desc *desc)
{
    lv_obj_t *item = lv_obj_create(parent);
    lv_obj_remove_style_all(item);
    lv_obj_set_size(item, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(item, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(item, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(item, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t *lbl_title = lv_label_create(item);
    lv_obj_add_style(lbl_title, &ui_style_label_subtitle, 0);
    lv_label_set_text(lbl_title, desc->short_label);

    lv_obj_t *qty = ui_quantity_create(item,
                                       &BarlowCondensed_BoldItalic_80,
                                       &BarlowCondensed_Italic_44, desc->unit);
    ui_quantity_bind_signal(qty, desc);
}

/**
 * @brief Build the HV accumulator voltage: caption and value over a bar.
 *
 * The caption stands at the left and the value at the right, both on the line
 * above the bar, which spans the full width. The bar is an lv_bar, not a
 * slider: the value comes from the vehicle and there is nothing here for the
 * driver to set, so it has no knob and no input handling.
 *
 * The block keeps an outline's width free at the sides and the bottom for the
 * bar's outline, which the container would otherwise clip.
 *
 * @param parent  Column to build into.
 */
static void build_hv_bar(lv_obj_t *parent)
{
    const struct ui_signal_desc *hv = &ui_sig_voltage_accu_hv;

    lv_obj_t *block = lv_obj_create(parent);
    lv_obj_remove_style_all(block);
    lv_obj_set_size(block, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_clear_flag(block, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(block, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_pad_hor(block, UI_SLIDER_OUTLINE_W, 0);
    lv_obj_set_style_pad_bottom(block, UI_SLIDER_OUTLINE_W, 0);
    lv_obj_set_flex_flow(block, LV_FLEX_FLOW_COLUMN);

    /* Caption at the left, value at the right, standing on the bar. */
    lv_obj_t *head = lv_obj_create(block);
    lv_obj_remove_style_all(head);
    lv_obj_set_size(head, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_clear_flag(head, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(head, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_END,
                          LV_FLEX_ALIGN_END);

    lv_obj_t *lbl_title = lv_label_create(head);
    lv_obj_add_style(lbl_title, &ui_style_label_subtitle, 0);
    lv_label_set_text(lbl_title, hv->label);

    lv_obj_t *qty = ui_quantity_create(head,
                                       &BarlowCondensed_BoldItalic_32,
                                       &BarlowCondensed_Italic_20, hv->unit);
    ui_quantity_bind_signal(qty, hv);

    /*
     * Styled with the shared slider styles like every other bar in the tree:
     * ui_style_slider_main paints the track, ui_style_slider_indicator the
     * fill. The names are about the visual role, not the widget type.
     */
    lv_obj_t *bar = lv_bar_create(block);
    lv_obj_remove_style_all(bar);
    lv_obj_add_style(bar, &ui_style_slider_main, LV_PART_MAIN);
    lv_obj_add_style(bar, &ui_style_slider_indicator, LV_PART_INDICATOR);
    if (hv->flags & UI_SIG_RANGE) {
        lv_bar_set_range(bar, (int32_t)hv->range_min, (int32_t)hv->range_max);
    }
    lv_bar_bind_value(bar, hv->subject);
    lv_obj_set_size(bar, lv_pct(100), ui_layout_u(BAR_H_U));
}

/**
 * @brief Build one on/off button and subscribe it to its setting.
 *
 * Both buttons on this screen are the same thing with a different caption and
 * a different setting behind them: a checkable button showing its title in the
 * top left and ON/OFF underneath, green while on.
 *
 * No focus style is added. Each button is alone in its own group and that group
 * is in edit mode, so it carries LV_STATE_FOCUS_KEY permanently — a focus style
 * would outrank the checked style and the button would sit there gold whatever
 * the setting says. The focus is not information here; which pad was pressed
 * is never in doubt.
 *
 * @param parent   Row to build into; it places the button.
 * @param title    Caption in the top left of the button.
 * @param cb       LV_EVENT_VALUE_CHANGED handler.
 * @param subject  TX subject the button mirrors.
 * @return         The button object.
 */
static lv_obj_t *build_toggle_button(lv_obj_t *parent, const char *title,
                                     lv_event_cb_t cb, lv_subject_t *subject)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_add_style(btn, &ui_style_btn_default, 0);
    lv_obj_add_style(btn, &ui_style_btn_checked, LV_STATE_CHECKED);
    lv_obj_set_size(btn, ui_layout_u(BTN_W_U), ui_layout_u(BTN_H_U));
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CHECKABLE);

    lv_obj_t *lbl_title = lv_label_create(btn);
    lv_obj_add_style(lbl_title, &ui_style_label_subtitle, 0);
    lv_label_set_text(lbl_title, title);
    lv_obj_align(lbl_title, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *lbl_value = lv_label_create(btn);
    lv_obj_add_style(lbl_value, &ui_style_label_title, 0);
    lv_label_set_text(lbl_value, "OFF");
    lv_obj_align(lbl_value, LV_ALIGN_BOTTOM_LEFT, 0, BTN_VALUE_DROP);

    /* The shared observer is handed the button only; this is how it finds the caption. */
    lv_obj_set_user_data(btn, lbl_value);

    lv_obj_add_event_cb(btn, cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* Fires once on subscription, so the caption starts out correct. */
    lv_subject_add_observer_obj(subject, toggle_observer_cb, btn, NULL);

    return btn;
}

/**
 * @brief Build the PWR Limit and TQ Vect buttons and subscribe them to their subjects.
 *
 * Two buttons in a row, spread evenly over its width. The row keeps an
 * outline's width free above and below: the buttons' outline is drawn outside
 * their boxes and the row clips it otherwise.
 *
 * @param parent  Column to build into.
 */
static void build_buttons(lv_obj_t *parent)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_pad_ver(row, UI_BTN_OUTLINE_W, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);

    s_btn_left  = build_toggle_button(row, "PWR Limit", btn_left_event_cb,
                                      &ui_tx_subj_pwrlimit_setting);

    s_btn_right = build_toggle_button(row, "TQ Vect", btn_right_event_cb,
                                      &ui_tx_subj_torquevect_setting);
}

/**
 * @brief Hand a setting change to the App Layer and mirror it locally.
 *
 * The subject is written only once the publish succeeded, so a dropped event
 * cannot leave a button claiming a state the settings service never received.
 *
 * LVGL has already flipped the button's checked state by the time this runs —
 * that is what produced the event. On a failed publish the subject therefore
 * still holds the old value while the button shows the new one, so the subject
 * is re-notified and the observer snaps the button back.
 *
 * @param id       Setting to change.
 * @param subject  TX subject the button observes.
 * @param on       New state.
 */
static void publish_setting(enum setting_id id, lv_subject_t *subject, bool on)
{
    struct ui_input_event evt = {
        .type             = UI_INPUT_SETTING_SELECTED,
        .data.setting.id  = id,
        .data.setting.val = on ? 1U : 0U,
    };

    int ret = zbus_chan_pub(&ui_input_chan, &evt, K_NO_WAIT);
    if (ret != 0) {
        LOG_WRN("UI_INPUT_SETTING_SELECTED (%d) publish failed: %d", (int)id, ret);
        lv_subject_notify(subject);
        return;
    }

    lv_subject_set_int(subject, on ? 1 : 0);
    LOG_DBG("Setting %d → %s", (int)id, on ? "ON" : "OFF");
}

/**
 * @brief PWR Limit toggle handler — store the new power-limit state.
 * @param e  LV_EVENT_VALUE_CHANGED from the checkable button.
 */
static void btn_left_event_cb(lv_event_t *e)
{
    bool on = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);

    publish_setting(SETTING_PWRLIMIT_SETTING, &ui_tx_subj_pwrlimit_setting, on);
}

/**
 * @brief TQ Vect toggle handler — store the new torque-vectoring state.
 * @param e  LV_EVENT_VALUE_CHANGED from the checkable button.
 */
static void btn_right_event_cb(lv_event_t *e)
{
    bool on = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);

    publish_setting(SETTING_TORQUEVECT_SETTING, &ui_tx_subj_torquevect_setting, on);
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

    /*
     * Seed the two button subjects from the settings service, which owns the
     * values: the generated TX subjects start at 0 and know nothing about what
     * is stored, and the same settings can have been changed meanwhile on the
     * settings screen.
     */
    lv_subject_set_int(&ui_tx_subj_pwrlimit_setting,
                       (int32_t)settings_get(SETTING_PWRLIMIT_SETTING));
    lv_subject_set_int(&ui_tx_subj_torquevect_setting,
                       (int32_t)settings_get(SETTING_TORQUEVECT_SETTING));

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_add_style(scr, &ui_style_screen, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* ── Widgets ─────────────────────────────────────────────────────────── */

    ui_header_create(scr, "EV DRIVING", status_subjects);

    /* TQG F | temperatures, HV bar, buttons | TQG R */
    lv_obj_t *content = ui_layout_content_create(scr);
    lv_obj_set_style_pad_column(content, ui_layout_u(1), 0);

    s_sldr_left  = build_slider_column(content, "TQG F", &s_sldr_left_val,
                                       sldr_left_value_changed_cb);
    build_middle(content);
    s_sldr_right = build_slider_column(content, "TQG R", &s_sldr_right_val,
                                       sldr_right_value_changed_cb);

    s_right_encoder_group = lv_group_create();
    lv_group_add_obj(s_right_encoder_group, s_sldr_right);
    lv_group_set_editing(s_right_encoder_group, true);

    /*
     * The left encoder drives TQG F here instead of the screen carousel — ui.c
     * routes it to this group and, seeing a non-NULL group, stops feeding the
     * carousel. That is what makes EV driving a navigation dead end.
     */
    s_left_encoder_group = lv_group_create();
    lv_group_add_obj(s_left_encoder_group, s_sldr_left);
    lv_group_set_editing(s_left_encoder_group, true);

    s_left_button_group = lv_group_create();
    lv_group_add_obj(s_left_button_group, s_btn_left);
    lv_group_set_editing(s_left_button_group, true);

    s_right_button_group = lv_group_create();
    lv_group_add_obj(s_right_button_group, s_btn_right);
    lv_group_set_editing(s_right_button_group, true);

    ui_hintbar_create(scr, k_hints);

    return scr;
}

lv_group_t *screen_ev_driving_get_right_encoder_group(void)
{
    return s_right_encoder_group;
}

lv_group_t *screen_ev_driving_get_left_encoder_group(void)
{
    return s_left_encoder_group;
}

lv_group_t *screen_ev_driving_get_left_button_group(void)
{
    return s_left_button_group;
}

lv_group_t *screen_ev_driving_get_right_button_group(void)
{
    return s_right_button_group;
}

