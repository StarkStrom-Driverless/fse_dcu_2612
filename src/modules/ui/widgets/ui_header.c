/**
 * @file
 * @brief       Reusable header widget — title bar with device status icons
 *
 * @ingroup     dcu_ui_widgets
 *
 * @details     Implementation of the header bar; the contract is in
 *              ui_header.h.
 *
 *              ### Object tree built here
 *              ```
 *              parent (the screen)
 *                ├─ header                styled with ui_style_header, a flex row
 *                │    ├─ title label      takes the free width, vertically centered
 *                │    └─ icon row (flex)  right, vertically centered
 *                │         └─ one 24×24 container per device slot
 *                │              └─ icon label in FontAwesome_Solid_18
 *                └─ page-indicator bar    full width, directly below the header
 *                                         segments painted, not child objects
 *              ```
 *
 *              Two objects, not one: the indicator is a sibling of the header
 *              rather than a child, so it carries its own background instead of
 *              inheriting the header's gradient — and the header's own layout
 *              is untouched by it.
 *
 *              The bar has no children. Its segments are drawn in a
 *              LV_EVENT_DRAW_MAIN_END callback, which is both cheaper than one
 *              object per segment and what allows them to be parallelograms;
 *              see page_bar_draw_cb().
 *
 *              Sizes are shares of the display or layout units (ui_layout.h);
 *              what stays in pixels is tied to a glyph: the 24 px icon cell and
 *              the 16 px hint-bar icons follow their font and asset sizes.
 *
 *              The per-slot container exists so blinking can change the
 *              container's opacity while the icon label keeps its own color,
 *              and so the flex row keeps a fixed cell size no matter which
 *              glyph is in it.
 *
 *              ### Blinking
 *              All blinking slots observe one shared phase subject driven by a
 *              single LVGL timer, so they blink in step and cannot drift apart.
 *              The timer only exists while at least one slot is blinking.
 *
 *              Opacity is used rather than LV_OBJ_FLAG_HIDDEN because hiding
 *              an object removes it from the flex layout, which would make the
 *              neighboring icons jump sideways twice a second.
 *
 * @author      Mario Wegmann <mario.wegmann@web.de>
 * @date        Created: 2026-08-04
 *
 * @copyright   Copyright (c) 2026 Mario Wegmann.
 *              SPDX-License-Identifier: Apache-2.0
 */

/* ── Corresponding Header ────────────────────────────────────────────────────────────────────── */

#include "modules/ui/widgets/ui_header.h"

/* ── LVGL Include ────────────────────────────────────────────────────────────────────────────── */

#include <lvgl.h>

/* ── Project Includes ────────────────────────────────────────────────────────────────────────── */

#include "modules/ui/ui.h"
#include "modules/ui/ui_layout.h"
#include "modules/ui/ui_styles.h"
#include "services/event_bus/events.h"

/* ── Layout constants ────────────────────────────────────────────────────── */

/**
 * @brief Header height as a percentage of the screen height.
 *
 * Defined in ui_layout.h: the content area of every screen starts where the
 * header ends.
 */
#define HEADER_HEIGHT_PCT   UI_LAYOUT_HEADER_PCT

/** @brief Padding at the left edge of the header, in layout units. */
#define HEADER_PAD_L_U      1

/**
 * @brief Padding at the right edge of the header, in half layout units.
 *
 * Smaller than the left one: the icon cells carry a few pixels of air of their
 * own, so the glyphs still stand as far from the edge as the title does.
 */
#define HEADER_PAD_R_HALF_U 1

/** @brief Gap between two icons, in half layout units. */
#define ICON_COL_GAP_HALF_U 1

/** @brief Half period of the blink cycle — one full cycle is twice this. */
#define BLINK_HALF_MS       400U

/**
 * @name Page-indicator geometry, as shares of the bar's height
 *
 * Shares and not pixels, so the segments keep their proportions when the bar
 * grows with the display. The bar is UI_LAYOUT_PAGEBAR_U layout units high, 10
 * px on the 480 × 320 panel, which gives 4, 6 and 6 px.
 * @{
 */

