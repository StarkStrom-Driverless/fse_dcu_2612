/**
 * @file        screen_sdc.c
 * @brief       Shutdown-circuit screen implementation
 *
 * @ingroup     dcu_ui_screens
 *
 * @details     Renders the twelve shutdown-circuit nodes as LEDs on a top-down
 *              car image and as a name table; the contract is in screen_sdc.h.
 *
 *              ### One table drives everything
 *              k_sdc_nodes[] pairs each node's label, its LVGL subject and its
 *              position on the image.  Both views and all observer wiring are
 *              derived from it by index, so adding or moving a node is a
 *              single-line change.
 *
 *              ### Two update mechanisms
 *              The overlay LEDs each have their own observer and recolor
 *              themselves.  The table cannot: an lv_table has no per-cell
 *              color property.  Its text color is applied in a draw-task
 *              callback that reads the subjects at paint time, and a second
 *              set of observers only invalidates the table so that callback
 *              runs again.  That is why the table observers ignore their
 *              subject argument entirely.
 *
 *              The draw callback derives the node index from the cell
 *              coordinates as row × 2 + column, which is the inverse of how
 *              build_checklist() fills the table — the two must stay in step.
 *
 * @author      Mario Wegmann <mario.wegmann@web.de>
 * @date        Created: 2026-08-05
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
#include "modules/ui/widgets/ui_hintbar.h"
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

/**
 * @brief Number of shutdown-circuit nodes.
 *
 * Must match the length of k_sdc_nodes[] and stay even — the table lays the
 * nodes out in two columns.
 */
#define SDC_NODE_COUNT          12

/**
 * @brief Per-node descriptor: table label, LVGL subject, and the
 *        pixel position of the overlay LED relative to the image top-left.
 *
 * To adapt to a different vehicle adjust img_x / img_y only — the
 * table and the observer wiring are derived automatically from this table.
 */
typedef struct {
    const char   *label;   /**< Node name shown in the table.                     */
    lv_subject_t *subject; /**< Generated RX subject; 1 = closed, 0 = open.        */
    int16_t       img_x;   /**< Overlay LED X on topdown image (px from image left)*/
    int16_t       img_y;   /**< Overlay LED Y on topdown image (px from image top) */
} sdc_node_t;

/**
 * @brief The twelve shutdown-circuit nodes.
 *
 * The order defines the table layout — index i lands in row i/2, column i%2 —
 * so reordering entries rearranges the table.
 *
 * Overlay LED positions are placeholders: adjust img_x / img_y to match the
 * actual component positions on the vehicle topdown image.  Coordinate origin
 * is the top-left corner of that image, because the LEDs are its children.
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

/*
 * The three group pointers are never assigned — the screen has no interactive
 * widgets, so the accessors hand ui.c a NULL and the input devices are
 * detached while this screen is shown.  The group creation is retained,
 * commented out, in the factory below.
 */

/** @brief Input group for the right encoder. Always NULL. */
static lv_group_t *s_right_encoder_group;

/** @brief Input group for the left button pad. Always NULL. */
static lv_group_t *s_left_button_group;

/** @brief Input group for the right button pad. Always NULL. */
static lv_group_t *s_right_button_group;


/**
 * @brief What each control does on this screen; see @ref ui_hint_input.
 *
 * Static storage: ui_hintbar_create() keeps the pointers rather than copying
 * the strings. Controls left out here are dimmed in the bar.
 */
static const char *const k_hints[UI_HINT_INPUT_COUNT] = {
    [UI_HINT_ENC_LEFT] = "Screen",
};

/* ── Private Function Prototypes ─────────────────────────────────────────────────────────────── */
static void build_checklist(lv_obj_t *scr);
static void sdc_led_observer_cb(lv_observer_t *observer, lv_subject_t *subject);
static void sdc_invalidate_cb(lv_observer_t *observer, lv_subject_t *subject);
static void sdc_table_draw_cb(lv_event_t *e);


/* ── Private Function Implementations ───────────────────────────────────────────────────────── */

/**
 * @brief Observer for one overlay LED on the car topdown image.
 *
 * Subject value 1 → node closed → green; 0 → node open → red. A shutdown
 * circuit reports the state of its own contact, so a set bit is the healthy
 * one: current is getting through.
 *
 * @param observer  Observer whose target object is the LED.
 * @param subject   The node's ui_subj_sdc_* subject.
 */
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
 * directly at paint time, so invalidating the table is sufficient.  One
 * instance is registered per node, all pointing at the same table.
 *
 * @param observer  Observer whose target object is the table.
 * @param subject   Unused; only the fact that something changed matters.
 */
static void sdc_invalidate_cb(lv_observer_t *observer, lv_subject_t *subject)
{
    ARG_UNUSED(subject);
    lv_obj_invalidate(lv_observer_get_target_obj(observer));
}

/**
 * @brief Draw callback that colors table cell text based on SDC subject state.
 *
 * value = 1 → UI_C_DARK (node closed, healthy)
 * value = 0 → UI_C_RED  (node open, circuit interrupted)
 *
 * Runs per draw task, so it filters down to label tasks on LV_PART_ITEMS and
 * ignores everything else the table draws. The cell coordinates arrive as
 * base->id1 (row) and base->id2 (column); cell index = row × 2 + col, matching
 * the k_sdc_nodes[] order.
 *
 * Requires LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS on the table — see
 * build_checklist().
 *
 * @param e  LV_EVENT_DRAW_TASK_ADDED from the table.
 */
static void sdc_table_draw_cb(lv_event_t *e)
{
    lv_draw_task_t     *t    = lv_event_get_draw_task(e);
    lv_draw_dsc_base_t *base = (lv_draw_dsc_base_t *)lv_draw_task_get_draw_dsc(t);

    if (base->part != LV_PART_ITEMS) return;
    if (lv_draw_task_get_type(t) != LV_DRAW_TASK_TYPE_LABEL) return;

    uint8_t idx = (uint8_t)(base->id1 * 2u + base->id2);
    if (idx >= SDC_NODE_COUNT) return;

    bool open = lv_subject_get_int(k_sdc_nodes[idx].subject) == 0;
    ((lv_draw_label_dsc_t *)lv_draw_task_get_draw_dsc(t))->color = open ? UI_C_RED : UI_C_DARK;
}

/**
 * @brief Build the 6×2 SDC status table on the right side of the screen.
 *
 * One lv_table replaces the previous 12-label list.  Text color is set
 * in sdc_table_draw_cb(); sdc_invalidate_cb() ensures the table repaints
 * whenever any subject changes value.
 *
 * @param scr  Screen object to build into.
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

    /* ── Input groups ────────────────────────────────────────────────────── */

    /*
     * Deliberately not created: with nothing to focus, leaving the groups NULL
     * detaches the input devices, so a stray encoder turn or button press on
     * this screen does nothing at all.  Retained for when the screen gains an
     * interactive widget.
     */
    // s_right_encoder_group = lv_group_create();
    // lv_group_set_editing(s_right_encoder_group, true);

    // s_right_button_group = lv_group_create();
    // lv_group_set_editing(s_right_button_group, true);

    ui_hintbar_create(scr, k_hints);

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


