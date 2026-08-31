/**
 * @file        ui_hintbar.c
 * @brief       Input hint bar — what each control does on the current screen
 *
 * @ingroup     dcu_ui_widgets
 *
 * @details     Implementation; the contract is in ui_hintbar.h.
 *
 *              ### Object tree built here
 *              ```
 *              bar (flex row, bottom edge, full width)
 *                └─ one slot per *used* control (flex row, sized to content)
 *                     ├─ icon, recoloured to UI_C_DARK
 *                     └─ caption label
 *              ```
 *
 *              Slots size to their content and the bar spreads them evenly, so
 *              the arrangement stays balanced whether a screen uses one control
 *              or all six. Controls without a function are not built at all —
 *              on a bar this narrow, space spent saying "does nothing" is space
 *              taken from the captions that do.
 *
 *              The consequence is that slot positions shift between screens: a
 *              control is found by its icon, not by where it sat last time.
 *              Relative order is still fixed, so the two encoders stay at the
 *              outer ends whenever both are in use.
 *
 *              ### Why objects here and drawing in the header widget
 *              The page indicator in ui_header.c paints itself in a draw
 *              callback because it is a dozen identical coloured shapes. This
 *              bar is images and text, which LVGL has no comparable shortcut
 *              for — an object per element is what it costs, and twelve of
 *              them once per screen build is not worth optimising.
 *
 *              ### Icons
 *              The images are I4 (16-colour indexed) and are drawn scaled down
 *              from their native 32 px to HINT_ICON_SIZE. Recolouring at full
 *              opacity flattens them to a UI_C_DARK silhouette, so they follow
 *              the theme rather than whatever palette they were exported with.
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

/* ── Corresponding Header ────────────────────────────────────────────────────────────────────── */

#include "modules/ui/widgets/ui_hintbar.h"

/* ── LVGL Include ────────────────────────────────────────────────────────────────────────────── */

#include <lvgl.h>

/* ── Project Includes ────────────────────────────────────────────────────────────────────────── */

#include "modules/ui/ui_styles.h"

/* ── Layout constants ────────────────────────────────────────────────────── */

/**
 * @brief Edge length of one control icon, in pixels.
 *
 * The assets are 32 px; LVGL scales them down to this on the way to the screen.
 */
#define HINT_ICON_SIZE      16

/** @brief Gap between an icon and its caption, in pixels. */
#define HINT_ICON_GAP       4

/** @brief Inset at the left and right end of the bar, in pixels. */
#define HINT_BAR_PAD_H      6


/* ── Icon table ──────────────────────────────────────────────────────────── */

LV_IMAGE_DECLARE(enc_left_b_i4);
LV_IMAGE_DECLARE(btn_left_b_i4);
LV_IMAGE_DECLARE(btn_mid_b_i4);
LV_IMAGE_DECLARE(btn_rear_b_i4);
LV_IMAGE_DECLARE(btn_right_b_i4);
LV_IMAGE_DECLARE(enc_right_b_i4);

/**
 * @brief Icon per control, indexed by @ref ui_hint_input.
 *
 * Designated initialisers tie the table to the enum rather than to a
 * hand-counted order — the build loop indexes both with the same value.
 */
static const lv_image_dsc_t *const k_hint_icons[UI_HINT_INPUT_COUNT] = {
    [UI_HINT_ENC_LEFT]  = &enc_left_b_i4,
    [UI_HINT_BTN_LEFT]  = &btn_left_b_i4,
    [UI_HINT_BTN_MID]   = &btn_mid_b_i4,
    [UI_HINT_BTN_REAR]  = &btn_rear_b_i4,
    [UI_HINT_BTN_RIGHT] = &btn_right_b_i4,
    [UI_HINT_ENC_RIGHT] = &enc_right_b_i4,
};


/* ── Private Function Implementations ───────────────────────────────────────────────────────── */

/**
 * @brief Build one slot: the control's icon and its caption.
 *
 * Only called for controls that have a function; the caller skips the rest.
 *
 * @param bar    Bar container; the slot becomes its child.
 * @param input  Which control this slot stands for.
 * @param label  Caption. Neither NULL nor empty.
 */
static void hint_slot_create(lv_obj_t *bar, enum ui_hint_input input,
                             const char *label)
{
    lv_obj_t *slot = lv_obj_create(bar);
    lv_obj_remove_style_all(slot);
    lv_obj_set_size(slot, LV_SIZE_CONTENT, lv_pct(100));
    lv_obj_set_layout(slot, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(slot, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(slot,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(slot, HINT_ICON_GAP, 0);
    lv_obj_clear_flag(slot, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *icon = lv_image_create(slot);
    lv_image_set_src(icon, k_hint_icons[input]);
    lv_obj_set_size(icon, HINT_ICON_SIZE, HINT_ICON_SIZE);

    lv_obj_t *lbl = lv_label_create(slot);
    lv_obj_add_style(lbl, &ui_style_label_subtitle, 0);
    lv_label_set_text(lbl, label);
}


/* ── Public API ──────────────────────────────────────────────────────────── */

lv_obj_t *ui_hintbar_create(lv_obj_t *parent,
                            const char *const labels[UI_HINT_INPUT_COUNT])
{
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_remove_style_all(bar);
    lv_obj_set_width(bar, lv_pct(100));
    lv_obj_set_height(bar, UI_HINTBAR_H);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_layout(bar, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    /*
     * SPACE_EVENLY rather than START: the number of slots varies per screen,
     * and spreading them keeps a single hint centred instead of stranded at
     * the left edge.
     */
    lv_obj_set_flex_align(bar,
                          LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(bar, HINT_BAR_PAD_H, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    for (uint8_t i = 0U; i < UI_HINT_INPUT_COUNT; i++) {
        const char *label = (labels != NULL) ? labels[i] : NULL;

        if ((label == NULL) || (label[0] == '\0')) {
            continue;   /* no function here — no slot, no width spent */
        }
        hint_slot_create(bar, (enum ui_hint_input)i, label);
    }

    return bar;
}