/**
 * @brief Height of a single segment.
 *
 * Less than the bar on purpose: the difference is what shows above and below
 * the segments once they are centered, turning the bar's dark background into a
 * frame around them rather than a backdrop behind them.
 *
 * The slant of the sides equals this height, which is what makes them run at
 * exactly 45° — parallel to the diagonal color edge in the header above (the
 * gradient axis in init_header_style() is (1,1), so its iso-color lines rise to
 * the right at 45°). Change this and the segments stop being parallel to that
 * edge; change the gradient and this has to follow.
 */
#define PAGE_SEG_PCT        40

/**
 * @brief Gap between two segments.
 *
 * The segments divide the full width; this is the only thing separating them.
 * The gaps show the bar's dark background, so they read as separators without
 * competing with the green marker. Set to 0 for one continuous bar.
 */
#define PAGE_GAP_PCT        60

/**
 * @brief Inset at the left and right end of the bar.
 *
 * Expressed as padding on the bar because the segments are laid out inside its
 * content area — narrowing that area shrinks every segment by the same amount
 * instead of only the outer two.
 */
#define PAGE_PAD_PCT        60

/** @} */

/**
 * @brief A share of the page bar's height, in pixels.
 *
 * @param bar_h  Height of the bar.
 * @param pct    Share in percent.
 * @return       The size in pixels.
 */
static inline int32_t page_share(int32_t bar_h, int32_t pct)
{
    return (bar_h * pct) / 100;
}

/* ── Per-slot icon configuration ─────────────────────────────────────────── */

/**
 * @brief Icon for each device slot.
 *
 * Designated initializers keep the table tied to enum ui_device_slot rather
 * than to a hand-counted order; the loop below indexes it with the same value
 * it uses for the status subject.
 */
static const ui_header_slot_cfg_t k_slot_cfg[UI_DEVICE_SLOT_COUNT] = {
    [UI_DEVICE_LOGGER]  = { .symbol = FA_SYMBOL_VIDEO         },
    [UI_DEVICE_ROS]     = { .symbol = FA_SYMBOL_ROBOT         },
    [UI_DEVICE_DV_PC]   = { .symbol = FA_SYMBOL_DESKTOP_SOLID },
    [UI_DEVICE_KISTLER] = { .symbol = FA_SYMBOL_RULER         },
    [UI_DEVICE_MABX]    = { .symbol = FA_SYMBOL_MICROCHIP     },
    [UI_DEVICE_SDCS]    = { .symbol = FA_SYMBOL_POWER_OFF     },
    [UI_DEVICE_CAN]     = { .symbol = FA_SYMBOL_NETWORK_SOLID },
};

/* ── Private helpers ─────────────────────────────────────────────────────── */

/**
 * @brief Map a device status to its icon color.
 *
 * FAULT and ACTIVE share the same red; they are told apart by the blinking,
 * not by the color.
 *
 * @param s  Device status.
 * @return   Color for the icon label. Unknown values fall back to green.
 */
static lv_color_t status_to_color(enum ui_device_status s)
{
    switch (s) {
    case UI_DEVICE_STATUS_WARN:     return UI_C_ACCENT;
    case UI_DEVICE_STATUS_FAULT:    return UI_C_RED;
    case UI_DEVICE_STATUS_ACTIVE: return UI_C_RED;
    case UI_DEVICE_STATUS_OK:       return UI_C_GREEN;
    default:                        return UI_C_GREEN;
    }
}

/* ── Shared blink state ──────────────────────────────────────────────────── */
/*
 * All blinking slot containers observe a single phase subject toggled by one
 * shared timer.  This guarantees perfect sync (same subject notification round)
 * and zero drift (one timer, no per-slot phase offset).
 *
 * The state is file-scope rather than per-header: only one screen is loaded at
 * a time, so at most one header exists, and the counter is what keeps the
 * timer's lifetime tied to actual demand across screen changes.
 */

/** @brief Shared blink phase: 0 = opaque, 1 = transparent. */
static lv_subject_t s_blink_phase;

/** @brief The one timer toggling s_blink_phase; NULL while nothing blinks. */
static lv_timer_t  *s_blink_timer;

/** @brief Number of containers currently blinking; the timer's reference count. */
static uint32_t     s_blink_count;

