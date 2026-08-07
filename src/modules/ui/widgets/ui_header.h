/**
 * @file        ui_header.h
 * @brief       Reusable header widget — title bar with device status icons
 *
 * @details     Creates a full-width header bar (15 % of screen height) with:
 *
 *              Left side:   Screen title label (ui_style_label_title)
 *              Right side:  Row of status icon slots (Kistler / DV PC /
 *                           Logger / EBS), each independently coloured and
 *                           optionally blinking.
 *
 *              Each slot subscribes to its corresponding lv_subject_t from the
 *              @p status_subjects array passed at creation time.  Updates are
 *              fully reactive — no explicit set-status call is needed after
 *              creation.  LVGL automatically removes the observers when the
 *              header object is deleted.
 *
 *              Status colours
 *              ──────────────
 *              UI_DEVICE_STATUS_OK       → UI_C_GREEN (solid)
 *              UI_DEVICE_STATUS_WARN     → UI_C_ACCENT gold (solid)
 *              UI_DEVICE_STATUS_FAULT    → UI_C_RED
 *              UI_DEVICE_STATUS_ACTIVE   → UI_C_RED + blink
 *
 *              Usage
 *              ─────
 *              @code
 *                // In screen_X_create() — subjects live in ui.c:
 *                lv_obj_t *hdr = ui_header_create(scr, "MY SCREEN", status_subjects);
 *              @endcode
 *
 * @author      Mario Wegmann <mario.wegmann@web.de>
 * @date        Created: 2026-08-04
 */

#ifndef MODULES_UI_WIDGETS_UI_HEADER_H
#define MODULES_UI_WIDGETS_UI_HEADER_H

#include <lvgl.h>
#include "services/event_bus/events.h"

/**
 * @brief Per-slot icon configuration for the header widget.
 *
 * Define one entry per @ref ui_device_slot value.  The @p symbol field
 * accepts any UTF-8 symbol string: Font Awesome defines from ui_styles.h
 * (FA_SYMBOL_*) or LVGL built-in symbols (LV_SYMBOL_*).
 *
 * Example (in ui.c):
 * @code
 *   static const ui_header_slot_cfg_t k_header_slot_cfg[UI_DEVICE_SLOT_COUNT] = {
 *       [UI_DEVICE_KISTLER] = { .symbol = FA_SYMBOL_RULER         },
 *       [UI_DEVICE_DV_PC]   = { .symbol = FA_SYMBOL_DESKTOP_SOLID },
 *       [UI_DEVICE_LOGGER]  = { .symbol = FA_SYMBOL_VIDEO         },
 *       [UI_DEVICE_EBS]     = { .symbol = FA_SYMBOL_GAUGE_SIMPLE  },
 *   };
 * @endcode
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
 * @param title           UTF-8 string shown on the left side.  Must remain
 *                        valid for the widget's lifetime (literals are fine).
 * @param status_subjects Array of UI_DEVICE_SLOT_COUNT subjects, indexed by
 *                        enum ui_device_slot.  Must remain valid for the
 *                        widget's lifetime (static storage in ui.c is fine).
 * @return                The header lv_obj_t.
 */
lv_obj_t *ui_header_create(lv_obj_t    *parent,
                            const char  *title,
                            lv_subject_t status_subjects[]);

#endif /* MODULES_UI_WIDGETS_UI_HEADER_H */
