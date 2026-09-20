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
 * @version     0.2.0
 *
 * @copyright   Copyright (c) 2026 Mario Wegmann.
 *              SPDX-License-Identifier: Apache-2.0
 */

/*
 * ─────────────────────────────────────────────────────────────────────────────────────────────────
 * Revision History
 * Version  Date        Author          Description
 * 0.1.0    2026-06-16  Mario Wegmann   Initial creation
 * 0.2.0    2026-09-20  Mario Wegmann   ui_quantity_bind_signal() replaces
 *                                      ui_quantity_bind_level(); limits from the
 *                                      signal descriptor
 * ─────────────────────────────────────────────────────────────────────────────────────────────────
 */

/* ── Corresponding Header ────────────────────────────────────────────────────────────────────── */

#include "modules/ui/widgets/ui_quantity.h"

/* ── Project Includes ────────────────────────────────────────────────────────────────────────── */

#include "modules/ui/ui_styles.h"


/* ── Private Constants ───────────────────────────────────────────────────────────────────────── */

/** @brief Index of the value span inside the spangroup. */
#define VALUE_SPAN_IDX  0

/** @brief Every limit flag of a struct ui_signal_desc. */
#define LIMIT_FLAGS (UI_SIG_CRIT_LOW | UI_SIG_WARN_LOW | UI_SIG_WARN_HIGH | UI_SIG_CRIT_HIGH)


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
        LV_LOG_WARN("ui_quantity: subject type %d holds no number",
                    (int)subject->type);
        return 0.0f;
    }
}

/**
 * @brief Observer callback — apply both level states to the bound object.
 *
 * The user data is the signal descriptor itself: a constant in flash that
 * outlives every widget, so there is nothing to allocate and nothing to free.
 *
 * @param observer  Observer whose target object carries the states.
 * @param subject   The observed quantity.
 */
static void level_observer_cb(lv_observer_t *observer, lv_subject_t *subject)
{
    lv_obj_t                    *obj = lv_observer_get_target_obj(observer);
    const struct ui_signal_desc *d   = lv_observer_get_user_data(observer);
    float                        val = level_value(subject);

    bool warn = ((d->flags & UI_SIG_WARN_LOW)  && val < d->warn_low) ||
                ((d->flags & UI_SIG_WARN_HIGH) && val > d->warn_high);
    bool crit = ((d->flags & UI_SIG_CRIT_LOW)  && val < d->crit_low) ||
                ((d->flags & UI_SIG_CRIT_HIGH) && val > d->crit_high);

    lv_obj_set_state(obj, UI_STATE_WARN, warn);
    lv_obj_set_state(obj, UI_STATE_CRIT, crit);
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

void ui_quantity_bind_signal(lv_obj_t *obj, const struct ui_signal_desc *desc)
{
    ui_quantity_bind_value(obj, desc->subject, desc->fmt);

    if ((desc->flags & LIMIT_FLAGS) == 0U) {
        return;   /* no limits declared: nothing to color by */
    }

    lv_obj_add_style(obj, &ui_style_level_warn, UI_STATE_WARN);
    lv_obj_add_style(obj, &ui_style_level_crit, UI_STATE_CRIT);

    /*
     * Fires once on subscription, so the initial state needs no separate call.
     * The descriptor is const, the observer's user data is not — the callback
     * only reads it.
     */
    lv_subject_add_observer_obj(desc->subject, level_observer_cb, obj,
                                (void *)desc);
}
