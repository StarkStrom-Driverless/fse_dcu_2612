/**
 * @file        screen_debug_write.c
 * @brief       Debug-bits transmit screen implementation
 *
 * @ingroup     dcu_ui_screens
 *
 * @details     Builds the debug-bits screen with:
 *
 *                – Roller     : the values 0…7, scrolled by the right encoder.
 *                               Scrolling publishes nothing.
 *
 *                – Label      : "Current Debug Bits: …", driven by an observer
 *                               on ui_tx_subj_debug_bits, so it shows the last
 *                               confirmed value rather than the highlighted one.
 *
 *                – SET BITS   : publishes UI_INPUT_DEBUG_BITS_SELECTED with the
 *                               highlighted value.  The App Layer forwards it to
 *                               the settings service, which clamps and owns it;
 *                               the CAN module reads it back on its next cycle.
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
 *              next visit.  The confirmed value needs no such handling — it
 *              lives in the generated TX subject.
 *
 * @author      Mario Wegmann <mario.wegmann@web.de>
 * @date        Created: 2026-06-09
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
 * 0.1.0    2026-06-09  Mario Wegmann   Initial creation
 * ─────────────────────────────────────────────────────────────────────────────────────────────────
 */

/* ── Corresponding Header ────────────────────────────────────────────────────────────────────── */

#include "modules/ui/screens/screen_debug_write.h"

/* ── Zephyr Includes ─────────────────────────────────────────────────────────────────────────── */

#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/logging/log.h>

/* ── Project Includes ────────────────────────────────────────────────────────────────────────── */

#include "modules/ui/ui_styles.h"
#include "modules/ui/widgets/ui_header.h"
#include "services/event_bus/event_bus.h"
#include "services/event_bus/events.h"
#include "generated/ui_tx_subjects_gen.h"

/* ── Zephyr Logging ──────────────────────────────────────────────────────────────────────────── */

LOG_MODULE_REGISTER(screen_debug_write, CONFIG_LOG_DEFAULT_LEVEL);


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

/** @brief Bottom margin for the button row (pixels from screen bottom). */
#define BTN_BOTTOM_MARGIN       20


/* ── Private Variables ───────────────────────────────────────────────────────────────────────── */

/**
 * @brief Currently highlighted roller index — survives screen destroy/recreate.
 *
 * Initialised once, guarded by s_subjects_init, and never re-initialised: that
 * is what carries the highlight across the create/delete cycle.
 */
static lv_subject_t s_roller_sel;

/** @brief Guard so the subject above is initialised exactly once. */
static bool         s_subjects_init;

/** @brief Debug-value roller — user scrolls with the right encoder. */
static lv_obj_t   *s_roller;

/** @brief "Current Debug Bits: …" label; driven by an observer on the TX subject. */
static lv_obj_t   *s_roller_lbl;

/** @brief SET BITS button — confirms the highlighted value. */
static lv_obj_t   *s_btn_ok;

/** @brief Input group for the right encoder — holds the roller. */
static lv_group_t *s_right_encoder_group;

/** @brief Input group for the left button pad. Never created; stays NULL. */
static lv_group_t *s_left_button_group;

/** @brief Input group for the right button pad — holds SET BITS. */
static lv_group_t *s_right_button_group;


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

    /* ── Confirmed bits label (observer-driven) ─────────────────────────── */

    s_roller_lbl = lv_label_create(scr);
    lv_obj_add_style(s_roller_lbl, &ui_style_label_subtitle, 0);
    lv_obj_align(s_roller_lbl, LV_ALIGN_CENTER, 0, 35);
    lv_subject_add_observer_obj(&ui_tx_subj_debug_bits, confirmed_bits_observer_cb,
                                s_roller_lbl, NULL);
}

