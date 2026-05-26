/**
 * @file        main.c
 * @brief       <Short one-line description of this file's purpose>
 *
 * @details     <Optional extended description. Explain the module's role,
 *              any important design decisions, or usage notes.>
 *
 * @author      Mario Wegmann <mario.wegmann@web.de>
 * @date        Created: 2026-05-21
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
 * 0.1.0    2026-05-21  Mario Wegmann   Initial creation
 * ─────────────────────────────────────────────────────────────────────────────────────────────────
 */

/* ── Corresponding Header ────────────────────────────────────────────────────────────────────── */
 
/* ── Zephyr Includes ─────────────────────────────────────────────────────────────────────────── */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <zephyr/logging/log.h>
 
/* ── LVGL Include ────────────────────────────────────────────────────────────────────────────── */
#include <lvgl.h>
 
/* ── Standard Includes ───────────────────────────────────────────────────────────────────────── */
#include <stdio.h>

/* ── Zephyr Logging ──────────────────────────────────────────────────────────────────────────── */
LOG_MODULE_REGISTER(app, CONFIG_LOG_DEFAULT_LEVEL);
 
/* ── Private Macros & Constants ──────────────────────────────────────────────────────────────── */
 
#define C_BG      lv_color_hex(0xFAF8F3)
#define C_DARK    lv_color_hex(0x1A1A1A)
#define C_YELLOW  lv_color_hex(0xDDCC00)
#define C_GREEN   lv_color_hex(0x15803D)
#define C_WHITE   lv_color_hex(0xFFFFFF)
#define C_BAR_BG  lv_color_hex(0xF5F0E0)
#define C_BORDER  lv_color_hex(0x333333)
 
/* ── Private Type Definitions ────────────────────────────────────────────────────────────────── */
 
typedef enum {
    SLIDER_LEFT,
    SLIDER_RIGHT,
    SLIDER_MIDDLE
} slider_id_t;

typedef enum {
    BUTTON_LEFT,
    BUTTON_RIGHT
} button_id_t;
 
/* ── Private Variables ───────────────────────────────────────────────────────────────────────── */

static lv_obj_t * sliderLeftSpan;
static lv_obj_t * sliderRightSpan;
static lv_obj_t * sliderMiddleSpan;

static lv_obj_t * buttonLeftValueLabel;
static lv_obj_t * buttonRightValueLabel;

LV_FONT_DECLARE(BarlowCondensed_BoldItalic_18)
LV_FONT_DECLARE(BarlowCondensed_BoldItalic_32)
LV_FONT_DECLARE(BarlowCondensed_BoldItalic_100)
LV_FONT_DECLARE(BarlowCondensed_Italic_20)
LV_FONT_DECLARE(BarlowCondensed_Italic_44)
 
/* ── Private Function Prototypes ─────────────────────────────────────────────────────────────── */
 
static void slider_event_cb(lv_event_t * e);
static void button_event_cb(lv_event_t * e);
 
/* ── Private Function Implementations ───────────────────────────────────────────────────────── */
 
static void build_header() {
    static const lv_color_t grad_colors[2] = {
        LV_COLOR_MAKE(0xDD, 0xCC, 0x00),
        LV_COLOR_MAKE(0x1A, 0x1A, 0x1A),
    };

    static const lv_opa_t grad_opa[2] = {
        LV_OPA_100,
        LV_OPA_100,
    };

    static lv_grad_dsc_t grad;
    lv_grad_init_stops(&grad, grad_colors, grad_opa, NULL, 2);
    lv_grad_linear_init(&grad, 159, 9, 160, 10, LV_GRAD_EXTEND_PAD);

    static lv_style_t style_shadow;
    lv_style_init(&style_shadow);
    lv_style_set_bg_grad(&style_shadow, &grad);
    lv_style_set_pad_all(&style_shadow, 0);
    lv_style_set_radius(&style_shadow, 0);
    lv_style_set_border_width(&style_shadow, 0);

    lv_obj_t * header;
    header = lv_obj_create(lv_screen_active());
    lv_obj_add_style(header, &style_shadow, 0);
    lv_obj_set_size(header, 100, 50);
    lv_obj_align(header, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_height(header, lv_pct(15));
    lv_obj_set_width(header, lv_pct(100));
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(header);
    lv_obj_set_style_text_font(title, &BarlowCondensed_BoldItalic_32, 0);
    lv_obj_set_style_text_color(title, C_DARK, 0);
    lv_label_set_text(title, "RTD");
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 10, 0);
}

