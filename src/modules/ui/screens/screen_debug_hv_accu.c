/**
 * @file        screen_debug_hv_accu.c
 * @brief       Debug screen for the high-voltage accumulator
 *
 * @ingroup     dcu_ui_screens
 *
 * @details     Implementation: the contract is in screen_debug_hv_accu.h. 
 * 
 *    Builds two bar-plus-readout pairs accumulator voltage and
 * 
 *              accumulator temperature. Each bar is bound to its generated
 *              subject with lv_bar_bind_value() and each readout to the same
 *              subject through the ui_quantity widget, so the screen has no
 *              update path of its own and no state to keep.
 *
 * @author      Mario Wegmann <mario.wegmann@web.de>
 * @date        Created: 2026-06-25
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
 * 0.1.0    2026-06-25  Mario Wegmann   Initial creation
 * ─────────────────────────────────────────────────────────────────────────────────────────────────
 */

/* ── Corresponding Header ────────────────────────────────────────────────────────────────────── */

#include "modules/ui/screens/screen_debug_hv_accu.h"

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
#include "generated/ui_subjects_gen.h"

/* ── Zephyr Logging ──────────────────────────────────────────────────────────────────────────── */

LOG_MODULE_REGISTER(screen_debug_hv_accu, CONFIG_LOG_DEFAULT_LEVEL);


/* ── Private Macros & Constants ──────────────────────────────────────────────────────────────── */

/* ── Private Variables ───────────────────────────────────────────────────────────────────────── */


/**
 * @brief What each control does on this screen; see @ref ui_hint_input.
 *
 * Static storage: ui_hintbar_create() keeps the pointers rather than copying
 * the strings. Controls left out here are dimmed in the bar.
 */
static const char *const k_hints[UI_HINT_INPUT_COUNT] = {
    [UI_HINT_ENC_LEFT] = "Switch Screen",
};

/* ── Private Function Prototypes ─────────────────────────────────────────────────────────────── */
static void build_bar_row(lv_obj_t *parent, const struct ui_signal_desc *desc,
                          const char *caption);
static void build_bars(lv_obj_t *scr);

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
#define VALUE_MIN_W_U       11

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
 * HV battery voltage and temperature, in a single full-width column.
 *
 * @param scr  Screen object to build into.
 */
static void build_bars(lv_obj_t *scr)
{
    lv_obj_t *content = ui_layout_content_create(scr);
    lv_obj_t *col     = ui_layout_column_create(content, 100);

    build_bar_row(col, &ui_sig_voltage_accu_hv, ui_sig_voltage_accu_hv.label);
    build_bar_row(col, &ui_sig_temperature_accu_hv, ui_sig_temperature_accu_hv.label);
}

/* ── Public Function Implementations ─────────────────────────────────────────────────────────── */

lv_obj_t *screen_debug_hv_accu_create(lv_subject_t *status_subjects)
{
    /* ── Screen base ─────────────────────────────────────────────────────── */

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_add_style(scr, &ui_style_screen, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* ── Widgets ─────────────────────────────────────────────────────────── */

    ui_header_create(scr, "DBG HV ACCU", status_subjects);
    build_bars(scr);

    ui_hintbar_create(scr, k_hints);

    return scr;
}

lv_group_t *screen_debug_hv_accu_get_right_encoder_group(void)
{
    return NULL;
}

lv_group_t *screen_debug_hv_accu_get_left_button_group(void)
{
    return NULL;
}

lv_group_t *screen_debug_hv_accu_get_right_button_group(void)
{
    return NULL;
}

