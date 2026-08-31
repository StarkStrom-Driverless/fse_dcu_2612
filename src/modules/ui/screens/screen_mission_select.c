/**
 * @file        screen_mission_select.c
 * @brief       Mission selection screen implementation
 *
 * @ingroup     dcu_ui_screens
 *
 * @details     Builds the mission selection screen with:
 *
 *                – Roller       : the mission list, scrolled by the right
 *                                 encoder.  Scrolling publishes nothing; it
 *                                 only moves the highlight.
 *
 *                – Label        : "Current Mission: …", driven by an observer
 *                                 on ui_tx_subj_drive_mode, so it always shows
 *                                 what was last confirmed rather than what is
 *                                 highlighted.
 *
 *                – SET MISSION  : publishes UI_INPUT_MISSION_SELECTED with the
 *                                 highlighted index and updates the TX subject.
 *                                 The App Layer writes it to app_state; the CAN
 *                                 module transmits it on its next cycle.  No
 *                                 CAN frame is built here.
 *
 *              ### State across visits
 *              The screen is destroyed on leaving, so the roller position is
 *              kept in the file-scope subject s_roller_sel and restored when
 *              the screen is rebuilt.  The confirmed mission needs no such
 *              handling — it lives in the generated TX subject.
 *
 *              ### Encoder mode
 *              The encoder group is created with editing enabled and never
 *              leaves it, because the roller is the only member: there is
 *              nothing to navigate between, so a turn should always scroll.
 *
 * @author      Mario Wegmann <mario.wegmann@web.de>
 * @date        Created: 2026-06-02
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
 * 0.1.0    2026-06-02  Mario Wegmann   Initial creation
 * ─────────────────────────────────────────────────────────────────────────────────────────────────
 */

/* ── Corresponding Header ────────────────────────────────────────────────────────────────────── */

#include "modules/ui/screens/screen_mission_select.h"

/* ── Zephyr Includes ─────────────────────────────────────────────────────────────────────────── */

#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/logging/log.h>

/* ── Project Includes ────────────────────────────────────────────────────────────────────────── */

#include "app/app_state.h"
#include "modules/ui/ui_styles.h"
#include "modules/ui/widgets/ui_header.h"
#include "modules/ui/widgets/ui_hintbar.h"
#include "services/event_bus/event_bus.h"
#include "services/event_bus/events.h"
#include "generated/ui_tx_subjects_gen.h"

/* ── Zephyr Logging ──────────────────────────────────────────────────────────────────────────── */

LOG_MODULE_REGISTER(screen_mission_select, CONFIG_LOG_DEFAULT_LEVEL);


/* ── Private Macros & Constants ──────────────────────────────────────────────────────────────── */

/**
 * @brief Roller option string — one mission per line.
 *
 * The roller index is published as a mission_id and ends up unchanged in the
 * DV_Drive_Mode_SETTING CAN signal, so this list is effectively a wire format.
 *
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

/**
 * @brief Mission names for the confirmed-mission label.
 *
 * Mirrors ROLLER_OPTIONS line by line and must stay in step with it: the
 * observer indexes this array with the confirmed roller index.
 */
static const char *const k_mission_names[] = {
    "None", "Acceleration", "Skidpad", "Trackdrive",
    "Braketest", "Inspection", "Autocross", "Manual Driving",
};

/** @brief Number of roller rows visible simultaneously (one above/below the selection). */
#define ROLLER_VISIBLE_ROWS     3U

/** @brief Width of the roller widget in pixels. */
#define ROLLER_WIDTH            220

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

/**
 * @brief Currently highlighted roller index — survives screen destroy/recreate.
 *
 * Initialised once, guarded by s_subjects_init, and never re-initialised: that
 * is what carries the highlight across the create/delete cycle. Re-running
 * lv_subject_init_int() would reset it to zero on every visit.
 */
static lv_subject_t s_roller_sel;

/** @brief Guard so the subject above is initialised exactly once. */
static bool         s_subjects_init;

/** @brief Mission roller — user scrolls with the right encoder. */
static lv_obj_t   *s_roller;

/** @brief "Current Mission: …" label; driven by an observer, not by the roller. */
static lv_obj_t   *s_roller_lbl;

/** @brief SET MISSION button — confirms the highlighted mission. */
static lv_obj_t   *s_btn_ok;

/* An UNSET MISSION counterpart existed here; the code is retained below. */

/** @brief Input group for the right encoder — holds the roller. */
static lv_group_t *s_right_encoder_group;

/** @brief Input group for the left button pad. Never created; stays NULL. */
static lv_group_t *s_left_button_group;

/** @brief Input group for the right button pad — holds SET MISSION. */
static lv_group_t *s_right_button_group;


/**
 * @brief What each control does on this screen; see @ref ui_hint_input.
 *
 * Static storage: ui_hintbar_create() keeps the pointers rather than copying
 * the strings. Controls left out here are dimmed in the bar.
 */
static const char *const k_hints[UI_HINT_INPUT_COUNT] = {
    [UI_HINT_ENC_LEFT]  = "Screen",
    [UI_HINT_BTN_RIGHT] = "Set",
    [UI_HINT_ENC_RIGHT] = "Mission",
};

/* ── Private Function Prototypes ─────────────────────────────────────────────────────────────── */

static void build_roller(lv_obj_t *scr);
static void build_buttons(lv_obj_t *scr);
static void btn_ok_event_cb(lv_event_t *e);
// static void btn_esc_event_cb(lv_event_t *e);


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
 * @brief Update the "Current Mission" label from the confirmed drive mode.
 *
 * Observing the TX subject rather than the roller is what makes the label show
 * the confirmed mission instead of the highlighted one.
 *
 * @param observer  Observer whose target object is the label.
 * @param subject   ui_tx_subj_drive_mode; holds an index into k_mission_names.
 */
