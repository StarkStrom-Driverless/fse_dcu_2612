/**
 * @file        ui_unit_label.c
 * @brief       Composite value+unit display widget based on lv_spangroup
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

/* ── Corresponding Header ────────────────────────────────────────────────────────────────────── */

#include "modules/ui/ui_unit_label.h"


/* ── Private Constants ───────────────────────────────────────────────────────────────────────── */

/** @brief Index of the value span inside the spangroup. */
#define VALUE_SPAN_IDX  0


/* ── Public Function Implementations ─────────────────────────────────────────────────────────── */

lv_obj_t *ui_unit_label_create(lv_obj_t *parent,
                                const lv_font_t *value_font,
                                const lv_font_t *unit_font,
                                const char *unit_str)
{
    lv_obj_t *sg = lv_spangroup_create(parent);
    lv_obj_set_style_text_align(sg, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);

    /* Span 0: numeric value — right-aligned so unit stays pinned as digits change */
    lv_span_t *value_span = lv_spangroup_new_span(sg);
    lv_style_set_text_font(lv_span_get_style(value_span), value_font);
    lv_span_set_text(value_span, "0");

    /* Span 1: unit suffix — font only, colour inherited from spangroup state */
    lv_span_t *unit_span = lv_spangroup_new_span(sg);
    lv_style_set_text_font(lv_span_get_style(unit_span), unit_font);
    lv_span_set_text(unit_span, unit_str);

    lv_spangroup_refresh(sg);

    return sg;
}

void ui_unit_label_bind_value(lv_obj_t *obj, lv_subject_t *subject, const char *fmt)
{
    lv_spangroup_bind_span_text(obj, lv_spangroup_get_child(obj, VALUE_SPAN_IDX), subject, fmt);
}
