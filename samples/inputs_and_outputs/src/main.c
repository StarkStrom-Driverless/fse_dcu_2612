/**
 * @file        main.c
 * @brief       <Short one-line description of this file's purpose>
 *
 * @details     <Optional extended description. Explain the module's role,
 *              any important design decisions, or usage notes.>
 *
 * @author      Mario Wegmann <mario.wegmann@web.de>
 * @date        Created: 2026-05-20
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
 * 0.1.0    2026-05-20  Mario Wegmann   Initial creation
 * ─────────────────────────────────────────────────────────────────────────────────────────────────
 */

/* ── Corresponding Header ────────────────────────────────────────────────────────────────────── */
 
/* ── Zephyr Includes ─────────────────────────────────────────────────────────────────────────── */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/drivers/display.h>
#include <zephyr/logging/log.h>
 
/* ── LVGL Include ────────────────────────────────────────────────────────────────────────────── */
#include <lvgl.h>
#include <lvgl_input_device.h>
 
/* ── Standard Includes ───────────────────────────────────────────────────────────────────────── */
#include <stdio.h>

/* ── Zephyr Logging ──────────────────────────────────────────────────────────────────────────── */
LOG_MODULE_REGISTER(app, CONFIG_LOG_DEFAULT_LEVEL);
 
/* ── Private Macros & Constants ──────────────────────────────────────────────────────────────── */
 
#define MAX_PERIOD PWM_SEC(1U)  / 1400U
 
/* ── Private Type Definitions ────────────────────────────────────────────────────────────────── */
 
typedef enum {
    SLIDER_LEFT,
    SLIDER_RIGHT
} slider_id_t;

typedef enum {
    BUTTON_LEFT,
    BUTTON_RIGHT
} button_id_t;
 
/* ── Private Variables ───────────────────────────────────────────────────────────────────────── */
 
#ifdef CONFIG_LV_Z_ENCODER_INPUT
static const struct device *lvgl_encoder_left = DEVICE_DT_GET(DT_ALIAS(qdec_input_left));
static const struct device *lvgl_encoder_right = DEVICE_DT_GET(DT_ALIAS(qdec_input_right));
#endif /* CONFIG_LV_Z_ENCODER_INPUT */

#ifdef CONFIG_LV_Z_KEYPAD_INPUT
static const struct device *lvgl_keypad_left = DEVICE_DT_GET(DT_ALIAS(keypad_left));
static const struct device *lvgl_keypad_right = DEVICE_DT_GET(DT_ALIAS(keypad_right));
#endif /* CONFIG_LV_Z_KEYPAD_INPUT */

#ifdef CONFIG_PWM
static const struct pwm_dt_spec pwm_pizeo = PWM_DT_SPEC_GET(DT_ALIAS(pwm_pizeo));
#endif /* CONFIG_PWM */

static lv_obj_t * sliderLeftSpan;
static lv_obj_t * sliderRightSpan;

static lv_obj_t * buttonLeftValueLabel;
static lv_obj_t * buttonRightValueLabel;
 
/* ── Private Function Prototypes ─────────────────────────────────────────────────────────────── */
 
static void slider_event_cb(lv_event_t * e);
static void button_event_cb(lv_event_t * e);
 
/* ── Private Function Implementations ───────────────────────────────────────────────────────── */
 
static void build_header() {
    lv_obj_t * header;
    header = lv_obj_create(lv_screen_active());
    lv_obj_set_size(header, 100, 50);
    lv_obj_align(header, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_height(header, lv_pct(15));
    lv_obj_set_width(header, lv_pct(100));
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "RTD");
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 10, 0);
}

