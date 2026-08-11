/**
 * @file        ui_header.c
 * @brief       Reusable header widget — title bar with device status icons
 *
 * @author      Mario Wegmann <mario.wegmann@web.de>
 * @date        Created: 2026-08-04
 */

#include "modules/ui/widgets/ui_header.h"

#include <lvgl.h>

#include "modules/ui/ui_styles.h"
#include "services/event_bus/events.h"

/* ── Layout constants ────────────────────────────────────────────────────── */

#define HEADER_HEIGHT_PCT   15
#define ICON_ROW_MARGIN_R   4
#define ICON_COL_GAP        6
#define BLINK_HALF_MS       400U

/* ── Per-slot icon configuration ─────────────────────────────────────────── */

static const ui_header_slot_cfg_t k_slot_cfg[UI_DEVICE_SLOT_COUNT] = {
    [UI_DEVICE_LOGGER]  = { .symbol = FA_SYMBOL_VIDEO         },
    [UI_DEVICE_ROS]     = { .symbol = FA_SYMBOL_ROBOT         },
    [UI_DEVICE_DV_PC]   = { .symbol = FA_SYMBOL_DESKTOP_SOLID },
    [UI_DEVICE_KISTLER] = { .symbol = FA_SYMBOL_RULER         },
    [UI_DEVICE_MABX]    = { .symbol = FA_SYMBOL_MICROCHIP     },
    [UI_DEVICE_SDCS]    = { .symbol = FA_SYMBOL_POWER_OFF     },
    [UI_DEVICE_CAN]     = { .symbol = FA_SYMBOL_NETWORK_SOLID },
};

/* ── Private helpers ─────────────────────────────────────────────────────── */

static lv_color_t status_to_color(enum ui_device_status s)
{
    switch (s) {
    case UI_DEVICE_STATUS_WARN:     return UI_C_ACCENT;
    case UI_DEVICE_STATUS_FAULT:    return UI_C_RED;
    case UI_DEVICE_STATUS_ACTIVE: return UI_C_RED;
    case UI_DEVICE_STATUS_OK:       return UI_C_GREEN;
    default:                        return UI_C_GREEN;
    }
}

/* ── Shared blink state ──────────────────────────────────────────────────── */
/*
 * All blinking slot containers observe a single phase subject toggled by one
 * shared timer.  This guarantees perfect sync (same subject notification round)
 * and zero drift (one timer, no per-slot phase offset).
 *
 * Opacity is used instead of LV_OBJ_FLAG_HIDDEN so the flex row does not
 * reflow when a slot disappears, which would shift neighbouring icons.
 */

static lv_subject_t s_blink_phase;  /* 0 = opaque, 1 = transparent */
static lv_timer_t  *s_blink_timer;
static uint32_t     s_blink_count;  /* nr of actively blinking containers */

static void blink_phase_cb(lv_observer_t *observer, lv_subject_t *subject)
{
    lv_obj_t *cont = lv_observer_get_target_obj(observer);
    lv_opa_t opa = lv_subject_get_int(subject) ? LV_OPA_TRANSP : LV_OPA_COVER;
    lv_obj_set_style_opa(cont, opa, 0);
}

static void blink_tick_cb(lv_timer_t *timer)
{
    (void)timer;
    lv_subject_set_int(&s_blink_phase, !lv_subject_get_int(&s_blink_phase));
}

static void blink_delete_event_cb(lv_event_t *e)
{
    (void)e;
    /*
     * lv_subject_add_observer_obj already registered its own LV_EVENT_DELETE
     * callback that calls lv_observer_remove() and frees the observer before
     * this callback runs.  Do NOT touch the observer pointer here — it is
     * already freed memory.  Only manage the shared count and timer.
     */
    if (s_blink_count > 0 && --s_blink_count == 0) {
        lv_timer_delete(s_blink_timer);
        s_blink_timer = NULL;
    }
}

static void blink_start(lv_obj_t *cont)
{
    if (lv_obj_get_user_data(cont) != NULL) {
        return; /* already subscribed */
    }
    if (s_blink_count == 0) {
        lv_subject_init_int(&s_blink_phase, 0);
        s_blink_timer = lv_timer_create(blink_tick_cb, BLINK_HALF_MS, NULL);
    }
    s_blink_count++;
    lv_observer_t *obs = lv_subject_add_observer_obj(&s_blink_phase,
                                                      blink_phase_cb,
                                                      cont, NULL);
    lv_obj_set_user_data(cont, obs);
    lv_obj_add_event_cb(cont, blink_delete_event_cb, LV_EVENT_DELETE, NULL);
}