static void confirmed_mission_observer_cb(lv_observer_t *observer, lv_subject_t *subject)
{
    lv_obj_t *lbl = lv_observer_get_target_obj(observer);
    int32_t   idx = lv_subject_get_int(subject);
    lv_label_set_text_fmt(lbl, "Current Mission: %s", k_mission_names[idx]);
}

/**
 * @brief Build the mission roller and the confirmed-mission label.
 *
 * The roller is restored to the remembered position, so returning to this
 * screen looks like it was never left.
 *
 * @param scr  Screen object to build into.
 */
static void build_roller(lv_obj_t *scr)
{
    s_roller = lv_roller_create(scr);

    lv_roller_set_options(s_roller, ROLLER_OPTIONS, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(s_roller, ROLLER_VISIBLE_ROWS);
    lv_roller_set_selected(s_roller, (uint16_t)lv_subject_get_int(&s_roller_sel), LV_ANIM_OFF);

    lv_obj_set_width(s_roller, ROLLER_WIDTH);

    /* ── Main part: all items ──────────────────────────────────────────── */
    lv_obj_set_style_bg_color(s_roller,     UI_C_WHITE,                     LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_roller,       LV_OPA_COVER,                   LV_PART_MAIN);
    lv_obj_set_style_text_font(s_roller,    &BarlowCondensed_BoldItalic_18, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_roller,   UI_C_DARK,                      LV_PART_MAIN);
    lv_obj_set_style_border_width(s_roller, 2,                              LV_PART_MAIN);
    lv_obj_set_style_border_color(s_roller, UI_C_DARK,                      LV_PART_MAIN);
    lv_obj_set_style_radius(s_roller,       0,                              LV_PART_MAIN);

    /* ── Selected part: centre row highlight ───────────────────────────── */
    lv_obj_set_style_bg_color(s_roller,   UI_C_ACCENT,                     LV_PART_SELECTED);
    lv_obj_set_style_bg_opa(s_roller,     LV_OPA_COVER,                    LV_PART_SELECTED);
    lv_obj_set_style_text_font(s_roller,  &BarlowCondensed_BoldItalic_18,  LV_PART_SELECTED);
    lv_obj_set_style_text_color(s_roller, UI_C_DARK,                       LV_PART_SELECTED);

    /* Vertically centred in the content area below the 15 % header. */
    lv_obj_align(s_roller, LV_ALIGN_CENTER, 0, -30);
    lv_obj_add_event_cb(s_roller, roller_value_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* ── Confirmed mission label (observer-driven) ──────────────────────── */

    s_roller_lbl = lv_label_create(scr);
    lv_obj_add_style(s_roller_lbl, &ui_style_label_subtitle, 0);
    lv_obj_align(s_roller_lbl, LV_ALIGN_CENTER, 0, 35);
    lv_subject_add_observer_obj(&ui_tx_subj_drive_mode, confirmed_mission_observer_cb,
                                s_roller_lbl, NULL);
}

/**
 * @brief Build the SET MISSION button.
 *
 * @param scr  Screen object to build into.
 */
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
 * @brief SET MISSION click handler — confirm the highlighted mission.
 *
 * Publishes UI_INPUT_MISSION_SELECTED with the roller index. The App Layer
 * stores it in app_state, from where the CAN module reads it on its next TX
 * cycle; no CAN frame is built here.
 *
 * The TX subject — which drives the "Current Mission" label — is only updated
 * once the publish succeeded, so a dropped event cannot leave the display
 * claiming a mission the vehicle was never told about.
 *
 * @param e  LV_EVENT_CLICKED from the button. Unused.
 */
static void btn_ok_event_cb(lv_event_t *e)
{
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
        lv_subject_set_int(&ui_tx_subj_drive_mode, (int32_t)idx);
        LOG_DBG("Mission selected: %u", (unsigned)idx);
    }
}

/*
 * Retained: the UNSET MISSION button, which published the same event so the
 * driver could clear a confirmed mission. Re-enable together with the button
 * itself in build_buttons() and the group registration in the factory.
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

lv_obj_t *screen_mission_select_create(lv_subject_t *status_subjects)
{
    /* ── Screen base ─────────────────────────────────────────────────────── */

    if (!s_subjects_init) {
        lv_subject_init_int(&s_roller_sel, 0);
        s_subjects_init = true;
    }

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_add_style(scr, &ui_style_screen, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* ── Widgets ─────────────────────────────────────────────────────────── */

    ui_header_create(scr, "DV MISSION", status_subjects);
    build_roller(scr);
    build_buttons(scr);

    /* ── Input groups ────────────────────────────────────────────────────── */

    /*
     * One member each, so there is nothing to tab between and both groups stay
     * in edit mode: a turn of the encoder scrolls the roller, a press of the
     * right pad clicks the button.
     *
     * ui.c attaches them to the input devices on screen entry and detaches
     * them on leaving.  The groups themselves are owned by LVGL and released
     * with the screen.
     */
    s_right_encoder_group = lv_group_create();
    lv_group_add_obj(s_right_encoder_group, s_roller);
    lv_group_set_editing(s_right_encoder_group, true);

    s_right_button_group = lv_group_create();
    lv_group_add_obj(s_right_button_group, s_btn_ok);
    lv_group_set_editing(s_right_button_group, true);

    ui_hintbar_create(scr, k_hints);

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