/**
 * @brief Observer on the blink phase — applies it to one slot container.
 *
 * @param observer  Observer whose target object is the slot container.
 * @param subject   The shared phase subject.
 */
static void blink_phase_cb(lv_observer_t *observer, lv_subject_t *subject)
{
    lv_obj_t *cont = lv_observer_get_target_obj(observer);
    lv_opa_t opa = lv_subject_get_int(subject) ? LV_OPA_TRANSP : LV_OPA_COVER;
    lv_obj_set_style_opa(cont, opa, 0);
}

/**
 * @brief Timer callback — flip the shared blink phase.
 *
 * @param timer  Unused.
 */
static void blink_tick_cb(lv_timer_t *timer)
{
    (void)timer;
    lv_subject_set_int(&s_blink_phase, !lv_subject_get_int(&s_blink_phase));
}

/**
 * @brief LV_EVENT_DELETE handler — release a blinking container's timer share.
 *
 * Registered once on every slot container, when the slot is built. It does
 * something only if the container is blinking at that moment, which its user
 * data says: blink_stop() clears it, so a container that already gave its
 * share back does not give it back a second time. Registering it on every
 * blink_start() instead added another copy each time and counted a share down
 * once per copy.
 *
 * A screen change while an icon blinks therefore cannot leak the timer, and
 * cannot stop it under the icons that still blink.
 *
 * @param e  LV_EVENT_DELETE from the slot container.
 */
static void blink_delete_event_cb(lv_event_t *e)
{
    lv_obj_t *cont = lv_event_get_target_obj(e);

    if (lv_obj_get_user_data(cont) == NULL) {
        return;   /* not blinking, nothing to release */
    }
    /*
     * lv_subject_add_observer_obj already registered its own LV_EVENT_DELETE
     * callback that calls lv_observer_remove() and frees the observer before
     * this callback runs.  Do NOT touch the observer pointer here — it is
     * already freed memory.  Only manage the shared count and timer.
     */
    if (s_blink_count > 0 && --s_blink_count == 0) {
        lv_timer_delete(s_blink_timer);
        s_blink_timer = NULL;
    }
}

/**
 * @brief Start blinking one slot container.
 *
 * Creates the shared timer if this is the first blinking container, subscribes
 * the container to the phase subject, and stores the observer in the
 * container's user data — which doubles as the "is blinking" flag.
 *
 * @param cont  Slot container.
 */
static void blink_start(lv_obj_t *cont)
{
    if (lv_obj_get_user_data(cont) != NULL) {
        return; /* already subscribed */
    }
    if (s_blink_count == 0) {
        lv_subject_init_int(&s_blink_phase, 0);
        s_blink_timer = lv_timer_create(blink_tick_cb, BLINK_HALF_MS, NULL);
    }
    s_blink_count++;
    lv_observer_t *obs = lv_subject_add_observer_obj(&s_blink_phase,
                                                      blink_phase_cb,
                                                      cont, NULL);
    lv_obj_set_user_data(cont, obs);
}

/**
 * @brief Stop blinking one slot container and restore full opacity.
 *
 * Deletes the shared timer once the last blinking container has gone.
 *
 * @param cont  Slot container. Doing this on a non-blinking one is a no-op.
 */
static void blink_stop(lv_obj_t *cont)
{
    lv_observer_t *obs = lv_obj_get_user_data(cont);
    if (obs == NULL) {
        return;
    }
    lv_observer_remove(obs);
    lv_obj_set_user_data(cont, NULL);
    lv_obj_set_style_opa(cont, LV_OPA_COVER, 0);
    if (s_blink_count > 0 && --s_blink_count == 0) {
        lv_timer_delete(s_blink_timer);
        s_blink_timer = NULL;
    }
}

/**
 * @brief Observer callback — apply a device status to its slot.
 *
 * Fired by LVGL whenever the slot's status subject changes, and once on
 * subscription so the initial state needs no separate call.
 *
 * Recolors the icon, then starts or stops blinking. Comparing against the
 * subject's previous value keeps that a transition, so the blink counter is
 * incremented and decremented exactly once per state change.
 *
 * Child layout inside cont (fixed insertion order from ui_header_create()):
 *   child[0] → icon label
 *
 * @param observer  Observer whose target object is the slot container.
 * @param subject   The slot's status subject; holds a @ref ui_device_status.
 */
