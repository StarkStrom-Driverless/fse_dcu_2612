/**
 * @file        ui_quantity.c
 * @brief       Physical quantity display widget based on lv_spangroup
 *
 * @ingroup     dcu_ui_widgets
 *
 * @details     Implementation; the contract is in ui_quantity.h.
 *
 *              The widget keeps no state of its own — it is two spans and a
 *              binding — so there is no create/destroy bookkeeping and no
 *              handle for the caller to hold on to.
 *
 * @author      Mario Wegmann <mario.wegmann@web.de>
 * @date        Created: 2026-06-16
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
 * 0.1.0    2026-06-16  Mario Wegmann   Initial creation
 * ─────────────────────────────────────────────────────────────────────────────────────────────────
 */

/* ── Corresponding Header ────────────────────────────────────────────────────────────────────── */

#include "modules/ui/widgets/ui_quantity.h"

/* ── Project Includes ────────────────────────────────────────────────────────────────────────── */

#include "modules/ui/ui_styles.h"


/* ── Private Constants ───────────────────────────────────────────────────────────────────────── */

/** @brief Index of the value span inside the spangroup. */
#define VALUE_SPAN_IDX  0


/* ── Private Types ───────────────────────────────────────────────────────────────────────────── */

/**
 * @brief Thresholds of one level binding, carried as the observer's user data.
 *
 * Heap-allocated per binding because the observer callback is shared and the
 * values differ per widget. Freed by level_delete_cb() when the object goes.
 */
struct level_dsc {
    float                      warn; /**< UI_STATE_WARN threshold.        */
    float                      crit; /**< UI_STATE_CRIT threshold.        */
    enum ui_quantity_level_cmp cmp;  /**< Which side is out of range.     */
};


/* ── Private Function Implementations ────────────────────────────────────────────────────────── */

/**
 * @brief Read a subject as a float, whatever it stores.
 *
 * The generated subjects are int for raw signals and float for scaled ones.
 * Comparing everything as float costs nothing here and spares every call site
 * from knowing which kind it got.
 *
 * @param subject  Subject to read.
 * @return         Its current value. 0 for a type that holds no number.
 */
static float level_value(lv_subject_t *subject)
{
    switch (subject->type) {
    case LV_SUBJECT_TYPE_FLOAT:
        return lv_subject_get_float(subject);
    case LV_SUBJECT_TYPE_INT:
        return (float)lv_subject_get_int(subject);
    default:
        LV_LOG_WARN("ui_quantity_bind_level: subject type %d holds no number",
                    (int)subject->type);
        return 0.0f;
    }
}

/**
 * @brief Observer callback — apply both level states to the bound object.
 *
 * @param observer  Observer whose target object carries the states.
 * @param subject   The observed quantity.
 */
static void level_observer_cb(lv_observer_t *observer, lv_subject_t *subject)
{
    lv_obj_t               *obj = lv_observer_get_target_obj(observer);
    const struct level_dsc *dsc = lv_observer_get_user_data(observer);
    float                   val = level_value(subject);

    bool warn;
    bool crit;

    if (dsc->cmp == UI_QUANTITY_LEVEL_BELOW) {
        warn = val < dsc->warn;
        crit = val < dsc->crit;
    } else {
        warn = val > dsc->warn;
        crit = val > dsc->crit;
    }

    lv_obj_set_state(obj, UI_STATE_WARN, warn);
    lv_obj_set_state(obj, UI_STATE_CRIT, crit);
}

/**
 * @brief LV_EVENT_DELETE handler — free the descriptor with the object.
 *
 * LVGL removes the observer itself when the object is deleted, but it does not
 * know about the descriptor behind user_data. Registering this first means it
 * runs before LVGL's own unsubscribe handler, and the callback cannot fire
 * during deletion, so the order is safe either way.
 *
 * @param e  LV_EVENT_DELETE; its user data is the descriptor.
 */
static void level_delete_cb(lv_event_t *e)
{
    lv_free(lv_event_get_user_data(e));
}


/* ── Public Function Implementations ─────────────────────────────────────────────────────────── */

lv_obj_t *ui_quantity_create(lv_obj_t *parent,
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

    /* Span 1: unit suffix — font only, color inherited from spangroup state */
    lv_span_t *unit_span = lv_spangroup_new_span(sg);
    lv_style_set_text_font(lv_span_get_style(unit_span), unit_font);
    lv_span_set_text(unit_span, unit_str);

    lv_spangroup_refresh(sg);

    return sg;
}

void ui_quantity_bind_value(lv_obj_t *obj, lv_subject_t *subject, const char *fmt)
{
    lv_spangroup_bind_span_text(obj, lv_spangroup_get_child(obj, VALUE_SPAN_IDX), subject, fmt);
}

void ui_quantity_bind_level(lv_obj_t *obj, lv_subject_t *subject,
                            enum ui_quantity_level_cmp cmp,
                            float warn, float crit)
{
    struct level_dsc *dsc = lv_malloc(sizeof(*dsc));

    if (dsc == NULL) {
        LV_LOG_WARN("ui_quantity_bind_level: out of memory");
        return;
    }

    dsc->warn = warn;
    dsc->crit = crit;
    dsc->cmp  = cmp;

    lv_obj_add_event_cb(obj, level_delete_cb, LV_EVENT_DELETE, dsc);

    /* Fires once on subscription, so the initial state needs no separate call. */
    lv_subject_add_observer_obj(subject, level_observer_cb, obj, dsc);
}
