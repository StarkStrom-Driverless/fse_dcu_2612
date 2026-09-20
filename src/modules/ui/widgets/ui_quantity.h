/**
 * @file
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
 *              spangroup affect both spans automatically.
 *
 *              ### Presentation comes from the signal descriptor
 *              A quantity is not told how to look — its caption, unit, decimals
 *              and limits are declared once per signal in dbc/dcu_app.yaml and
 *              arrive as a struct ui_signal_desc (ui_subjects_gen.h).
 *              ui_quantity_bind_signal() takes that descriptor and does the
 *              rest: the value text in the right format, and the coloring by
 *              the signal's limits. A screen writes no unit, format or limit of
 *              its own.
 *
 *              Usage:
 *              @code
 *                const struct ui_signal_desc *d = &ui_sig_power_average;
 *
 *                lv_obj_t *w = ui_quantity_create(scr,
 *                                  &BarlowCondensed_BoldItalic_32,
 *                                  &BarlowCondensed_Italic_20, d->unit);
 *                lv_obj_align(w, LV_ALIGN_CENTER, 0, 0);
 *
 *                ui_quantity_bind_signal(w, d);
 *              @endcode
 *
 *              @note Do not reach for LVGL's own lv_obj_bind_state_if_gt/_lt
 *                    for the coloring. Those take an int32_t reference value
 *                    and refuse any subject that is not LV_SUBJECT_TYPE_INT — a
 *                    float subject only produces "bind_to_bitfield:
 *                    Incompatible subject type" at runtime and is then left
 *                    uncolored. The generated limits are floats and some of the
 *                    subjects are too.
 *
 * @author      Mario Wegmann <mario.wegmann@web.de>
 * @date        Created: 2026-06-16
 *
 * @copyright   Copyright (c) 2026 Mario Wegmann.
 *              SPDX-License-Identifier: Apache-2.0
 * 
 */

#ifndef MODULES_UI_WIDGETS_UI_QUANTITY_H
#define MODULES_UI_WIDGETS_UI_QUANTITY_H

#include <lvgl.h>

#include "generated/ui_subjects_gen.h"


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

/**
 * @brief Show a signal: bind its value and color it by its limits.
 *
 * Does two things for the signal @p desc describes:
 *
 * 1. Binds the value span to desc->subject, formatted with desc->fmt — "%d" for
 *    an integer signal, "%.Nf" with the YAML's precision for a float one.
 *
 * 2. If the signal declares any limit, adds the two level styles for
 *    UI_STATE_WARN and UI_STATE_CRIT and installs one observer that keeps both
 *    states in step with the value. Both are evaluated on every update, so a
 *    value that comes back into range clears them again; UI_STATE_CRIT wins
 *    over UI_STATE_WARN through the style priority (see ui_styles.h), so a
 *    value past the critical limit carrying both is correct, not a conflict.
 *    The limits follow the YAML semantics, all of them optional:
 *    @code
 *      value < crit_low   → critical        value > warn_high → warning
 *      value < warn_low   → warning         value > crit_high → critical
 *    @endcode
 *    A signal without limits is not colored, and no styles are added.
 *
 * The value is read according to the subject's own type (int or float) and
 * compared as a float.
 *
 * @p desc must outlive the widget — the generated descriptors are constants in
 * flash, so it always does. The widget keeps a pointer, not a copy. The binding
 * dies with @p obj; there is no unbind.
 *
 * @param obj   Spangroup returned by ui_quantity_create().
 * @param desc  Descriptor from ui_subjects_gen.h, e.g. &ui_sig_power_average.
 */
void ui_quantity_bind_signal(lv_obj_t *obj, const struct ui_signal_desc *desc);

#endif /* MODULES_UI_WIDGETS_UI_QUANTITY_H */
