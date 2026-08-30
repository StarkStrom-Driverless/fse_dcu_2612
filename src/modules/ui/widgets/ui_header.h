/**
 * @file        ui_header.h
 * @brief       Reusable header widget — title bar with device status icons
 *
 * @ingroup     dcu_ui_widgets
 *
 * @details     Creates a full-width header bar (15 % of screen height) with:
 *
 *              | Position     | Content                                      |
 *              |--------------|----------------------------------------------|
 *              | Left         | Screen title (ui_style_label_title)          |
 *              | Right        | One icon per @ref ui_device_slot, independently colored and optionally blinking |
 *              | Below the bar| Page indicator: one segment per carousel screen, across the full width |
 *
 *              Every screen puts one of these at the top, so the vehicle's
 *              health and the driver's position in the carousel stay visible
 *              no matter where they have navigated.
 *
 *              ### Page indicator
 *
 *              A full-width bar under the header, divided into one segment per
 *              carousel screen: white for the others, UI_C_GREEN for the
 *              current one. Position reads as a proportion of the whole width,
 *              so it can be taken in without counting — which is what a driver
 *              glancing down from the track actually has time for. The carousel
 *              has no other visible structure; without it the only way to find
 *              out how far the ends are is to turn the encoder until it stops.
 *
 *              The segments are 45° parallelograms, matching the diagonal
 *              colour edge of the header above them. They are painted in a
 *              draw callback rather than built as objects, which costs less
 *              than the ten objects it replaces.
 *
 *              Two things follow from it being a *sibling* of the header rather
 *              than part of it: it keeps the screen background instead of the
 *              header gradient, and it adds its own height below the header.
 *              Screens that place content by absolute offset should leave the
 *              first few pixels under the header free.
 *
 *              The widget reads the position itself, through
 *              ui_carousel_get_position() — it is not a parameter, and the
 *              creating screen neither knows nor needs to know its own index.
 *              Read once while building, which is correct because navigation
 *              destroys and rebuilds the screen, and with it this header. See
 *              page_indicator_create() in ui_header.c for what that assumes.
 *
 *              Each slot subscribes to its corresponding lv_subject_t from the
 *              @p status_subjects array passed at creation time.  Updates are
 *              fully reactive — no explicit set-status call is needed after
 *              creation, and the callback fires once on subscription so a
 *              freshly built header shows the current state immediately.
 *              LVGL removes the observers when the header object is deleted,
 *              which is what makes the widget safe under the create-on-visit /
 *              delete-on-leave screen lifecycle.
 *
 *              ### Status colors
 *
 *              | Status                  | Icon color | Animation |
 *              |-------------------------|-------------|-----------|
 *              | UI_DEVICE_STATUS_OK     | UI_C_GREEN  | solid     |
 *              | UI_DEVICE_STATUS_WARN   | UI_C_ACCENT | solid     |
 *              | UI_DEVICE_STATUS_FAULT  | UI_C_RED    | solid     |
 *              | UI_DEVICE_STATUS_ACTIVE | UI_C_RED    | blinking  |
 *
 *              ### Usage
 *              @code
 *                // In screen_X_create() — status_subjects is the argument
 *                // every screen factory receives from ui.c:
 *                ui_header_create(scr, "MY SCREEN", status_subjects);
 *              @endcode
 *
 *              The return value is normally discarded: the header positions
 *              itself and needs no further calls.
 *
 * @author      Mario Wegmann <mario.wegmann@web.de>
 * @date        Created: 2026-08-04
 *
 * @copyright   Copyright (c) 2026 Mario Wegmann.
 *              SPDX-License-Identifier: Apache-2.0
 */

#ifndef MODULES_UI_WIDGETS_UI_HEADER_H
#define MODULES_UI_WIDGETS_UI_HEADER_H

#include <lvgl.h>
#include "services/event_bus/events.h"

/**
 * @defgroup dcu_ui_widgets UI widgets
 * @ingroup  dcu_ui
 * @brief Composite widgets shared across screens.
 * @{
 */

/**
 * @brief Per-slot icon configuration for the header widget.
 *
 * The @p symbol field accepts any UTF-8 symbol string, but in practice a
 * FA_SYMBOL_* define from ui_styles.h: the icon labels are rendered in
 * FontAwesome_Solid_18, so an LVGL built-in symbol would come out as a
 * missing glyph.
 *
 * The table itself (@c k_slot_cfg, one entry per @ref ui_device_slot) lives in
 * ui_header.c and is not configurable per screen — every header looks the
 * same. The type is exposed so the table can be moved out later without an
 * API change.
 */
typedef struct {
    const char *symbol; /**< UTF-8 symbol string for this slot's icon. */
} ui_header_slot_cfg_t;

/**
 * @brief Create a header widget as a child of @p parent.
 *
 * The widget occupies the full width and 15 % of the screen height, pinned
 * to the top-left corner.  Each device slot subscribes to its element in
 * @p status_subjects and reacts automatically when the subject value changes.
 *
 * @param parent          Screen object (lv_obj_create(NULL)).
 * @param title           UTF-8 string shown on the left side.  Copied by
 *                        lv_label_set_text(), so it need not outlive the call.
 * @param status_subjects Array of UI_DEVICE_SLOT_COUNT subjects, indexed by
 *                        enum ui_device_slot.  Must remain valid for the
 *                        widget's lifetime — the file-scope array in ui.c is
 *                        what every screen passes.
 * @return                The header lv_obj_t, owned by the LVGL object tree.
 */
lv_obj_t *ui_header_create(lv_obj_t    *parent,
                            const char  *title,
                            lv_subject_t status_subjects[]);

/** @} */ /* dcu_ui_widgets */

#endif /* MODULES_UI_WIDGETS_UI_HEADER_H */
