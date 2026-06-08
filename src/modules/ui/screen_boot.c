/**
 * @file        screen_boot.c
 * @brief       Boot (splash) screen implementation
 *
 * @details     Builds the first screen shown after firmware start.  The screen
 *              contains a single centred label displaying the product identifier.
 *
 *              There are no interactive widgets and no runtime data bindings —
 *              the content is entirely static.  Navigation away from this screen
 *              is handled externally by ui.c (encoder input → lv_screen_load_anim).
 *
 * @author      Mario Wegmann <mario.wegmann@web.de>
 * @date        Created: 2026-06-02
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
 * 0.1.0    2026-06-02  Mario Wegmann   Initial creation
 * ─────────────────────────────────────────────────────────────────────────────────────────────────
 */

/* ── Corresponding Header ────────────────────────────────────────────────────────────────────── */

#include "modules/ui/screen_boot.h"

/* ── Project Includes ────────────────────────────────────────────────────────────────────────── */

#include "modules/ui/ui_styles.h"


/* ── Private Macros & Constants ──────────────────────────────────────────────────────────────── */

/** @brief Product identifier shown on the boot screen. */
#define BOOT_LABEL_TEXT     "DCU"


/* ── Public Function Implementations ─────────────────────────────────────────────────────────── */

lv_obj_t *screen_boot_create(void)
{
    /* ── Screen base ─────────────────────────────────────────────────────── */

    lv_obj_t *scr = lv_obj_create(NULL);    /* NULL parent → top-level screen */
    lv_obj_remove_style_all(scr);
    lv_obj_add_style(scr, &ui_style_screen, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* ── Product label ───────────────────────────────────────────────────── */

    lv_obj_t *lbl = lv_label_create(scr);
    lv_obj_add_style(lbl, &ui_style_label_value_lg, 0);
    lv_label_set_text(lbl, BOOT_LABEL_TEXT);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);

    return scr;
}
