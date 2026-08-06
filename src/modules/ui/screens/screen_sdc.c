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

/** @brief Width of each action button in pixels. */
#define BTN_WIDTH               100

/** @brief Height of each action button in pixels. */
#define BTN_HEIGHT              50

/**
 * @brief Half the centre-to-centre distance between the two buttons.
 *
 * Layout: |←BTN_WIDTH→| 10px gap |←BTN_WIDTH→|
 *          centre-to-centre = BTN_WIDTH + 10 = 110 px → half = 55 px
 */
#define BTN_HALF_SPACING        55

/** @brief Bottom margin for the button row (pixels from screen bottom). */
#define BTN_BOTTOM_MARGIN       20

/* ── SDC node table ──────────────────────────────────────────────────────── */

/** @brief Diameter of the LED indicator dot in the checklist (px). */
#define SDC_LED_SIZE_LIST       10

/** @brief Diameter of the overlay LED dot on the topdown image (px). */
#define SDC_LED_SIZE_OVERLAY    8

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
    { "MOTOR_RL", &ui_subj_sdc_motor_rl,            30,   105 },
    { "MOTOR_FL", &ui_subj_sdc_motor_fl,            30,    30 },
    { "MOTOR_RR", &ui_subj_sdc_motor_rr,           252,   105 },
    { "MOTOR_FR", &ui_subj_sdc_motor_fr,           252,    30 },
    { "COCKPIT",  &ui_subj_sdc_cockpit,            141,    25 },
    { "BSPD",     &ui_subj_sdc_bspd,              141,    55 },
    { "ASCU",     &ui_subj_sdc_ascu,              141,    70 },
    { "HVD",      &ui_subj_sdc_hvd,               141,    85 },
    { "MH",       &ui_subj_sdc_sdb_mh,            141,   100 },
    { "RES",      &ui_subj_sdc_res,               100,   130 },
    { "BOTS",     &ui_subj_sdc_bots,              182,   130 },
    { "INERTIA",  &ui_subj_sdc_inertia,           141,    40 },
};


/* ── Private Variables ───────────────────────────────────────────────────────────────────────── */

/** @brief RTD button — requests Ready-to-Drive with the last-known drive mode. */
static lv_obj_t   *s_btn_rtd;

/** @brief LVGL input group for the right encoder. */
static lv_group_t *s_right_encoder_group;

/** @brief LVGL input group for the right encoder. */
static lv_group_t *s_left_button_group;

/** @brief LVGL input group for the right encoder. */
static lv_group_t *s_right_button_group;


/* ── Private Function Prototypes ─────────────────────────────────────────────────────────────── */
static void build_buttons(lv_obj_t *scr);
static void build_checklist(lv_obj_t *scr);
static void sdc_led_observer_cb(lv_observer_t *observer, lv_subject_t *subject);
static void btn_rtd_event_cb(lv_event_t *e);


/* ── Private Function Implementations ───────────────────────────────────────────────────────── */

/**
 * @brief Common observer callback for both checklist LEDs and overlay LEDs.
 *
 * Signal = 1 → SDC closed (OK)  → green
 * Signal = 0 → SDC open (fault) → red
 *
 * LVGL fires this callback immediately on subscription so the initial
 * state is set without a separate initialisation call.
 */
static void sdc_led_observer_cb(lv_observer_t *observer, lv_subject_t *subject)
{
    lv_obj_t *led = lv_observer_get_target_obj(observer);
    bool closed = lv_subject_get_int(subject) != 0;

    lv_led_set_color(led, closed ? lv_color_hex(0x00cc44) : lv_color_hex(0xff2020));
    lv_led_on(led);
}

/**
 * @brief Build the 12-entry SDC checklist on the right side of the screen.
 *
 * Each row contains a small LED indicator and a label.  The LEDs are wired
 * to the same subjects as the overlay LEDs via sdc_led_observer_cb so both
 * update together.
 */
static void build_checklist(lv_obj_t *scr)
{
    lv_obj_t *panel = lv_obj_create(scr);
    lv_obj_remove_style_all(panel);
    lv_obj_set_size(panel, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(panel, LV_ALIGN_RIGHT_MID, -12, 20);
    lv_obj_set_layout(panel, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(panel, 3, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    for (uint8_t i = 0; i < 4; i++) {
        lv_obj_t *row = lv_obj_create(panel);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_layout(row, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row,
                              LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, 5, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *led = lv_led_create(row);
        lv_obj_set_size(led, SDC_LED_SIZE_LIST, SDC_LED_SIZE_LIST);
        lv_led_set_color(led, lv_color_hex(0xff2020));
        lv_led_on(led);

        lv_obj_t *lbl = lv_label_create(row);
        lv_obj_add_style(lbl, &ui_style_label_subtitle, 0);
        lv_label_set_text(lbl, k_sdc_nodes[i].label);

        lv_subject_add_observer_obj(k_sdc_nodes[i].subject,
                                    sdc_led_observer_cb, led, NULL);
    }
}

/**
 * @brief RTD button press/release handler.
 *
 * PRESSED  → publishes UI_INPUT_RTD_REQUEST  (App sets mode RTD  → CAN rtd_button=1)
 * RELEASED → publishes UI_INPUT_RTD_RELEASE  (App sets mode DEBUG → CAN rtd_button=0)
 */
static void btn_rtd_event_cb(lv_event_t *e)
{
    lv_obj_t * button = lv_event_get_target_obj(e);
    lv_event_code_t code = lv_event_get_code(e);

    lv_obj_set_state(button, LV_STATE_USER_1, (code == LV_EVENT_LONG_PRESSED) ? true : false);

    struct ui_input_event evt = {
        .type = (code == LV_EVENT_LONG_PRESSED) ? UI_INPUT_RTD_REQUEST : UI_INPUT_RTD_RELEASE,
    };

    int ret = zbus_chan_pub(&ui_input_chan, &evt, K_NO_WAIT);
    if (ret != 0) {
        LOG_WRN("RTD event publish failed: %d", ret);
    } else {
        LOG_DBG("RTD %s", (code == LV_EVENT_LONG_PRESSED) ? "pressed" : "released");
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

    // LV_IMAGE_DECLARE(car_topdown_b_i4);

    // lv_obj_t *img_car_topdown = lv_image_create(scr);
    // lv_image_set_src(img_car_topdown, &car_topdown_b_i4);
    // lv_obj_align(img_car_topdown, LV_ALIGN_CENTER, -80, 0);

    /* ── Overlay LEDs on topdown image ───────────────────────────────────── */
    /*
     * Each LED is a child of img_car_topdown so img_x / img_y are relative
     * to the image's top-left corner.  Adjust the coordinates in k_sdc_nodes
     * to match the component positions on a different vehicle.
     */
    // for (uint8_t i = 0; i < SDC_NODE_COUNT; i++) {
    //     lv_obj_t *led = lv_led_create(img_car_topdown);
    //     lv_obj_set_size(led, SDC_LED_SIZE_OVERLAY, SDC_LED_SIZE_OVERLAY);
    //     lv_obj_set_pos(led, k_sdc_nodes[i].img_x, k_sdc_nodes[i].img_y);
    //     lv_led_set_color(led, lv_color_hex(0xff2020));
    //     lv_led_on(led);
    //     lv_subject_add_observer_obj(k_sdc_nodes[i].subject,
    //                                 sdc_led_observer_cb, led, NULL);
    // }

    // /* ── Checklist (right side) ──────────────────────────────────────────── */

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
    // lv_group_add_obj(s_right_button_group, s_btn_rtd);
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


