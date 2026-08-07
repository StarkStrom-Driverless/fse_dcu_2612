/**
 * @file        screen_sdc.c
 * @brief       
 *
 * @details     
 *
 * @author      Mario Wegmann <mario.wegmann@web.de>
 * @date        Created: 2026-08-05
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
 * 0.1.0    2026-08-05  Mario Wegmann   Initial creation
 * ─────────────────────────────────────────────────────────────────────────────────────────────────
 */

/* ── Corresponding Header ────────────────────────────────────────────────────────────────────── */

#include "modules/ui/screens/screen_sdc.h"

/* ── Zephyr Includes ─────────────────────────────────────────────────────────────────────────── */

#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/logging/log.h>

/* ── Project Includes ────────────────────────────────────────────────────────────────────────── */

#include "modules/ui/ui_styles.h"
#include "modules/ui/widgets/ui_header.h"
#include "services/event_bus/event_bus.h"
#include "services/event_bus/events.h"
#include "generated/ui_subjects_gen.h"

/* ── Zephyr Logging ──────────────────────────────────────────────────────────────────────────── */

LOG_MODULE_REGISTER(screen_sdc, CONFIG_LOG_DEFAULT_LEVEL);


/* ── Private Macros & Constants ──────────────────────────────────────────────────────────────── */

/** @brief Diameter of the overlay LED dot on the topdown image (px). */
#define SDC_LED_SIZE_OVERLAY    8

/** @brief Width of each table column in pixels. */
#define SDC_COL_WIDTH           75

#define SDC_NODE_COUNT          12

/**
 * @brief Per-node descriptor: checklist label, Zbus subject, and the
 *        pixel position of the overlay LED relative to the image top-left.
 *
 * To adapt to a different vehicle adjust img_x / img_y only — the
 * checklist and observer wiring are derived automatically from this table.
 */
typedef struct {
    const char   *label;
    lv_subject_t *subject;
    int16_t       img_x;   /* overlay LED X on topdown image (px from image left) */
    int16_t       img_y;   /* overlay LED Y on topdown image (px from image top)  */
} sdc_node_t;

/*
 * Overlay LED positions are placeholders — adjust img_x / img_y to match
 * the actual component positions on the vehicle topdown image.
 *
 * Coordinate origin: top-left corner of the car_topdown image.
 */
static const sdc_node_t k_sdc_nodes[SDC_NODE_COUNT] = {
    /*  label        subject                       img_x  img_y */
    { "ASCU",     &ui_subj_sdc_ascu,               141,    70 },
    { "HVD",      &ui_subj_sdc_hvd,                 70,    72 },
    { "MOTOR RL", &ui_subj_sdc_motor_rl,            45,    10 },
    { "RES",      &ui_subj_sdc_res,                100,   130 },
    { "MOTOR FL", &ui_subj_sdc_motor_fl,           205,    10 },
    { "BOTS",     &ui_subj_sdc_bots,               182,   130 },
    { "MOTOR FR", &ui_subj_sdc_motor_fr,           205,   134 },
    { "COCKPIT",  &ui_subj_sdc_cockpit,            175,    72 },
    { "INERTIA",  &ui_subj_sdc_inertia,            141,    40 },
    { "MH",       &ui_subj_sdc_sdb_mh,             141,   100 },
    { "MOTOR RR", &ui_subj_sdc_motor_rr,            45,   134 },
    { "BSPD",     &ui_subj_sdc_bspd,               252,    72 },
};

/* ── Private Variables ───────────────────────────────────────────────────────────────────────── */

/** @brief SDC status table (2 columns × 6 rows). */
static lv_obj_t   *s_table;

/** @brief LVGL input group for the right encoder. */
static lv_group_t *s_right_encoder_group;

/** @brief LVGL input group for the left button. */
static lv_group_t *s_left_button_group;

/** @brief LVGL input group for the right button. */
static lv_group_t *s_right_button_group;


/* ── Private Function Prototypes ─────────────────────────────────────────────────────────────── */
static void build_checklist(lv_obj_t *scr);
static void sdc_led_observer_cb(lv_observer_t *observer, lv_subject_t *subject);
static void sdc_invalidate_cb(lv_observer_t *observer, lv_subject_t *subject);
static void sdc_table_draw_cb(lv_event_t *e);


/* ── Private Function Implementations ───────────────────────────────────────────────────────── */

/** @brief Observer for overlay LEDs on the car topdown image. */
static void sdc_led_observer_cb(lv_observer_t *observer, lv_subject_t *subject)
{
    lv_obj_t *led = lv_observer_get_target_obj(observer);
    bool closed = lv_subject_get_int(subject) != 0;
    lv_led_set_color(led, closed ? lv_color_hex(0x00cc44) : lv_color_hex(0xff2020));
}

/**
 * @brief Observer that triggers a table redraw when any SDC subject changes.
 *
 * Does not update individual cells — the draw callback reads subject values
 * directly at paint time, so invalidating the table is sufficient.
 */
static void sdc_invalidate_cb(lv_observer_t *observer, lv_subject_t *subject)
{
    ARG_UNUSED(subject);
    lv_obj_invalidate(lv_observer_get_target_obj(observer));
}

/**
 * @brief Draw callback that colours table cell text based on SDC subject state.
 *
 * value = 0 → UI_C_DARK (node OK / SDC closed)
 * value = 1 → UI_C_RED  (node fault / SDC open)
 *
 * Cell index = row × 2 + col, matching the k_sdc_nodes[] order.
 */
