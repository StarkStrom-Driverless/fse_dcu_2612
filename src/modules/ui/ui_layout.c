/**
 * @file        ui_layout.c
 * @brief       Shared screen geometry — implementation
 *
 * @ingroup     dcu_ui_layout
 *
 * @details     The contract is in ui_layout.h.
 *
 *              ### Where the content area sits
 *              The container fills the whole screen and keeps clear of the frame
 *              with padding: the header and the page bar on top, the hint bar at
 *              the bottom. Padding rather than an offset and a reduced height,
 *              because a percentage height would then be a percentage of the
 *              wrong thing — LVGL resolves the children's percentages against the
 *              content area, which is exactly the space that is left.
 *
 *              The three heights are shares of the display (header, hint bar) and
 *              a layout unit (page bar), so the frame keeps its proportions on
 *              another display; nothing else in the screen frame is ever
 *              anything but one of these three.
 *
 * @author      Mario Wegmann <mario.wegmann@web.de>
 * @date        Created: 2026-09-20
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
 * 0.1.0    2026-09-20  Mario Wegmann   Initial creation
 * 0.2.0    2026-09-20  Mario Wegmann   Frame heights (header, page bar, hint bar) exported as
 *                                      functions; hint bar now a share of the height
 * ─────────────────────────────────────────────────────────────────────────────────────────────────
 */

/* ── Corresponding Header ────────────────────────────────────────────────────────────────────── */

#include "modules/ui/ui_layout.h"

/* ── Private Macros & Constants ──────────────────────────────────────────────────────────────── */

/** @brief Units per shorter display side; see ui_layout_u(). */
#define UNITS_PER_SIDE      32

/**
 * @brief Padding at each side of the content area, in units.
 *
 * Every column adds COLUMN_PAD_U on each of its sides again, so the visible
 * margin at the screen edge is the sum of the two.
 */
#define CONTENT_PAD_U       1

/**
 * @brief Padding at each side of a column, in units.
 *
 * Two neighboring columns therefore stand 2 × COLUMN_PAD_U apart. The gap is
 * padding inside the columns and not a gap between them on purpose: columns
 * that add up to 100 % of the content width plus a gap between them would be
 * wider than the content area, and flex does not shrink them to fit.
 */
#define COLUMN_PAD_U        1


/* ── Private Function Implementations ───────────────────────────────────────────────────────── */

/**
 * @brief A share of the display height, in pixels.
 *
 * The same arithmetic LVGL applies to lv_pct() in the widgets that use it, so
 * the pixel value here and the height the widget gets agree to the pixel.
 *
 * @param pct  Percentage of the display height.
 * @return     The height in pixels.
 */
static int32_t share_of_height(int32_t pct)
{
    int32_t ver = lv_display_get_vertical_resolution(lv_display_get_default());

    return (ver * pct) / 100;
}


/* ── Public Function Implementations ─────────────────────────────────────────────────────────── */

int32_t ui_layout_header_h(void)
{
    return share_of_height(UI_LAYOUT_HEADER_PCT);
}

int32_t ui_layout_pagebar_h(void)
{
    return ui_layout_u(UI_LAYOUT_PAGEBAR_U);
}

int32_t ui_layout_hintbar_h(void)
{
    return share_of_height(UI_LAYOUT_HINTBAR_PCT);
}

int32_t ui_layout_u(int32_t n)
{
    lv_display_t *disp = lv_display_get_default();
    int32_t       hor  = lv_display_get_horizontal_resolution(disp);
    int32_t       ver  = lv_display_get_vertical_resolution(disp);

    return (n * LV_MIN(hor, ver)) / UNITS_PER_SIDE;
}

lv_obj_t *ui_layout_content_create(lv_obj_t *scr)
{
    lv_obj_t *content = lv_obj_create(scr);

    lv_obj_remove_style_all(content);
    lv_obj_set_size(content, lv_pct(100), lv_pct(100));
    lv_obj_set_pos(content, 0, 0);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_set_style_pad_top(content, ui_layout_header_h() + ui_layout_pagebar_h(), 0);
    lv_obj_set_style_pad_bottom(content, ui_layout_hintbar_h(), 0);
    lv_obj_set_style_pad_hor(content, ui_layout_u(CONTENT_PAD_U), 0);

    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(content,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);

    return content;
}

lv_obj_t *ui_layout_column_create(lv_obj_t *content, int32_t width_pct)
{
    lv_obj_t *col = lv_obj_create(content);

    lv_obj_remove_style_all(col);
    lv_obj_set_size(col, lv_pct(width_pct), lv_pct(100));
    lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_set_style_pad_hor(col, ui_layout_u(COLUMN_PAD_U), 0);

    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);

    return col;
}
