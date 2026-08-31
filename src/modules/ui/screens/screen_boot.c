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
 *              There are no interactive widgets.  The one dynamic element is an
 *              LVGL animation that rotates the outer gear continuously; it is
 *              owned by the object and dies with it, so nothing has to stop it
 *              when the screen is deleted.
 *
 *              Navigation away from this screen is handled by ui.c.
 *
 *              ### Optical latency patch (CONFIG_DCU_BENCHMARK_BOOT_PATCH)
 *
 *              With that option on, a solid rectangle is added in the
 *              bottom-left corner whose colour follows the AS_state CAN signal:
 *              black, yellow, white. Held against a photodiode it turns "a CAN
 *              frame arrived" into an edge on a scope trace, which extends the
 *              existing power-on-to-first-frame measurement into the running
 *              system.
 *
 *              The signal is borrowed, not repurposed. AS_state was picked
 *              because it already arrives over the bus and needs no change to
 *              dbc/dcu_app.yaml; the patch ignores what the value means, and
 *              nothing else in the firmware is affected. Off by default — see
 *              the Kconfig help.
 *
 *              ### What the measurement actually contains
 *
 *              Between the frame on the wire and the pixel on the glass sit
 *              four hand-offs, two of them polled:
 *
 *              | Stage | Cost |
 *              |-------|------|
 *              | RX interrupt → message queue | immediate |
 *              | CAN worker drains the queue and publishes | up to 10 ms (CAN_TX_PERIOD_MS) |
 *              | App thread reads and forwards to ui_cmd_chan | scheduling only, priority 5 |
 *              | UI thread polls the channel and sets the subject | up to 5 ms (UI_TASK_PERIOD_MS) |
 *              | lv_timer_handler() renders, driver writes the panel | one further UI iteration plus SPI transfer |
 *
 *              So the floor is the panel transfer and the ceiling carries about
 *              15 ms of pure polling quantisation. Expect a spread across
 *              repeated measurements rather than a single figure — that spread
 *              is the two tick intervals beating against each other, not jitter
 *              in the code. Note also that the subject is set *after*
 *              lv_timer_handler() in the UI loop, so a change always waits for
 *              the following iteration to be drawn.
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
#include "modules/ui/widgets/ui_hintbar.h"

#ifdef CONFIG_DCU_BENCHMARK_BOOT_PATCH
#include "generated/ui_subjects_gen.h"
#endif


/* ── Private Macros & Constants ──────────────────────────────────────────────────────────────── */

#ifdef CONFIG_DCU_BENCHMARK_BOOT_PATCH

/** @brief Edge length of the measurement patch, in pixels. */
#define BENCH_PATCH_SIZE        100

/** @brief Distance from the screen corner, in pixels. */
#define BENCH_PATCH_MARGIN      8

/**
 * @name Raw AS_state values keyed to the three optical levels
 *
 * Raw signal values, not decoded states — the signal is borrowed, its meaning
 * is irrelevant here. Remap these to whatever the frame generator emits.
 * @{
 */
#define BENCH_LEVEL_DARK        0   /**< → black  */
#define BENCH_LEVEL_MID         1   /**< → yellow */
#define BENCH_LEVEL_BRIGHT      2   /**< → white  */
/** @} */



/* ── Private Function Implementations ───────────────────────────────────────────────────────── */

/**
 * @brief Repaint the measurement patch for a new AS_state value.
 *
 * LVGL fires this once on subscription, so the patch has a defined colour from
 * the moment the screen is built — black, since the subject starts at 0. That
 * is the optical baseline the first transition is measured against.
 *
 * Any value outside the three below is painted black rather than left
 * unchanged, so a stray value cannot be mistaken for a held level.
 *
 * @param observer  Observer whose target object is the patch.
 * @param subject   ui_subj_as_state.
 */
static void bench_patch_observer_cb(lv_observer_t *observer, lv_subject_t *subject)
{
    lv_obj_t  *patch = lv_observer_get_target_obj(observer);
    lv_color_t colour;

    switch (lv_subject_get_int(subject)) {
    case BENCH_LEVEL_MID:
        colour = lv_color_hex(0x7F7F7F);
        break;
    case BENCH_LEVEL_BRIGHT:
        colour = lv_color_hex(0xFFFFFF);
        break;
    case BENCH_LEVEL_DARK:
    default:
        colour = lv_color_hex(0x000000);
        break;
    }

    lv_obj_set_style_bg_color(patch, colour, LV_PART_MAIN);
}

/**
 * @brief Build the measurement patch and bind it to the borrowed CAN signal.
 *
 * Every default style is stripped: no border, no radius, no padding, so the
 * patch is a hard-edged block of one colour and the photodiode sees a clean
 * step rather than an anti-aliased ramp.
 *
 * Placed in the bottom-left corner, clear of the gear, the product name and
 * the version — move it with the two geometry macros above if the diode sits
 * elsewhere.
 *
 * @param scr  Screen object to build into.
 */
static void bench_patch_create(lv_obj_t *scr)
{
    lv_obj_t *patch = lv_obj_create(scr);

    lv_obj_remove_style_all(patch);
    lv_obj_set_size(patch, BENCH_PATCH_SIZE, BENCH_PATCH_SIZE);
    lv_obj_align(patch, LV_ALIGN_BOTTOM_LEFT, BENCH_PATCH_MARGIN,
                 -(BENCH_PATCH_MARGIN + UI_HINTBAR_H));
    lv_obj_set_style_bg_opa(patch, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(patch, LV_OBJ_FLAG_SCROLLABLE);

    lv_subject_add_observer_obj(&ui_subj_as_state, bench_patch_observer_cb,
                                patch, NULL);
}

#endif /* CONFIG_DCU_BENCHMARK_BOOT_PATCH */


/**
 * @brief What each control does on this screen; see @ref ui_hint_input.
 *
 * Static storage: ui_hintbar_create() keeps the pointers rather than copying
 * the strings. Controls left out here are dimmed in the bar.
 */
static const char *const k_hints[UI_HINT_INPUT_COUNT] = {
    [UI_HINT_ENC_LEFT] = "Screen",
};

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
    lv_obj_align(lbl_dcu, LV_ALIGN_CENTER, -5, 80);

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
    lv_obj_align(lbl_version, LV_ALIGN_CENTER, -5, 120);

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

#ifdef CONFIG_DCU_BENCHMARK_BOOT_PATCH
    /* Built last so it sits on top of everything else. */
    bench_patch_create(scr);
#endif

    ui_hintbar_create(scr, k_hints);

    return scr;
}