static void blink_stop(lv_obj_t *cont)
{
    lv_observer_t *obs = lv_obj_get_user_data(cont);
    if (obs == NULL) {
        return;
    }
    lv_observer_remove(obs);
    lv_obj_set_user_data(cont, NULL);
    lv_obj_set_style_opa(cont, LV_OPA_COVER, 0);
    if (s_blink_count > 0 && --s_blink_count == 0) {
        lv_timer_delete(s_blink_timer);
        s_blink_timer = NULL;
    }
}

/**
 * @brief Observer callback — fired by LVGL whenever a device status subject
 *        changes value.  The target object is the slot container (cont).
 *
 *        Child layout inside cont (fixed insertion order from ui_header_create):
 *          child[0] → img     (device icon)
 *          child[1] → overlay (offline X)
 */
static void slot_status_observer_cb(lv_observer_t *observer, lv_subject_t *subject)
{
    lv_obj_t *cont    = lv_observer_get_target_obj(observer);
    lv_obj_t *img     = lv_obj_get_child(cont, 0);
    // lv_obj_t *overlay = lv_obj_get_child(cont, 1);

    enum ui_device_status cur  = (enum ui_device_status)lv_subject_get_int(subject);
    enum ui_device_status prev = (enum ui_device_status)lv_subject_get_previous_int(subject);

    // lv_obj_set_style_image_recolor(img, status_to_color(cur), 0);
    lv_obj_set_style_text_color(img, status_to_color(cur), LV_PART_MAIN);

    bool should_blink = (cur  == UI_DEVICE_STATUS_ACTIVE);
    bool was_blinking = (prev == UI_DEVICE_STATUS_ACTIVE);

    if (should_blink && !was_blinking) {
        blink_start(cont);
    } else if (!should_blink && was_blinking) {
        blink_stop(cont);
    }
}

/* ── Public API ──────────────────────────────────────────────────────────── */


lv_obj_t *ui_header_create(lv_obj_t    *parent,
                            const char  *title,
                            lv_subject_t status_subjects[])
{
    /* ── Header container ──────────────────────────────────────────────── */
    lv_obj_t *header = lv_obj_create(parent);
    lv_obj_remove_style_all(header);
    lv_obj_add_style(header, &ui_style_header, 0);
    lv_obj_set_width(header,  lv_pct(100));
    lv_obj_set_height(header, lv_pct(HEADER_HEIGHT_PCT));
    lv_obj_align(header, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    /* ── Title label (left side) — child index 0 of header ─────────────── */
    lv_obj_t *lbl = lv_label_create(header);
    lv_obj_add_style(lbl, &ui_style_label_title, 0);
    lv_label_set_text(lbl, title);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 10, 0);

    /* ── Icon row (right side, flex) — child index 1 of header ─────────── */
    lv_obj_t *icon_row = lv_obj_create(header);
    lv_obj_remove_style_all(icon_row);
    lv_obj_set_size(icon_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(icon_row, LV_ALIGN_RIGHT_MID, -ICON_ROW_MARGIN_R, 0);
    lv_obj_set_layout(icon_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(icon_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(icon_row,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(icon_row, ICON_COL_GAP, 0);
    lv_obj_clear_flag(icon_row, LV_OBJ_FLAG_SCROLLABLE);

    /* ── One slot per device ────────────────────────────────────────────── */
    for (uint8_t i = 0U; i < UI_DEVICE_SLOT_COUNT; i++) {
        /* Slot container — child[i] of icon_row */
        lv_obj_t *cont = lv_obj_create(icon_row);
        
        lv_obj_remove_style_all(cont);
        lv_obj_set_size(cont, 24U, 24U);
        lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

        /* Device icon — child[0] of cont */
        lv_obj_t *img = lv_label_create(cont);
        lv_obj_set_style_text_font(img, &FontAwesome_Solid_18, 0);
        lv_label_set_text(img, k_slot_cfg[i].symbol);
        lv_obj_center(img);

        /*
         * Subscribe cont to its status subject.
         * LVGL fires the callback immediately with the current value so the
         * initial visual state is applied without a separate call.
         * The observer is automatically removed when cont is deleted.
         */
        lv_subject_add_observer_obj(&status_subjects[i],
                                    slot_status_observer_cb,
                                    cont,
                                    NULL);
    }

    return header;
}
