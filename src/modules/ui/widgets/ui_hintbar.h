/**
 * @file        ui_hintbar.h
 * @brief       Input hint bar — what each control does on the current screen
 *
 * @ingroup     dcu_ui_widgets
 *
 * @details     A strip along the bottom edge of a screen with one slot per
 *              physical control, each showing the control's icon and a short
 *              caption naming what it does here.
 *
 *              The steering wheel has no labels on it and the same six controls
 *              mean something different on every screen. Without this the only
 *              way to find out is to try — which is not a thing to do on track.
 *
 *              ### Slot order
 *
 *              Fixed, left to right, matching the physical layout:
 *
 *              | Slot | Control | Icon |
 *              |------|---------|------|
 *              | 0 | Left encoder  | enc_left_b_i4   |
 *              | 1 | Left button   | btn_left_b_i4   |
 *              | 2 | Mid button    | btn_mid_b_i4    |
 *              | 3 | Rear button   | btn_rear_b_i4   |
 *              | 4 | Right button  | btn_right_b_i4  |
 *              | 5 | Right encoder | enc_right_b_i4  |
 *
 *              Only controls that have a function are shown. Relative order is
 *              preserved, but a control with nothing to do is left out entirely
 *              rather than dimmed: the bar is narrow, and width spent saying
 *              "does nothing" comes off the captions that carry meaning.
 *
 *              A control is therefore identified by its icon, not by a fixed
 *              position — the slots shift as the number of active controls
 *              changes from screen to screen.
 *
 *              ### Usage
 *
 *              @code
 *                static const char *const k_hints[UI_HINT_INPUT_COUNT] = {
 *                    [UI_HINT_ENC_LEFT]  = "Screen",
 *                    [UI_HINT_BTN_LEFT]  = "PWR Lim",
 *                    [UI_HINT_BTN_RIGHT] = "TQ Vect",
 *                    [UI_HINT_ENC_RIGHT] = "TQG R",
 *                };
 *                ui_hintbar_create(scr, k_hints);
 *              @endcode
 *
 *              Designated initialisers leave the unused slots NULL, so a screen
 *              only writes down the controls it actually uses.
 *
 *              ### Captions have to be short
 *
 *              Slots take exactly the width their icon and caption need, and
 *              the bar spreads whatever is left between them. Nothing is
 *              truncated, which also means nothing stops a screen from
 *              overflowing the bar: the budget is the panel width less the
 *              insets, shared by however many controls that screen uses.
 *
 *              On the 3.5" panel that is 468 px. With all six in use and 16 px
 *              icons, captions have to average well under ten characters to
 *              stay inside it — fewer active controls buy proportionally more
 *              room. Abbreviate at the call site, where the meaning is known.
 *
 * @author      Mario Wegmann <mario.wegmann@web.de>
 * @date        Created: 2026-08-31
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
 * 0.1.0    2026-08-31  Mario Wegmann   Initial creation
 * ─────────────────────────────────────────────────────────────────────────────────────────────────
 */

#ifndef MODULES_UI_WIDGETS_UI_HINTBAR_H
#define MODULES_UI_WIDGETS_UI_HINTBAR_H

#include <lvgl.h>

/**
 * @ingroup dcu_ui_widgets
 * @{
 */

/**
 * @brief Height of the hint bar, in pixels.
 *
 * Exported because it is not only this widget's business: it takes height away
 * from the screen above it, so anything a screen anchors to LV_ALIGN_BOTTOM_*
 * has to add it to its own margin or end up underneath the bar. Four screens
 * do exactly that — grep for UI_HINTBAR_H before changing this.
 *
 * Now that the icons are drawn at 16 px this is more headroom than the content
 * needs; the caption font sets the real floor at around 24 px. Lowering it
 * gives every screen the difference back.
 */
#define UI_HINTBAR_H            36

/**
 * @brief Physical controls, in the order they appear in the bar.
 *
 * Indexes the caption array handed to ui_hintbar_create().
 *
 * @note UI_HINT_INPUT_COUNT is used as an array size — keep it last.
 */
enum ui_hint_input {
    UI_HINT_ENC_LEFT = 0,   /**< Left rotary encoder.  */
    UI_HINT_BTN_LEFT,       /**< Left button.          */
    UI_HINT_BTN_MID,        /**< Middle button.        */
    UI_HINT_BTN_REAR,       /**< Rear button.          */
    UI_HINT_BTN_RIGHT,      /**< Right button.         */
    UI_HINT_ENC_RIGHT,      /**< Right rotary encoder. */
    UI_HINT_INPUT_COUNT,
};

/**
 * @brief Create the hint bar along the bottom edge of @p parent.
 *
 * @param parent  Screen object (lv_obj_create(NULL)).
 * @param labels  One caption per @ref ui_hint_input. A NULL or empty entry
 *                leaves that control out of the bar entirely. The strings are
 *                not copied, so they must outlive the widget — a static array
 *                of literals, as in the usage example. Passing NULL for the
 *                whole array builds an empty bar.
 * @return        The bar object, owned by the LVGL object tree.
 */
lv_obj_t *ui_hintbar_create(lv_obj_t *parent,
                            const char *const labels[UI_HINT_INPUT_COUNT]);

/** @} */ /* dcu_ui_widgets */

#endif /* MODULES_UI_WIDGETS_UI_HINTBAR_H */