static void build_sliders() {
    lv_obj_t * sliderLeft = lv_slider_create(lv_screen_active());
    lv_obj_set_size(sliderLeft, 40, 200);
    lv_obj_set_pos(sliderLeft, 10, 70);
    lv_obj_add_event_cb(sliderLeft, slider_event_cb, LV_EVENT_VALUE_CHANGED, (void *)SLIDER_LEFT);

    lv_obj_t * sliderLeft_Titlelabel = lv_label_create(lv_screen_active());
    lv_label_set_text(sliderLeft_Titlelabel, "TQG F");
    lv_obj_align_to(sliderLeft_Titlelabel, sliderLeft, LV_ALIGN_OUT_TOP_MID, 0, 0);

    sliderLeftSpan = lv_spangroup_create(lv_screen_active());

    lv_obj_set_width(sliderLeftSpan, 60);
    lv_obj_set_height(sliderLeftSpan, 60);
    lv_obj_set_style_text_align(sliderLeftSpan, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    lv_obj_align_to(sliderLeftSpan, sliderLeft, LV_ALIGN_OUT_BOTTOM_MID, 0, 5);

    lv_span_t * spanL1 = lv_spangroup_new_span(sliderLeftSpan);
    lv_span_set_text(spanL1, "0");

    lv_span_t * spanL2 = lv_spangroup_new_span(sliderLeftSpan);
    lv_span_set_text(spanL2, "%");

    #ifdef CONFIG_LV_Z_ENCODER_INPUT
    lv_group_t *sliderLeft_group;
    sliderLeft_group = lv_group_create();
	lv_group_add_obj(sliderLeft_group, sliderLeft);
	lv_indev_set_group(lvgl_input_get_indev(lvgl_encoder_left), sliderLeft_group);
    lv_group_set_editing(sliderLeft_group, true);
    #endif

    lv_obj_t * sliderRight = lv_slider_create(lv_screen_active());
    lv_obj_set_size(sliderRight, 40, 200);
    lv_obj_set_pos(sliderRight, 430, 70);
    lv_obj_add_event_cb(sliderRight, slider_event_cb, LV_EVENT_VALUE_CHANGED, (void *)SLIDER_RIGHT);

    lv_obj_t * sliderRight_Titlelabel = lv_label_create(lv_screen_active());
    lv_label_set_text(sliderRight_Titlelabel, "TQG R");
    lv_obj_align_to(sliderRight_Titlelabel, sliderRight, LV_ALIGN_OUT_TOP_MID, 0, 0);

    sliderRightSpan = lv_spangroup_create(lv_screen_active());

    lv_obj_set_width(sliderRightSpan, 60);
    lv_obj_set_height(sliderRightSpan, 60);
    lv_obj_set_style_text_align(sliderRightSpan, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    lv_obj_align_to(sliderRightSpan, sliderRight, LV_ALIGN_OUT_BOTTOM_MID, 0, 5);

    lv_span_t * spanR1 = lv_spangroup_new_span(sliderRightSpan);
    lv_span_set_text(spanR1, "0");

    lv_span_t * spanR2 = lv_spangroup_new_span(sliderRightSpan);
    lv_span_set_text(spanR2, "%");

    #ifdef CONFIG_LV_Z_ENCODER_INPUT
    lv_group_t *sliderRight_group;
    sliderRight_group = lv_group_create();
	lv_group_add_obj(sliderRight_group, sliderRight);
	lv_indev_set_group(lvgl_input_get_indev(lvgl_encoder_right), sliderRight_group);
    lv_group_set_editing(sliderRight_group, true);
    #endif
}

static void slider_event_cb(lv_event_t * e) {
    lv_obj_t * slider = lv_event_get_target_obj(e);
    
    slider_id_t id = (slider_id_t)(uintptr_t)lv_event_get_user_data(e);

    int32_t value = lv_slider_get_value(slider);

    char buf[8];
    lv_snprintf(buf, sizeof(buf), "%d", value);

    lv_span_t * span;

    switch(id) {
        case SLIDER_LEFT:
            span = lv_spangroup_get_child(sliderLeftSpan, 0);
            lv_span_set_text(span, buf);
            lv_spangroup_refresh(sliderLeftSpan);
            break;
        case SLIDER_RIGHT:
            span = lv_spangroup_get_child(sliderRightSpan, 0);
            lv_span_set_text(span, buf);
            lv_spangroup_refresh(sliderRightSpan);
            break;
        default:
            break;
    }
}

static void build_buttons() {
    lv_obj_t * label1;

    lv_obj_t * btn1 = lv_button_create(lv_screen_active());
    lv_obj_set_size(btn1, 140, 50);
    lv_obj_align(btn1, LV_ALIGN_BOTTOM_MID, -80, 0);
    lv_obj_add_flag(btn1, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_add_event_cb(btn1, button_event_cb, LV_EVENT_VALUE_CHANGED, (void *)BUTTON_LEFT);

    label1 = lv_label_create(btn1);
    lv_label_set_text(label1, "PWR Limit");
    lv_obj_align(label1, LV_ALIGN_TOP_LEFT, 0, 0);

    buttonLeftValueLabel = lv_label_create(btn1);
    lv_label_set_text(buttonLeftValueLabel, "OFF");
    lv_obj_align(buttonLeftValueLabel, LV_ALIGN_BOTTOM_LEFT, 0, 8);

    #ifdef CONFIG_LV_Z_KEYPAD_INPUT
        lv_group_t * btn1_group = lv_group_create();
        lv_group_add_obj(btn1_group, btn1);
        lv_indev_set_group(lvgl_input_get_indev(lvgl_keypad_left), btn1_group);
    #endif /* CONFIG_LV_Z_KEYPAD_INPUT */

    lv_obj_t * label2;

    lv_obj_t * btn2 = lv_button_create(lv_screen_active());
    lv_obj_set_size(btn2, 140, 50);
    lv_obj_align(btn2, LV_ALIGN_BOTTOM_MID, 80, 0);
    lv_obj_add_flag(btn2, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_add_event_cb(btn2, button_event_cb, LV_EVENT_VALUE_CHANGED, (void *)BUTTON_RIGHT);

    label2 = lv_label_create(btn2);
    lv_label_set_text(label2, "TQV");
    lv_obj_align(label2, LV_ALIGN_TOP_LEFT, 0, 0);

    buttonRightValueLabel = lv_label_create(btn2);
    lv_label_set_text(buttonRightValueLabel, "OFF");
    lv_obj_align(buttonRightValueLabel, LV_ALIGN_BOTTOM_LEFT, 0, 8);

    #ifdef CONFIG_LV_Z_KEYPAD_INPUT
        lv_group_t * btn2_group = lv_group_create();
        lv_group_add_obj(btn2_group, btn2);
        lv_indev_set_group(lvgl_input_get_indev(lvgl_keypad_right), btn2_group);
    #endif /* CONFIG_LV_Z_KEYPAD_INPUT */
}

static void button_event_cb(lv_event_t * e) {
    lv_obj_t * button = lv_event_get_target_obj(e);
    button_id_t id = (button_id_t)(uintptr_t)lv_event_get_user_data(e);

    switch(id) {
        case BUTTON_LEFT:
            if (lv_obj_has_state(button, LV_STATE_CHECKED) == true) {
                lv_label_set_text(buttonLeftValueLabel, "ON");
            } else {
                lv_label_set_text(buttonLeftValueLabel, "OFF");
            }
            break;
        case BUTTON_RIGHT:
            if (lv_obj_has_state(button, LV_STATE_CHECKED) == true) {
                lv_label_set_text(buttonRightValueLabel, "ON");
            } else {
                lv_label_set_text(buttonRightValueLabel, "OFF");
            }
            break;
        default:
            break;
    }
}

/* ── Public Function Implementations ─────────────────────────────────────────────────────────── */
 
int main(void)
{
    const struct device *display_dev;
    int ret;

    display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
    if (!device_is_ready(display_dev)) {
        LOG_ERR("Display device not ready");
        return 0;
    }

    lv_obj_t *scr = lv_screen_active();
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    build_header();
    build_sliders();
    build_buttons();

    lv_timer_handler();

    ret = display_blanking_off(display_dev);
    if (ret < 0 && ret != -ENOSYS) {
        LOG_ERR("Failed to disable blanking (err %d)", ret);
        return 0;
    }
    #ifdef CONFIG_PWM
        if (!pwm_is_ready_dt(&pwm_pizeo)) {
            printk("Error: PWM device %s is not ready\n",
                pwm_pizeo.dev->name);
            return 0;
        }

        uint32_t period = MAX_PERIOD;

        pwm_set_dt(&pwm_pizeo, period, period / 2U);
    #endif /* CONFIG_PWM */ 

    while (1) {
        lv_timer_handler();
        k_sleep(K_MSEC(10));
    }

    return 0;
}