static void slot_status_observer_cb(lv_observer_t *observer, lv_subject_t *subject)
{
    lv_obj_t *cont    = lv_observer_get_target_obj(observer);
    lv_obj_t *img     = lv_obj_get_child(cont, 0);

    enum ui_device_status cur  = (enum ui_device_status)lv_subject_get_int(subject);
    enum ui_device_status prev = (enum ui_device_status)lv_subject_get_previous_int(subject);

    lv_obj_set_style_text_color(img, status_to_color(cur), LV_PART_MAIN);

    bool should_blink = (cur  == UI_DEVICE_STATUS_ACTIVE);
    bool was_blinking = (prev == UI_DEVICE_STATUS_ACTIVE);

    if (should_blink && !was_blinking) {
        blink_start(cont);
    } else if (!should_blink && was_blinking) {
        blink_stop(cont);
    }
}

/* ── Page indicator ──────────────────────────────────────────────────────── */

/**
 * @brief Draw the carousel segments as 45° parallelograms.
 *
 * Runs after the bar has painted its own dark background, so the segments land
 * on top of it.
 *
 * ### Why draw instead of building objects
 *
 * The segments used to be one child object each. Drawing them directly is
 * cheaper on every count that matters here: no per-segment object, no style
 * storage, no layout pass — and a filled triangle costs a scanline fill just
 * like a filled rectangle does, so the slanted shape is free. Ten objects
 * become one.
 *
 * It also costs nothing per frame. LVGL only redraws invalidated areas, and
 * nothing invalidates this bar once the screen is built; a navigation rebuilds
 * the screen anyway.
 *
 * ### How each segment is cut up
 *
 * Three pieces, not two:
 *
 * ```
 *        tl ┌───┬───────┬───┐ tr
 *          / A │   B    │ C /
 *      bl └───┴───────┴───┘ br
 * ```
 *
 * B is a plain fill, A and C are the triangular wedges left and right of it.
 *
 * The obvious split — two triangles sharing the bl→tr diagonal — leaves a
 * visible seam down the middle of every segment. Each triangle antialiases its
 * own side of the shared edge, and two half-covered pixels do not add up to one
 * covered pixel: the dark background shows through as a gray line. Because the
 * segments are wide and flat, that diagonal runs almost horizontally through
 * the middle, which is exactly where it is most obvious.
 *
 * Splitting on the two vertical lines instead puts every seam on an
 * axis-aligned edge at an integer coordinate, where coverage is all-or-nothing
 * and no blending happens. The wedges keep their diagonals, but those are
 * outer edges against the background, not seams between two shapes.
 *
 * ### Geometry
 *
 * The usable width has the slant (one segment height) subtracted before the segments divide
 * it, so the rightmost top corner lands exactly on the content edge instead of
 * being clipped.
 *
 * The position is read here rather than captured at build time, so a redraw
 * always reflects the current screen. Outside the carousel the bar is hidden
 * (see page_bar_screen_loaded_cb()), so this callback never runs with
 * UI_CAROUSEL_POS_NONE.
 *
 * @param e  LV_EVENT_DRAW_MAIN_END from the bar.
 */
