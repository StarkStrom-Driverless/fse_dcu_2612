/**
 * @file        ui_quantity.h
 * @brief       Physical quantity display widget based on lv_spangroup
 *
 * @ingroup     dcu_ui_widgets
 *
 * @details     Shows a physical quantity the way it is written: a numeric
 *              value followed by its unit, as one typographic whole. That
 *              pairing is what the widget is named after — neither half means
 *              anything on its own.
 *
 *              Creates a spangroup with two spans:
 *                span 0 — numeric value, large font
 *                span 1 — unit suffix, small font
 *
 *              A spangroup rather than two labels, because the unit then flows
 *              directly after the digits with correct baseline alignment and
 *              no manual positioning as the value's width changes.
 *
 *              Both spans inherit text_color from the parent spangroup object,
 *              so UI_STATE_WARN / UI_STATE_CRIT level styles applied to the
 *              spangroup affect both spans automatically.  Combined with the
 *              threshold defines from ui_subjects_gen.h, a value colors
 *              itself out of range without a line of per-frame code — see the
 *              example below.
 *
 *              Usage:
 *              @code
 *                lv_obj_t *w = ui_quantity_create(scr,
 *                                  &BarlowCondensed_BoldItalic_32,
 *                                  &BarlowCondensed_Italic_20, "W");
 *                lv_obj_set_size(w, 80, 40);
 *                lv_obj_align(w, LV_ALIGN_CENTER, 0, 0);
 *
 *                ui_quantity_bind_value(w, &ui_subj_power_average, "%.0f");
 *
 *                // Optional level coloring:
 *                lv_obj_add_style(w, &ui_style_level_warn, UI_STATE_WARN);
 *                lv_obj_add_style(w, &ui_style_level_crit, UI_STATE_CRIT);
 *                lv_obj_bind_state_if_gt(w, &ui_subj_power_average,
 *                                        UI_STATE_WARN, UI_POWER_AVERAGE_WARN_HIGH);
 *                lv_obj_bind_state_if_gt(w, &ui_subj_power_average,
 *                                        UI_STATE_CRIT, UI_POWER_AVERAGE_CRIT_HIGH);
 *              @endcode
 *
 * @author      Mario Wegmann <mario.wegmann@web.de>
 * @date        Created: 2026-06-16
 *
 * @version     0.1.0
 *
 * @copyright   Copyright (c) 2026 Mario Wegmann.
 *              SPDX-License-Identifier: Apache-2.0
 * 
 */

/*
 * ─────────────────────────────────────────────────────────────────────────────────────────────────
 * Revision History
 * Version  Date        Author          Description
 * 0.1.0    2026-06-16  Mario Wegmann   Initial creation
 * ─────────────────────────────────────────────────────────────────────────────────────────────────
 */

#ifndef MODULES_UI_WIDGETS_UI_QUANTITY_H
#define MODULES_UI_WIDGETS_UI_QUANTITY_H

#include <lvgl.h>


/**
 * @brief Create a quantity widget.
 *
 * The value span is right-aligned, so the unit stays put as the number of
 * digits changes.
 *
 * @param parent      Parent LVGL object.
 * @param value_font  Font for the numeric value span (e.g. BarlowCondensed_BoldItalic_32).
 *                    Must outlive the widget; the font objects are static.
 * @param unit_font   Font for the unit suffix span  (e.g. BarlowCondensed_Italic_20).
 * @param unit_str    Unit text shown after the value (e.g. "W", "°C", "V").
 *                    Pass "" for a bare number.
 * @return            The lv_spangroup object. Use lv_obj_set_size / lv_obj_align
 *                    to position it. The value reads "0" until the first
 *                    ui_quantity_bind_value() update arrives.
 */
lv_obj_t *ui_quantity_create(lv_obj_t *parent,
                                const lv_font_t *value_font,
                                const lv_font_t *unit_font,
                                const char *unit_str);

/**
 * @brief Bind a subject to the value span.
 *
 * Delegates to lv_spangroup_bind_span_text, which handles both int and float
 * subjects internally. From here on the widget updates itself whenever the
 * subject changes; there is no unbind — the binding dies with the widget.
 *
 * @warning The format specifier must match the subject's actual type. The
 *          generated RX subjects are int subjects, so "%d" is the usual
 *          choice; "%.2f" appears where a signal carries a scaled value.
 *          A mismatch is not diagnosed and prints garbage.
 *
 * @param obj      Spangroup returned by ui_quantity_create().
 * @param subject  lv_subject_t to observe (int or float).
 * @param fmt      printf format string for the value. Must remain valid for
 *                 the lifetime of the widget (string literals are fine).
 */
void ui_quantity_bind_value(lv_obj_t *obj, lv_subject_t *subject, const char *fmt);

#endif /* MODULES_UI_WIDGETS_UI_QUANTITY_H */
