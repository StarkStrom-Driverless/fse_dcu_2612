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

#include "modules/ui/screens/screen_boot.h"

/* ── Project Includes ────────────────────────────────────────────────────────────────────────── */

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

    ui_header_create(scr, "BOOT", status_subjects);

    /* ── Product label ───────────────────────────────────────────────────── */

    lv_obj_t *lbl_dcu = lv_label_create(scr);
    lv_obj_add_style(lbl_dcu, &ui_style_label_value_md, 0);
    lv_label_set_text(lbl_dcu, "DCU");
    lv_obj_align(lbl_dcu, LV_ALIGN_CENTER, -5, 90);

    lv_obj_t *lbl_version = lv_label_create(scr);
    lv_obj_add_style(lbl_version, &ui_style_label_title, 0);
    lv_label_set_text(lbl_version, "v2612");
    lv_obj_align(lbl_version, LV_ALIGN_CENTER, -5, 130);

    /* ── Logo ────────────────────────────────────────────────────────────── */

    LV_IMAGE_DECLARE(outer_gear_a8);

    lv_obj_t *img_outer_gear = lv_image_create(scr);
    lv_image_set_src(img_outer_gear, &outer_gear_a8);
    lv_obj_set_style_image_recolor(img_outer_gear, lv_color_hex(0xfa6e00), LV_PART_MAIN);
    lv_obj_set_style_image_recolor_opa(img_outer_gear, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_align(img_outer_gear, LV_ALIGN_CENTER, 0, -30);

    lv_image_set_pivot(img_outer_gear,
                       outer_gear_a8.header.w / 2,
                       outer_gear_a8.header.h / 2);

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
