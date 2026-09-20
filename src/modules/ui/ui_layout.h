/**
 * @file        ui_layout.h
 * @brief       Shared screen geometry — the content area and how to divide it
 *
 * @ingroup     dcu_ui_layout
 *
 * @details     Every screen has the same frame: the header on top, the carousel
 *              page bar under it, the hint bar at the bottom. What is left in
 *              between belongs to the screen. This module says where that is,
 *              once, so that no screen has to work it out — and no screen has to
 *              say in pixels where its widgets go.
 *
 *              ### How a screen is laid out
 *              A screen builds its widgets into containers, and the containers
 *              place them:
 *
 *              @code
 *                lv_obj_t *content = ui_layout_content_create(scr);
 *                lv_obj_t *left    = ui_layout_column_create(content, 50);
 *                lv_obj_t *right   = ui_layout_column_create(content, 50);
 *                // widgets are created with `left` or `right` as their parent
 *              @endcode
 *
 *              The content area is a flex row, the columns are flex columns, and
 *              their widths are shares of the space that is there. Nothing in
 *              between names a coordinate.
 *
 *              ### Sizes that have to be numbers
 *              Some things need a size in pixels all the same — the gap between
 *              two rows, the height of a bar. Those are given in *units*
 *              (ui_layout_u()): a unit is 1/32 of the shorter side of the
 *              display, 10 px on the 480 × 320 panel. A layout written in units
 *              keeps its proportions on another display instead of keeping its
 *              pixels.
 *
 *              ### What does not scale
 *              The fonts. They are compiled at fixed pixel sizes, so a layout can
 *              give a large value the room it needs on this display but cannot
 *              make it smaller on a small one.
 *
 * @author      Mario Wegmann <mario.wegmann@web.de>
 * @date        Created: 2026-09-20
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
 * 0.1.0    2026-09-20  Mario Wegmann   Initial creation
 * ─────────────────────────────────────────────────────────────────────────────────────────────────
 */

#ifndef MODULES_UI_UI_LAYOUT_H
#define MODULES_UI_UI_LAYOUT_H

/* ── LVGL Include ────────────────────────────────────────────────────────────────────────────── */

#include <lvgl.h>

/**
 * @defgroup dcu_ui_layout UI layout
 * @ingroup  dcu_ui
 * @brief The frame every screen shares, and how a screen divides what is left.
 * @{
 */


/* ── The Frame ───────────────────────────────────────────────────────────────────────────────── */

/**
 * @brief Header height as a percentage of the screen height.
 *
 * Owned here rather than in ui_header.c because the content area starts where
 * the header ends; both have to read the same number.
 */
#define UI_LAYOUT_HEADER_PCT    15

/**
 * @brief Height of the carousel page bar under the header, in pixels.
 *
 * The bar is drawn by ui_header.c, but it takes the space below the header on
 * every screen, so the content area has to know about it.
 */
#define UI_LAYOUT_PAGEBAR_H     10


/* ── Public Function Declarations ────────────────────────────────────────────────────────────── */

/**
 * @brief Convert layout units to pixels.
 *
 * One unit is 1/32 of the shorter side of the display: 10 px on the 480 × 320
 * panel. The shorter side is used so that the same layout gives the same sizes
 * whichever way round the panel is mounted.
 *
 * Needs the display to exist, i.e. call it while a screen is being built, not
 * from a static initialiser.
 *
 * @param n  Number of units.
 * @return   The size in pixels.
 */
int32_t ui_layout_u(int32_t n);

/**
 * @brief Create the content area of a screen.
 *
 * A transparent, non-scrolling flex row that fills the screen and keeps clear
 * of the header, the page bar and the hint bar through its padding. Anything a
 * screen wants to show between them goes into it, or into the columns of it.
 *
 * It is created after the header and before the hint bar, so it sits above the
 * one and below the other; it is transparent and does not take input, so that
 * makes no difference to what is seen or pressed.
 *
 * @param scr  The screen to build into.
 * @return     The content container. Never NULL.
 */
lv_obj_t *ui_layout_content_create(lv_obj_t *scr);

/**
 * @brief Create one column in the content area.
 *
 * A transparent flex column, @p width_pct percent of the content width and as
 * tall as the content area, laying its children out from the top, one directly
 * under the other. There is no gap between them: a row brings the air around it
 * itself, and the area is too short for a gap on top of that where a screen
 * stacks four rows. Columns placed side by side should add up to 100; the space
 * between two of them is padding inside the columns, two units in all.
 *
 * @param content    Container from ui_layout_content_create().
 * @param width_pct  Share of the content width, 1 to 100.
 * @return           The column container. Never NULL.
 */
lv_obj_t *ui_layout_column_create(lv_obj_t *content, int32_t width_pct);

/** @} */ /* dcu_ui_layout */

#endif /* MODULES_UI_UI_LAYOUT_H */