static void page_bar_draw_cb(lv_event_t *e)
{
    lv_obj_t   *bar   = lv_event_get_target_obj(e);
    lv_layer_t *layer = lv_event_get_layer(e);

    const int32_t count = (int32_t)ui_carousel_get_length();
    const int32_t pos   = (int32_t)ui_carousel_get_position();

    lv_area_t content;
    lv_obj_get_content_coords(bar, &content);

    const int32_t bar_h = lv_obj_get_height(bar);
    const int32_t seg_h = page_share(bar_h, PAGE_SEG_PCT);   /* = the slant, 45° */
    const int32_t gap   = page_share(bar_h, PAGE_GAP_PCT);

    const int32_t usable = lv_area_get_width(&content)
                           - seg_h
                           - ((count - 1) * gap);
    if (usable < count) {
        return;   /* narrower than one pixel per segment — nothing to say */
    }

    const int32_t seg_w = usable / count;
    const int32_t y_top = content.y1
                          + (lv_area_get_height(&content) - seg_h) / 2;
    const int32_t y_bot = y_top + seg_h;

    lv_draw_triangle_dsc_t tri;
    lv_draw_triangle_dsc_init(&tri);
    tri.opa = LV_OPA_COVER;

    lv_draw_fill_dsc_t fill;
    lv_draw_fill_dsc_init(&fill);
    fill.opa = LV_OPA_COVER;

    for (int32_t i = 0; i < count; i++) {
        const int32_t x_bl = content.x1 + (i * (seg_w + gap));
        const int32_t x_br = x_bl + seg_w;
        const int32_t x_tl = x_bl + seg_h;
        const int32_t x_tr = x_br + seg_h;

        const lv_color_t color = (i == pos) ? UI_C_GREEN : UI_C_WHITE;
        tri.color  = color;
        fill.color = color;

        /* A — left wedge, from the bottom-left corner up to the top edge. */
        tri.p[0].x = x_bl; tri.p[0].y = y_bot;
        tri.p[1].x = x_tl; tri.p[1].y = y_bot;
        tri.p[2].x = x_tl; tri.p[2].y = y_top;
        lv_draw_triangle(layer, &tri);

        /*
         * B — the full-height middle, where both edges are present.
         *
         * lv_area_t is inclusive on both ends. The right bound deliberately
         * reaches x_br, one column into wedge C, so that however the triangle
         * rasterizer rounds its vertical edge there can be no hairline gap
         * between the two. Both pieces are opaque and the same color, so the
         * overlap is invisible.
         */
        const lv_area_t mid = {
            .x1 = x_tl, .y1 = y_top,
            .x2 = x_br, .y2 = y_bot - 1,
        };
        lv_draw_fill(layer, &fill, &mid);

        /* C — right wedge, from the top-right corner down to the bottom edge. */
        tri.p[0].x = x_br; tri.p[0].y = y_bot;
        tri.p[1].x = x_tr; tri.p[1].y = y_top;
        tri.p[2].x = x_br; tri.p[2].y = y_top;
        lv_draw_triangle(layer, &tri);
    }
}

/**
 * @brief Show or hide the bar with the screen it belongs to.
 *
 * Runs on LV_EVENT_SCREEN_LOADED, which the display sends while the screen is
 * being loaded — by then ui.c has recorded the new active screen, so the
 * position is the one for this screen and not for the previous one.
 *
 * Screens outside the carousel (EV DRIVING, DV DRIVING) have no position to
 * show and hide the bar entirely, rather than showing a bar with no segment
 * lit.
 *
 * @param e  LV_EVENT_SCREEN_LOADED from the screen; user data is the bar.
 */
static void page_bar_screen_loaded_cb(lv_event_t *e)
{
    lv_obj_t *bar = lv_event_get_user_data(e);

    lv_obj_set_flag(bar, LV_OBJ_FLAG_HIDDEN,
                    ui_carousel_get_position() == UI_CAROUSEL_POS_NONE);
}

/**
 * @brief Build the carousel position indicator directly below the header.
 *
 * One segment per carousel screen, dividing the full display width: white for
 * the screens the driver is not on, UI_C_GREEN for the current one. Position
 * reads as a proportion of the whole width, so it can be taken in without
 * counting — which is what a driver glancing down from the track has time for.
 *
 * A sibling of the header, not a child of it, so it carries its own dark
 * background instead of inheriting the header's gradient. Aligning to the
 * header object rather than to a computed offset keeps the two together if
 * HEADER_HEIGHT_PCT ever changes.
 *
 * The bar itself is a single empty object; page_bar_draw_cb() paints the
 * segments into it. Its padding is what defines the content area they divide.
 *
 * The dark background shows through in three places, and all three are
 * deliberate: as the inset at both ends (PAGE_PAD_PCT), as the separators
 * between segments (PAGE_GAP_PCT), and as the band above and below them
 * (the bar height minus PAGE_SEG_PCT).
 *
 * Suppressed for a carousel of one, where the indicator would state the
 * obvious, and hidden on screens outside the carousel.
 *
 * The bar starts hidden: the screen is built before it is loaded, so the
 * position is not known yet here. page_bar_screen_loaded_cb() reveals it while
 * the screen loads, which is still before the first frame is drawn — there is
 * no flicker.
 *
 * @param parent  Screen the header was built on; the bar becomes its child.
 * @param header  Header container, used only as the alignment reference.
 */