static void build_sliders() {
    static lv_style_t style_main;
    static lv_style_t style_indicator;

    lv_style_init(&style_main);
    lv_style_set_bg_opa(&style_main, LV_OPA_COVER);
    lv_style_set_bg_color(&style_main, C_WHITE);
    lv_style_set_outline_color(&style_main, C_DARK);
    lv_style_set_outline_width(&style_main, 2);
    lv_style_set_radius(&style_main, 0);

    lv_style_init(&style_indicator);
    lv_style_set_bg_opa(&style_indicator, LV_OPA_COVER);
    lv_style_set_bg_color(&style_indicator, C_GREEN);
    

    lv_obj_t * sliderLeft = lv_slider_create(lv_screen_active());
    lv_obj_remove_style_all(sliderLeft);
    lv_obj_add_style(sliderLeft, &style_main, LV_PART_MAIN);
    lv_obj_add_style(sliderLeft, &style_indicator, LV_PART_INDICATOR);
    lv_obj_set_size(sliderLeft, 40, 200);
    lv_obj_set_size(sliderLeft, 40, 200);
    lv_obj_set_pos(sliderLeft, 10, 70);
    lv_obj_add_event_cb(sliderLeft, slider_event_cb, LV_EVENT_VALUE_CHANGED, (void *)SLIDER_LEFT);

    lv_obj_t * sliderLeft_Titlelabel = lv_label_create(lv_screen_active());
    lv_obj_set_style_text_font(sliderLeft_Titlelabel, &BarlowCondensed_BoldItalic_18, 0);
    lv_label_set_text(sliderLeft_Titlelabel, "TQG F");
    lv_obj_align_to(sliderLeft_Titlelabel, sliderLeft, LV_ALIGN_OUT_TOP_MID, 0, 0);

    sliderLeftSpan = lv_spangroup_create(lv_screen_active());

    lv_obj_set_width(sliderLeftSpan, 60);
    lv_obj_set_height(sliderLeftSpan, 60);
    lv_obj_set_style_text_align(sliderLeftSpan, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    lv_obj_align_to(sliderLeftSpan, sliderLeft, LV_ALIGN_OUT_BOTTOM_MID, 0, 5);

    lv_span_t * spanL1 = lv_spangroup_new_span(sliderLeftSpan);
    lv_style_set_text_font(lv_span_get_style(spanL1), &BarlowCondensed_BoldItalic_32);
    lv_span_set_text(spanL1, "0");

    lv_span_t * spanL2 = lv_spangroup_new_span(sliderLeftSpan);
    lv_style_set_text_font(lv_span_get_style(spanL2), &BarlowCondensed_Italic_20);
    lv_span_set_text(spanL2, "%");

    lv_obj_t * sliderRight = lv_slider_create(lv_screen_active());
    lv_obj_remove_style_all(sliderRight);
    lv_obj_add_style(sliderRight, &style_main, LV_PART_MAIN);
    lv_obj_add_style(sliderRight, &style_indicator, LV_PART_INDICATOR);
    lv_obj_set_size(sliderRight, 40, 200);
    lv_obj_set_pos(sliderRight, 430, 70);
    lv_obj_add_event_cb(sliderRight, slider_event_cb, LV_EVENT_VALUE_CHANGED, (void *)SLIDER_RIGHT);

    lv_obj_t * sliderRight_Titlelabel = lv_label_create(lv_screen_active());
    lv_obj_set_style_text_font(sliderRight_Titlelabel, &BarlowCondensed_BoldItalic_18, 0);
    lv_label_set_text(sliderRight_Titlelabel, "TQG R");
    lv_obj_align_to(sliderRight_Titlelabel, sliderRight, LV_ALIGN_OUT_TOP_MID, 0, 0);

    sliderRightSpan = lv_spangroup_create(lv_screen_active());

    lv_obj_set_width(sliderRightSpan, 60);
    lv_obj_set_height(sliderRightSpan, 60);
    lv_obj_set_style_text_align(sliderRightSpan, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    lv_obj_align_to(sliderRightSpan, sliderRight, LV_ALIGN_OUT_BOTTOM_MID, 0, 5);

    lv_span_t * spanR1 = lv_spangroup_new_span(sliderRightSpan);
    lv_style_set_text_font(lv_span_get_style(spanR1), &BarlowCondensed_BoldItalic_32);
    lv_span_set_text(spanR1, "0");

    lv_span_t * spanR2 = lv_spangroup_new_span(sliderRightSpan);
    lv_style_set_text_font(lv_span_get_style(spanR2), &BarlowCondensed_Italic_20);
    lv_span_set_text(spanR2, "%");

    lv_obj_t * sliderMiddle = lv_slider_create(lv_screen_active());
    lv_obj_remove_style_all(sliderMiddle);
    lv_obj_add_style(sliderMiddle, &style_main, LV_PART_MAIN);
    lv_obj_add_style(sliderMiddle, &style_indicator, LV_PART_INDICATOR);
    lv_obj_set_size(sliderMiddle, 300, 20);
    lv_obj_align(sliderMiddle, LV_ALIGN_BOTTOM_MID, 0, -60);
    lv_obj_add_event_cb(sliderMiddle, slider_event_cb, LV_EVENT_VALUE_CHANGED, (void *)SLIDER_MIDDLE);

    lv_slider_set_value(sliderMiddle, 68, LV_ANIM_OFF);

    lv_obj_t * sliderMiddle_Titlelabel = lv_label_create(lv_screen_active());
    lv_obj_set_style_text_font(sliderMiddle_Titlelabel, &BarlowCondensed_BoldItalic_18, 0);
    lv_label_set_text(sliderMiddle_Titlelabel, "HV SoC");
    lv_obj_align_to(sliderMiddle_Titlelabel, sliderMiddle, LV_ALIGN_OUT_TOP_LEFT, 0, 0);

    sliderMiddleSpan = lv_spangroup_create(lv_screen_active());
    lv_obj_set_width(sliderMiddleSpan, 60);
    lv_obj_set_height(sliderMiddleSpan, 30);
    lv_obj_set_style_text_align(sliderMiddleSpan, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);

    lv_obj_align_to(sliderMiddleSpan, sliderMiddle, LV_ALIGN_OUT_TOP_RIGHT, 0, 0);

    lv_span_t * spanM1 = lv_spangroup_new_span(sliderMiddleSpan);
    lv_style_set_text_font(lv_span_get_style(spanM1), &BarlowCondensed_BoldItalic_32);
    lv_span_set_text(spanM1, "68");
    lv_style_set_text_align(lv_span_get_style(spanM1), LV_TEXT_ALIGN_RIGHT);

    lv_span_t * spanM2 = lv_spangroup_new_span(sliderMiddleSpan);
    lv_style_set_text_font(lv_span_get_style(spanM2), &BarlowCondensed_Italic_20);
    lv_span_set_text(spanM2, "%");
    lv_style_set_text_align(lv_span_get_style(spanM2), LV_TEXT_ALIGN_RIGHT);
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
        case SLIDER_MIDDLE:
            span = lv_spangroup_get_child(sliderMiddleSpan, 0);
            lv_span_set_text(span, buf);
            lv_spangroup_refresh(sliderMiddleSpan);
            break;
        default:
            break;
    }
}

static void build_buttons() {
    static lv_style_t style;
    lv_style_init(&style);

    lv_style_set_radius(&style, 0);
    lv_style_set_bg_opa(&style, LV_OPA_100);
    lv_style_set_bg_color(&style, C_WHITE);

    lv_style_set_outline_width(&style, 2);
    lv_style_set_outline_color(&style, C_DARK);

    lv_style_set_text_color(&style, C_DARK);
    lv_style_set_pad_all(&style, 2);

    static lv_style_t style_checked;
    lv_style_init(&style_checked);

    lv_style_set_radius(&style_checked, 0);
    lv_style_set_bg_opa(&style_checked, LV_OPA_100);
    lv_style_set_bg_color(&style_checked, C_GREEN);

    lv_style_set_outline_width(&style_checked, 2);
    lv_style_set_outline_color(&style_checked, C_DARK);

    lv_style_set_text_color(&style_checked, C_WHITE);
    lv_style_set_pad_all(&style_checked, 2);


    lv_obj_t * label1;

    lv_obj_t * btn1 = lv_button_create(lv_screen_active());
    lv_obj_remove_style_all(btn1);
    lv_obj_add_style(btn1, &style, 0);
    lv_obj_add_style(btn1, &style_checked, LV_STATE_CHECKED);
    lv_obj_set_size(btn1, 140, 50);
    lv_obj_align(btn1, LV_ALIGN_BOTTOM_MID, -80, 0);
    lv_obj_add_flag(btn1, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_add_event_cb(btn1, button_event_cb, LV_EVENT_VALUE_CHANGED, (void *)BUTTON_LEFT);

    label1 = lv_label_create(btn1);
    lv_obj_set_style_text_font(label1, &BarlowCondensed_BoldItalic_18, 0);
    lv_label_set_text(label1, "PWR Limit");
    lv_obj_align(label1, LV_ALIGN_TOP_LEFT, 0, 0);

    buttonLeftValueLabel = lv_label_create(btn1);
    lv_obj_set_style_text_font(buttonLeftValueLabel, &BarlowCondensed_BoldItalic_32, 0);
    lv_label_set_text(buttonLeftValueLabel, "OFF");
    lv_obj_align(buttonLeftValueLabel, LV_ALIGN_BOTTOM_LEFT, 0, 8);

    lv_obj_t * label2;

    lv_obj_t * btn2 = lv_button_create(lv_screen_active());
    lv_obj_remove_style_all(btn2);
    lv_obj_add_style(btn2, &style, 0);
    lv_obj_add_style(btn2, &style_checked, LV_STATE_CHECKED);
    lv_obj_set_size(btn2, 140, 50);
    lv_obj_align(btn2, LV_ALIGN_BOTTOM_MID, 80, 0);
    lv_obj_add_flag(btn2, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_add_event_cb(btn2, button_event_cb, LV_EVENT_VALUE_CHANGED, (void *)BUTTON_RIGHT);

    label2 = lv_label_create(btn2);
    lv_obj_set_style_text_font(label2, &BarlowCondensed_BoldItalic_18, 0);
    lv_label_set_text(label2, "TQV");
    lv_obj_align(label2, LV_ALIGN_TOP_LEFT, 0, 0);

    buttonRightValueLabel = lv_label_create(btn2);
    lv_obj_set_style_text_font(buttonRightValueLabel, &BarlowCondensed_BoldItalic_32, 0);
    lv_label_set_text(buttonRightValueLabel, "OFF");
    lv_obj_align(buttonRightValueLabel, LV_ALIGN_BOTTOM_LEFT, 0, 8);
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
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
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

    while (1) {
        lv_timer_handler();
        k_sleep(K_MSEC(10));
    }

    return 0;
}