static void sdc_table_draw_cb(lv_event_t *e)
{
    lv_draw_task_t     *t    = lv_event_get_draw_task(e);
    lv_draw_dsc_base_t *base = (lv_draw_dsc_base_t *)lv_draw_task_get_draw_dsc(t);

    if (base->part != LV_PART_ITEMS) return;
    if (lv_draw_task_get_type(t) != LV_DRAW_TASK_TYPE_LABEL) return;

    uint8_t idx = (uint8_t)(base->id1 * 2u + base->id2);
    if (idx >= SDC_NODE_COUNT) return;

    bool fault = lv_subject_get_int(k_sdc_nodes[idx].subject) != 0;
    ((lv_draw_label_dsc_t *)lv_draw_task_get_draw_dsc(t))->color = fault ? UI_C_RED : UI_C_DARK;
}

/**
 * @brief Build the 2×6 SDC status table on the right side of the screen.
 *
 * One lv_table replaces the previous 12-label list.  Text colour is set
 * in sdc_table_draw_cb; sdc_invalidate_cb ensures the table repaints
 * whenever any subject changes value.
 */
static void build_checklist(lv_obj_t *scr)
{
    s_table = lv_table_create(scr);
    lv_obj_remove_style_all(s_table);

    lv_table_set_column_count(s_table, 2);

    // lv_table_set_cell_value(s_table, 0, 0, "SDC Components");
    // lv_table_set_cell_ctrl(s_table, 0, 0, LV_TABLE_CELL_CTRL_MERGE_RIGHT);

    for (uint8_t i = 0; i < SDC_NODE_COUNT; i++) {
        lv_table_set_cell_value(s_table, i / 2u, i % 2u, k_sdc_nodes[i].label);
    }

    lv_table_set_column_width(s_table, 0, SDC_COL_WIDTH);
    lv_table_set_column_width(s_table, 1, SDC_COL_WIDTH);

    lv_obj_set_style_text_font(s_table,    &BarlowCondensed_BoldItalic_18, LV_PART_ITEMS);
    lv_obj_set_style_text_color(s_table,   UI_C_DARK,                      LV_PART_ITEMS);
    lv_obj_set_style_pad_ver(s_table,      4,                              LV_PART_ITEMS);
    lv_obj_set_style_pad_hor(s_table,      4,                              LV_PART_ITEMS);
    lv_obj_set_style_border_width(s_table, 0,                              LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(s_table,       LV_OPA_TRANSP,                  LV_PART_ITEMS);
    lv_obj_set_style_border_width(s_table, 1,                              LV_PART_MAIN);
    lv_obj_set_style_border_color(s_table, UI_C_DARK,                      LV_PART_MAIN);


    lv_obj_align(s_table, LV_ALIGN_RIGHT_MID, -12, 20);
    lv_obj_clear_flag(s_table, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_add_flag(s_table, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);
    lv_obj_add_event_cb(s_table, sdc_table_draw_cb, LV_EVENT_DRAW_TASK_ADDED, NULL);

    for (uint8_t i = 0; i < SDC_NODE_COUNT; i++) {
        lv_subject_add_observer_obj(k_sdc_nodes[i].subject,
                                    sdc_invalidate_cb, s_table, NULL);
    }
}


/* ── Public Function Implementations ─────────────────────────────────────────────────────────── */

lv_obj_t *screen_sdc_create(lv_subject_t *status_subjects)
{
    /* ── Screen base ─────────────────────────────────────────────────────── */

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_add_style(scr, &ui_style_screen, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* ── Widgets ─────────────────────────────────────────────────────────── */

    ui_header_create(scr, "SDC", status_subjects);

    /* ── Topdown Image ───────────────────────────────────────────────────── */

    LV_IMAGE_DECLARE(car_topdown_b_i4);

    lv_obj_t *img_car_topdown = lv_image_create(scr);
    lv_image_set_src(img_car_topdown, &car_topdown_b_i4);
    lv_obj_align(img_car_topdown, LV_ALIGN_CENTER, -85, 24);

    /* ── Overlay LEDs on topdown image ───────────────────────────────────── */
    /*
     * Each LED is a child of img_car_topdown so img_x / img_y are relative
     * to the image's top-left corner.  Adjust the coordinates in k_sdc_nodes
     * to match the component positions on a different vehicle.
     */
    for (uint8_t i = 0; i < SDC_NODE_COUNT; i++) {
        lv_obj_t *led = lv_led_create(img_car_topdown);
        lv_obj_set_size(led, SDC_LED_SIZE_OVERLAY, SDC_LED_SIZE_OVERLAY);
        lv_obj_set_pos(led, k_sdc_nodes[i].img_x, k_sdc_nodes[i].img_y);
        lv_led_set_color(led, lv_color_hex(0xff2020));
        lv_led_on(led);
        lv_subject_add_observer_obj(k_sdc_nodes[i].subject,
                                    sdc_led_observer_cb, led, NULL);
    }

    /* ── Status table (right side, 2×6) ─────────────────────────────────── */

    build_checklist(scr);

    /* ── Input group (right encoder) ─────────────────────────────────────── */

    /*
     * Tab order: roller → OK → RTD.
     *
     * ui.c assigns this group to the right encoder indev on screen entry:
     *   lv_indev_set_group(right_encoder_indev, screen_mission_select_get_group())
     * and removes it on screen leave:
     *   lv_indev_set_group(right_encoder_indev, NULL)
     */
    // s_right_encoder_group = lv_group_create();
    // lv_group_set_editing(s_right_encoder_group, true);

    // s_right_button_group = lv_group_create();
    // lv_group_set_editing(s_right_button_group, true);

    return scr;
}

lv_group_t *screen_sdc_get_right_encoder_group(void)
{
    return s_right_encoder_group;
}

lv_group_t *screen_sdc_get_left_button_group(void)
{
    return s_left_button_group;
}

lv_group_t *screen_sdc_get_right_button_group(void)
{
    return s_right_button_group;
}