static void page_indicator_create(lv_obj_t *parent, lv_obj_t *header)
{
    if (ui_carousel_get_length() <= 1U) {
        return;
    }

    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_remove_style_all(bar);
    lv_obj_set_width(bar, lv_pct(100));
    lv_obj_set_height(bar, ui_layout_pagebar_h());
    lv_obj_align_to(bar, header, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);
    lv_obj_set_style_pad_hor(bar, page_share(ui_layout_pagebar_h(), PAGE_PAD_PCT), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(bar, UI_C_DARK, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(bar, LV_OBJ_FLAG_HIDDEN);

    lv_obj_add_event_cb(bar, page_bar_draw_cb, LV_EVENT_DRAW_MAIN_END, NULL);
    lv_obj_add_event_cb(parent, page_bar_screen_loaded_cb,
                        LV_EVENT_SCREEN_LOADED, bar);
}


/* ── Public API ──────────────────────────────────────────────────────────── */

lv_obj_t *ui_header_create(lv_obj_t    *parent,
                            const char  *title,
                            lv_subject_t status_subjects[])
{
    /* ── Header container ──────────────────────────────────────────────── */
    lv_obj_t *header = lv_obj_create(parent);
    lv_obj_remove_style_all(header);
    lv_obj_add_style(header, &ui_style_header, 0);
    lv_obj_set_width(header,  lv_pct(100));
    lv_obj_set_height(header, lv_pct(HEADER_HEIGHT_PCT));
    lv_obj_align(header, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    /*
     * The header is a flex row: the title takes what the icons leave, the
     * icons stand at the right edge, both centered on the header's height.
     */
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(header, ui_layout_u(HEADER_PAD_L_U), 0);
    lv_obj_set_style_pad_right(header, (ui_layout_u(HEADER_PAD_R_HALF_U)) / 2, 0);

    /* ── Title label (left side) — child index 0 of header ─────────────── */
    lv_obj_t *lbl = lv_label_create(header);
    lv_obj_add_style(lbl, &ui_style_label_title, 0);
    lv_label_set_text(lbl, title);
    lv_obj_set_flex_grow(lbl, 1);

    /* ── Icon row (right side, flex) — child index 1 of header ─────────── */
    lv_obj_t *icon_row = lv_obj_create(header);
    lv_obj_remove_style_all(icon_row);
    lv_obj_set_size(icon_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_layout(icon_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(icon_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(icon_row,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(icon_row, ui_layout_u(ICON_COL_GAP_HALF_U) / 2, 0);
    lv_obj_clear_flag(icon_row, LV_OBJ_FLAG_SCROLLABLE);

    /* ── One slot per device ────────────────────────────────────────────── */
    for (uint8_t i = 0U; i < UI_DEVICE_SLOT_COUNT; i++) {
        /* Slot container — child[i] of icon_row */
        lv_obj_t *cont = lv_obj_create(icon_row);
        
        lv_obj_remove_style_all(cont);
        lv_obj_set_size(cont, 24U, 24U);
        lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(cont, blink_delete_event_cb, LV_EVENT_DELETE, NULL);

        /* Device icon — child[0] of cont */
        lv_obj_t *img = lv_label_create(cont);
        lv_obj_set_style_text_font(img, &FontAwesome_Solid_18, 0);
        lv_label_set_text(img, k_slot_cfg[i].symbol);
        lv_obj_center(img);

        /*
         * Subscribe cont to its status subject.
         * LVGL fires the callback immediately with the current value so the
         * initial visual state is applied without a separate call.
         * The observer is automatically removed when cont is deleted.
         */
        lv_subject_add_observer_obj(&status_subjects[i],
                                    slot_status_observer_cb,
                                    cont,
                                    NULL);
    }

    /* ── Page indicator — sibling of the header, directly below it ──────── */
    page_indicator_create(parent, header);

    return header;
}
