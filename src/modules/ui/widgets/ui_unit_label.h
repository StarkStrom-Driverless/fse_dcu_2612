/**
 * @file        ui_unit_label.h
 * @brief       Composite value+unit display widget based on lv_spangroup
 *
 * @details     Creates a spangroup with two spans:
 *                span 0 — numeric value, large font
 *                span 1 — unit suffix, small font
 *
 *              Both spans inherit text_color from the parent spangroup object,
 *              so UI_STATE_WARN / UI_STATE_CRIT level styles applied to the
 *              spangroup affect both spans automatically.
 *
 *              Usage:
 *              @code
 *                lv_obj_t *w = ui_unit_label_create(scr,
 *                                  &BarlowCondensed_BoldItalic_32,
 *                                  &BarlowCondensed_Italic_20, "W");
 *                lv_obj_set_size(w, 80, 40);
 *                lv_obj_align(w, LV_ALIGN_CENTER, 0, 0);
 *
 *                ui_unit_label_bind_float(w, &ui_subj_power_average, "%.0f");
 *
 *                // Optional level colouring:
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
 * @copyright   Copyright (c) 2026 Mario Wegmann
 *              SPDX-License-Identifier: Apache-2.0
 * 
 * @note        Target RTOS : Zephyr RTOS (https://zephyrproject.org)
 *              UI Library  : LVGL (https://lvgl.io)
 *
 * ─────────────────────────────────────────────────────────────────────────────────────────────────
 * Revision History
 * ─────────────────────────────────────────────────────────────────────────────────────────────────
 * Version  Date        Author          Description
 * 0.1.0    2026-06-16  Mario Wegmann   Initial creation
 * ─────────────────────────────────────────────────────────────────────────────────────────────────
 */

#ifndef MODULES_UI_WIDGETS_UI_UNIT_LABEL_H
#define MODULES_UI_WIDGETS_UI_UNIT_LABEL_H

#include <lvgl.h>


/**
 * @brief Create a value+unit spangroup widget.
 *
 * @param parent      Parent LVGL object.
 * @param value_font  Font for the numeric value span (e.g. BarlowCondensed_BoldItalic_32).
 * @param unit_font   Font for the unit suffix span  (e.g. BarlowCondensed_Italic_20).
 * @param unit_str    Unit text shown after the value (e.g. "W", "°C", "V").
 * @return            The lv_spangroup object. Use lv_obj_set_size / lv_obj_align
 *                    to position it. Initial value text is "---".
 */
lv_obj_t *ui_unit_label_create(lv_obj_t *parent,
                                const lv_font_t *value_font,
                                const lv_font_t *unit_font,
                                const char *unit_str);

/**
 * @brief Bind a subject to the value span.
 *
 * Delegates to lv_spangroup_bind_span_text, which handles both int and float
 * subjects internally. Use a printf-style @p fmt matching the subject type
 * (e.g. "%.0f" for float, "%d" for int). The format string must remain valid
 * for the lifetime of the widget (string literals are fine).
 *
 * @param obj      Spangroup returned by ui_unit_label_create().
 * @param subject  lv_subject_t to observe (int or float).
 * @param fmt      printf format string for the value.
 */
void ui_unit_label_bind_value(lv_obj_t *obj, lv_subject_t *subject, const char *fmt);

#endif /* MODULES_UI_WIDGETS_UI_UNIT_LABEL_H */