/**
 * @brief Build the SET BITS button.
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
    lv_obj_add_style(s_btn_ok, &ui_style_btn_focused, LV_STATE_FOCUS_KEY);
    lv_obj_set_size(s_btn_ok, BTN_WIDTH, BTN_HEIGHT);
    lv_obj_align(s_btn_ok, LV_ALIGN_BOTTOM_MID, BTN_HALF_SPACING, -BTN_BOTTOM_MARGIN);

    lv_obj_t *lbl_ok = lv_label_create(s_btn_ok);
    lv_obj_add_style(lbl_ok, &ui_style_label_subtitle, 0);
    lv_label_set_text(lbl_ok, "SET BITS");
    lv_obj_align(lbl_ok, LV_ALIGN_CENTER, 0, 0);

    lv_obj_add_event_cb(s_btn_ok, btn_ok_event_cb, LV_EVENT_CLICKED, NULL);

    /* ── RTD button ────────────────────────────────────────────────────── */

    // s_btn_rtd = lv_button_create(scr);
    // lv_obj_remove_style_all(s_btn_rtd);
    // lv_obj_add_style(s_btn_rtd, &ui_style_btn_default, 0);
    // lv_obj_add_style(s_btn_rtd, &ui_style_btn_checked, LV_STATE_CHECKED);
    // lv_obj_add_style(s_btn_rtd, &ui_style_btn_focused, LV_STATE_FOCUS_KEY);
    // lv_obj_set_size(s_btn_rtd, BTN_WIDTH, BTN_HEIGHT);
    // lv_obj_align(s_btn_rtd, LV_ALIGN_BOTTOM_MID, BTN_HALF_SPACING, -BTN_BOTTOM_MARGIN);

    // lv_obj_t *lbl_rtd = lv_label_create(s_btn_rtd);
    // lv_obj_add_style(lbl_rtd, &ui_style_label_subtitle, 0);
    // lv_label_set_text(lbl_rtd, "SET BITS");
    // lv_obj_align(lbl_rtd, LV_ALIGN_CENTER, 0, 0);

    // lv_obj_add_flag(s_btn_rtd, LV_OBJ_FLAG_CHECKABLE);
    // lv_obj_add_event_cb(s_btn_rtd, btn_rtd_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
}

/**
 * @brief SET BITS click handler — confirm the highlighted debug value.
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
static void btn_ok_event_cb(lv_event_t *e)
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

/*
 * Retained: a second, checkable button that published only while checked.
 * Left over from the screen this file was copied from; the code below refers
 * to mission selection, not to debug bits.
 */
// static void btn_rtd_event_cb(lv_event_t *e)
// {
//     lv_obj_t *button = lv_event_get_target_obj(e);
//     uint16_t  idx    = lv_roller_get_selected(s_roller);

//     struct ui_input_event evt = {
//         .type         = UI_INPUT_MISSION_SELECTED,
//         .data.mission = (enum mission_id)idx,
//     };

//     if (lv_obj_has_state(button, LV_STATE_CHECKED)) {
//         int ret = zbus_chan_pub(&ui_input_chan, &evt, K_NO_WAIT);
//         if (ret != 0) {
//             LOG_WRN("UI_INPUT_MISSION_SELECTED publish failed (mission=%u): %d",
//                     (unsigned)idx, ret);
//         } else {
//             LOG_INF("Mission confirmed via RTD: idx=%u", (unsigned)idx);
//         }
//     }
// }


/* ── Public Function Implementations ─────────────────────────────────────────────────────────── */

lv_obj_t *screen_debug_write_create(lv_subject_t *status_subjects)
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

    ui_header_create(scr, "DBG TX", status_subjects);
    build_roller(scr);
    build_buttons(scr);

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
    lv_group_add_obj(s_right_button_group, s_btn_ok);
    lv_group_set_editing(s_right_button_group, true);

    return scr;
}

lv_group_t *screen_debug_write_get_right_encoder_group(void)
{
    return s_right_encoder_group;
}

lv_group_t *screen_debug_write_get_left_button_group(void)
{
    return s_left_button_group;
}

lv_group_t *screen_debug_write_get_right_button_group(void)
{
    return s_right_button_group;
}

