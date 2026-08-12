/**
 * @file        screen_boot.c
 * @brief       Boot (splash) screen implementation
 *
 * @ingroup     dcu_ui_screens
 *
 * @details     Builds the first screen shown after firmware start: the shared
 *              header, the StarkStrom gear logo, the product name and the
 *              firmware version.
 *
 *              There are no interactive widgets and no data bindings.  The one
 *              dynamic element is an LVGL animation that rotates the outer gear
 *              continuously; it is owned by the object and dies with it, so
 *              nothing has to stop it when the screen is deleted.
 *
 *              Navigation away from this screen is handled by ui.c.
 *
 * @author      Mario Wegmann <mario.wegmann@web.de>
 * @date        Created: 2026-06-02
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
 * 0.1.0    2026-06-02  Mario Wegmann   Initial creation
 * ─────────────────────────────────────────────────────────────────────────────────────────────────
 */

/* ── Corresponding Header ────────────────────────────────────────────────────────────────────── */

#include "modules/ui/screens/screen_boot.h"

/* ── Project Includes ────────────────────────────────────────────────────────────────────────── */

#include <app_version.h>

#include "modules/ui/ui_styles.h"
#include "modules/ui/widgets/ui_header.h"


/* ── Private Macros & Constants ──────────────────────────────────────────────────────────────── */

/* ── Public Function Implementations ─────────────────────────────────────────────────────────── */

lv_obj_t *screen_boot_create(lv_subject_t *status_subjects)
{
    /* ── Screen base ─────────────────────────────────────────────────────── */

    lv_obj_t *scr = lv_obj_create(NULL);    /* NULL parent → top-level screen */
    lv_obj_remove_style_all(scr);
    lv_obj_add_style(scr, &ui_style_screen, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* ── Widgets ─────────────────────────────────────────────────────────── */

    ui_header_create(scr, "START", status_subjects);

    /* ── Product label ───────────────────────────────────────────────────── */

    lv_obj_t *lbl_dcu = lv_label_create(scr);
    lv_obj_add_style(lbl_dcu, &ui_style_label_value_md, 0);
    lv_label_set_text(lbl_dcu, "DCU");
    lv_obj_align(lbl_dcu, LV_ALIGN_CENTER, -5, 90);

    /*
     * Version comes from the VERSION file via app_version.h, so it cannot
     * drift from the release.  DCU_VEHICLE_ID (CMakeLists.txt) is the public
     * major number — 2612 — which does not fit in Zephyr's 8-bit
     * APP_VERSION_MAJOR and is derived from it instead.
     */
    lv_obj_t *lbl_version = lv_label_create(scr);
    lv_obj_add_style(lbl_version, &ui_style_label_title, 0);
    lv_label_set_text_fmt(lbl_version, "v%d.%d.%d",
                          DCU_VEHICLE_ID, APP_VERSION_MINOR, APP_PATCHLEVEL);
    lv_obj_align(lbl_version, LV_ALIGN_CENTER, -5, 130);

    /* ── Logo ────────────────────────────────────────────────────────────── */

    /*
     * Both gears are A8 images — alpha only, no color of their own — so the
     * recolor below is what gives them the StarkStrom orange and red. Storing
     * one channel instead of three is what keeps them affordable in flash.
     *
     * Only the outer ring turns; the inner gear is drawn on top of it and
     * stays put.
     */
    LV_IMAGE_DECLARE(outer_gear_a8);

    lv_obj_t *img_outer_gear = lv_image_create(scr);
    lv_image_set_src(img_outer_gear, &outer_gear_a8);
    lv_obj_set_style_image_recolor(img_outer_gear, lv_color_hex(0xfa6e00), LV_PART_MAIN);
    lv_obj_set_style_image_recolor_opa(img_outer_gear, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_align(img_outer_gear, LV_ALIGN_CENTER, 0, -30);

    lv_image_set_pivot(img_outer_gear,
                       outer_gear_a8.header.w / 2,
                       outer_gear_a8.header.h / 2);

    /*
     * 0 … 3600 in tenths of a degree is one full turn every 6 s, repeating
     * forever.  lv_anim_start() copies the descriptor, so the local is fine.
     */
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, img_outer_gear);
    lv_anim_set_exec_cb(&anim, (lv_anim_exec_xcb_t)lv_image_set_rotation);
    lv_anim_set_values(&anim, 0, 3600);
    lv_anim_set_duration(&anim, 6000);
    lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&anim);

    LV_IMAGE_DECLARE(inner_gear_a8);

    lv_obj_t *img_inner_gear = lv_image_create(scr);
    lv_image_set_src(img_inner_gear, &inner_gear_a8);
    lv_obj_set_style_image_recolor(img_inner_gear, lv_color_hex(0xcd1a17), LV_PART_MAIN);
    lv_obj_set_style_image_recolor_opa(img_inner_gear, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_align(img_inner_gear, LV_ALIGN_CENTER, 0, -30);

    return scr;
}
