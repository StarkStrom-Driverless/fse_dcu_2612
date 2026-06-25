/**
 * @file        ui.c
 * @brief       UI module — LVGL task thread, screen carousel, encoder routing
 *
 * @details     Central coordinator for all display output.
 *
 *              Thread model
 *              ────────────
 *              One dedicated LVGL task thread (priority 8) calls
 *              lv_timer_handler() at UI_TASK_PERIOD_MS intervals.  All LVGL
 *              API calls (screen loads, style changes, label updates) happen
 *              inside this thread to preserve LVGL's single-thread assumption.
 *
 *              Left encoder — screen carousel
 *              ──────────────────────────────
 *              INPUT_CALLBACK_DEFINE captures INPUT_REL_WHEEL events from
 *              qdec_input0 in the Zephyr input workqueue context and accumulates
 *              them in atomic_t s_nav_delta.  The LVGL thread drains this delta
 *              each iteration and calls carousel_navigate() if non-zero.
 *
 *              Carousel layout (left → right):
 *                index 0 : SCREEN_MISSION_SELECT
 *                index 1 : SCREEN_BOOT  ← starting position
 *
 *              Right encoder — in-screen widget navigation
 *              ────────────────────────────────────────────
 *              The second LVGL encoder indev (lvgl_encoder1) is assigned to the
 *              active screen's lv_group_t via lv_indev_set_group().  Screens
 *              without interactive content receive NULL (indev ignored by LVGL).
 *
 *              Zbus — App → UI commands
 *              ─────────────────────────
 *              ZBUS_SUBSCRIBER_DEFINE(ui_cmd_sub) subscribes to ui_cmd_chan.
 *              The LVGL thread polls the subscriber queue with K_NO_WAIT each
 *              iteration so commands are processed without a dedicated thread.
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

#include "modules/ui/ui.h"

/* ── Zephyr Includes ─────────────────────────────────────────────────────────────────────────── */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <zephyr/input/input.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/logging/log.h>

/* ── LVGL Include ────────────────────────────────────────────────────────────────────────────── */

#include <lvgl.h>
#include <lvgl_input_device.h>

/* ── Project Includes ────────────────────────────────────────────────────────────────────────── */

#include "modules/ui/ui_styles.h"
#include "modules/ui/screens/screen_boot.h"
#include "modules/ui/screens/screen_mission_select.h"
#include "modules/ui/screens/screen_checklist.h"
#include "modules/ui/screens/screen_debug_lv_accu.h"
#include "modules/ui/screens/screen_debug_hv_accu.h"
#include "modules/ui/screens/screen_debug_pressure.h"
#include "modules/ui/screens/screen_debug_tractive_system.h"
#include "modules/ui/screens/screen_debug_write.h"
#include "modules/ui/screens/screen_ev_driving.h"
#include "generated/ui_subjects_gen.h"
#include "services/event_bus/event_bus.h"
#include "services/event_bus/events.h"

/* ── Zephyr Logging ──────────────────────────────────────────────────────────────────────────── */

LOG_MODULE_REGISTER(ui_module, CONFIG_LOG_DEFAULT_LEVEL);


/* ── Private Macros & Constants ──────────────────────────────────────────────────────────────── */

/** @brief LVGL task period in milliseconds (~100 fps upper bound). */
#define UI_TASK_PERIOD_MS       10U

/** @brief Screen slide animation duration in milliseconds. */
#define UI_ANIM_DURATION_MS     300U

/** @brief Stack size for the LVGL task thread. */
#define UI_THREAD_STACK_SIZE    16384U

/** @brief Scheduling priority for the LVGL task thread (lowest in the system). */
#define UI_THREAD_PRIORITY      8

/**
 * @brief Total number of screen_id slots.
 * Sized to SCREEN_ERROR+1 so any valid enum value is a safe index.
 */
#define SCREEN_ID_COUNT         (SCREEN_ERROR + 1U)


/* ── Carousel Definition ─────────────────────────────────────────────────────────────────────── */

/**
 * @brief Left-to-right ordered list of screens reachable via the left encoder.
 *
 * Index 0 is the leftmost screen; rotating CCW moves to lower indices.
 * The starting position is SCREEN_BOOT (index 1).
 */
static const enum screen_id k_carousel[] = {
    SCREEN_DEBUG_LV_ACCU,   /* index 1 — CCW from boot   */
    SCREEN_DEBUG_HV_ACCU,   /* index 2 — CCW from boot   */
    SCREEN_DEBUG_PRESSURE,  /* index 3 — CCW from boot   */
    SCREEN_DEBUG_TS,        /* index 4 — CCW from boot   */
    SCREEN_DEBUG_WRITE,     /* index 5 — CCW from boot   */
    SCREEN_BOOT,            /* index 6 — initial screen  */
    SCREEN_MISSION_SELECT,  /* index 7 — CW from boot    */
    SCREEN_PRE_RTD,         /* index 8 — CW from Mission */
    SCREEN_EV_DRIVING,      /* index 9 - EV Driving      */
};

#define CAROUSEL_LEN    ARRAY_SIZE(k_carousel)


/* ── Private Variables ───────────────────────────────────────────────────────────────────────── */

/**
 * @brief Screen object pool, indexed by enum screen_id.
 *
 * Entries are NULL for screens not yet implemented.  Checked before any
 * lv_screen_load_anim() call.
 */
static lv_obj_t *s_screens[SCREEN_ID_COUNT];

/** @brief Currently active screen identifier. */
static enum screen_id s_active_screen = SCREEN_NONE;

/** @brief Current position in the carousel array. */
static uint8_t s_carousel_pos = SCREEN_BOOT;    /* starts at SCREEN_BOOT */

/**
 * @brief Accumulated left encoder delta from the input callback.
 *
 * Written by left_encoder_cb() (input workqueue context) via atomic_add().
 * Read and cleared by the LVGL thread via atomic_set(&s_nav_delta, 0).
 */
static atomic_t s_nav_delta;

/**
 * @brief LVGL indev for the right encoder (lvgl_encoder1).
 *
 * Resolved once in ui_module_init() by iterating registered encoder indevs.
 * Assigned to the active screen's group on each screen transition.
 */
static lv_indev_t *s_right_enc_indev;

/**
 * @brief LVGL indev for the right encoder (lvgl_encoder1).
 *
 * Resolved once in ui_module_init() by iterating registered encoder indevs.
 * Assigned to the active screen's group on each screen transition.
 */
static lv_indev_t *s_left_btn_indev;

/**
 * @brief LVGL indev for the right encoder (lvgl_encoder1).
 *
 * Resolved once in ui_module_init() by iterating registered encoder indevs.
 * Assigned to the active screen's group on each screen transition.
 */
static lv_indev_t *s_right_btn_indev;

/** @brief Thread control block for the LVGL task thread. */
static struct k_thread s_ui_thread;

/** @brief Stack storage for the LVGL task thread. */
static K_THREAD_STACK_DEFINE(s_ui_stack, UI_THREAD_STACK_SIZE);

/**
 * @brief Display device for backlight control.
 *
 * Resolved in ui_module_init() and used by ui_thread_fn() to enable the
 * backlight after the first rendered frame.  NULL if the device is not ready.
 */
static const struct device *s_disp_dev;


/* ── Zbus Subscriber ─────────────────────────────────────────────────────────────────────────── */

/** @brief Subscriber for ui_cmd_chan (App Layer → UI module). */
ZBUS_SUBSCRIBER_DEFINE(ui_cmd_sub, 4);
ZBUS_CHAN_ADD_OBS(ui_cmd_chan, ui_cmd_sub, 0);


/* ── Left Encoder Input Callback ─────────────────────────────────────────────────────────────── */

/**
 * @brief Capture left encoder rotation events for carousel navigation.
 *
 * Runs in the Zephyr input workqueue context — LVGL API must not be called
 * here.  The delta is accumulated atomically and drained by the LVGL thread.
 *
 * Positive value → CW  (navigate right in carousel)
 * Negative value → CCW (navigate left  in carousel)
 */
static void left_encoder_cb(struct input_event *evt, void *user_data)
{
    ARG_UNUSED(user_data);

    if (evt->type == INPUT_EV_REL && evt->code == INPUT_REL_WHEEL) {
        atomic_add(&s_nav_delta, (atomic_val_t)evt->value);
    }
}

/*
 * Register the callback for qdec_input0 (left encoder) only.
 * Events from qdec_input1 and the keypad devices are not received here.
 */
INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(DT_ALIAS(qdec_input_left)), left_encoder_cb, NULL);


/* ── Private Function Prototypes ─────────────────────────────────────────────────────────────── */

static lv_indev_t *get_encoder_indev(uint8_t index);
static void        set_encoder_group(enum screen_id id);
static void        ui_load_screen(enum screen_id id, lv_scr_load_anim_t anim);
static void        carousel_navigate(int32_t delta);
static void        handle_ui_cmd(const struct ui_cmd *cmd);
static void        ui_thread_fn(void *p1, void *p2, void *p3);


/* ── Private Function Implementations ───────────────────────────────────────────────────────── */

/**
 * @brief Return the Nth LVGL encoder indev (0-indexed).
 *
 * Relies on the Zephyr init order: lvgl_encoder0 registers before lvgl_encoder1.
 * Passing index=0 returns the left encoder indev; index=1 the right.
 *
 * @param index  0-based index among encoder-type indevs.
 * @return Pointer to the indev, or NULL if not found.
 */
static lv_indev_t *get_encoder_indev(uint8_t index)
{
    lv_indev_t *indev = lv_indev_get_next(NULL);
    uint8_t     n     = 0U;

    while (indev != NULL) {
        if (lv_indev_get_type(indev) == LV_INDEV_TYPE_ENCODER) {
            if (n == index) {
                return indev;
            }
            n++;
        }
        indev = lv_indev_get_next(indev);
    }
    return NULL;
}

/**
 * @brief Assign the right encoder indev to the group of the given screen.
 *
 * Screens without interactive content pass NULL, which detaches the encoder
 * so LVGL ignores its events (useful on the boot screen where any encoder
 * input drives carousel navigation, not widget focus).
 *
 * @param id  The screen that is about to become active.
 */
static void set_encoder_group(enum screen_id id)
{
    lv_group_t *right_encoder_group = NULL;
    lv_group_t *right_button_group = NULL;
    lv_group_t *left_button_group = NULL;

    switch (id) {
    case SCREEN_MISSION_SELECT:
        right_encoder_group = screen_mission_select_get_right_encoder_group();
        left_button_group = screen_mission_select_get_left_button_group();
        right_button_group = screen_mission_select_get_right_button_group();
        break;
    case SCREEN_PRE_RTD:
        right_encoder_group = screen_checklist_get_right_encoder_group();
        left_button_group = screen_checklist_get_left_button_group();
        right_button_group = screen_checklist_get_right_button_group();
        break;
    case SCREEN_DEBUG_WRITE:
        right_encoder_group = screen_debug_write_get_right_encoder_group();
        left_button_group = screen_debug_write_get_left_button_group();
        right_button_group = screen_debug_write_get_right_button_group();
        break;

    case SCREEN_EV_DRIVING:
        right_encoder_group = screen_ev_driving_get_right_encoder_group();
        left_button_group = screen_ev_driving_get_left_button_group();
        right_button_group = screen_ev_driving_get_right_button_group();
        break;

    default:
        right_encoder_group = NULL;
        right_button_group = NULL;
        left_button_group = NULL;
        break;
    }

    if (s_right_enc_indev != NULL) {
        lv_indev_set_group(s_right_enc_indev, right_encoder_group);
    }
    
    if (s_left_btn_indev != NULL) {
        lv_indev_set_group(s_left_btn_indev, left_button_group);
    }

    if (s_right_btn_indev != NULL) {
        lv_indev_set_group(s_right_btn_indev, right_button_group);
    }
}

/**
 * @brief Load a screen with the given animation.
 *
 * Updates the right encoder group, triggers the LVGL screen transition, and
 * records the active screen.  Logs a warning if the screen was not created.
 *
 * @param id    Target screen.
 * @param anim  Slide animation direction.
 */
static void ui_load_screen(enum screen_id id, lv_scr_load_anim_t anim)
{
    if (id == SCREEN_NONE || id >= SCREEN_ID_COUNT || s_screens[id] == NULL) {
        LOG_WRN("Screen %d not available", (int)id);
        return;
    }

    set_encoder_group(id);
    lv_screen_load_anim(s_screens[id], anim, UI_ANIM_DURATION_MS, 0, false);
    s_active_screen = id;

    LOG_DBG("Screen transition → %d", (int)id);
}

/**
 * @brief Navigate the screen carousel by the given encoder delta.
 *
 * One step in either direction changes s_carousel_pos by ±1, clamped at
 * the carousel boundaries (no wrapping).  The animation direction mirrors
 * the physical movement:
 *
 *   delta < 0 (CCW, left)  → new screen slides in from the left
 *   delta > 0 (CW,  right) → new screen slides in from the right
 *
 * @param delta  Signed encoder step count; sign encodes direction.
 */
static void carousel_navigate(int32_t delta)
{
    int new_pos = (int)s_carousel_pos + (delta < 0 ? -1 : 1);

    /* Clamp — no wrap-around in the carousel */
    if (new_pos < 0) {
        new_pos = 0;
    } else if (new_pos >= (int)CAROUSEL_LEN) {
        new_pos = (int)CAROUSEL_LEN - 1;
    }

    if (new_pos == (int)s_carousel_pos) {
        return;  /* Already at boundary — nothing to do */
    }

    bool going_right = (new_pos > (int)s_carousel_pos);
    s_carousel_pos   = (uint8_t)new_pos;

    /*
     * Phone-launcher animation convention:
     *   Going right → current screen exits left  → LV_SCR_LOAD_ANIM_MOVE_LEFT
     *   Going left  → current screen exits right → LV_SCR_LOAD_ANIM_MOVE_RIGHT
     */
    // lv_scr_load_anim_t anim = going_right
    //     ? LV_SCR_LOAD_ANIM_MOVE_LEFT
    //     : LV_SCR_LOAD_ANIM_MOVE_RIGHT;
    lv_scr_load_anim_t anim = LV_SCREEN_LOAD_ANIM_NONE;

    ui_load_screen(k_carousel[s_carousel_pos], anim);
}

/**
 * @brief Process a single ui_cmd received from the App Layer.
 *
 * UI_CMD_SET_SCREEN  — switches to the requested screen; if the target is
 *                      in the carousel the carousel position is updated so
 *                      subsequent encoder navigation stays consistent.
 *                      For non-carousel screens (SCREEN_RTD, SCREEN_ERROR …)
 *                      a fade-in animation is used.
 *
 * UI_CMD_UPDATE_DATA — forwards a CAN data snapshot to the active screen
 *                      (RTD screen handler, not yet implemented).
 *
 * UI_CMD_SET_STATUS  — updates persistent status indicator widgets
 *                      (not yet implemented).
 */
static void handle_ui_cmd(const struct ui_cmd *cmd)
{
    switch (cmd->type) {
    case UI_CMD_SET_SCREEN: {
        enum screen_id target = cmd->data.screen;
        lv_scr_load_anim_t anim = LV_SCR_LOAD_ANIM_FADE_IN;

        /* If the target is in the carousel, use a directional slide */
        for (uint8_t i = 0U; i < (uint8_t)CAROUSEL_LEN; i++) {
            if (k_carousel[i] == target) {
                bool going_right = (i > s_carousel_pos);
                anim = going_right
                    ? LV_SCR_LOAD_ANIM_MOVE_LEFT
                    : LV_SCR_LOAD_ANIM_MOVE_RIGHT;
                s_carousel_pos = i;
                break;
            }
        }

        ui_load_screen(target, anim);
        break;
    }

    case UI_CMD_UPDATE_DATA:
        /*
         * Push the snapshot into the generated LVGL subjects.  Observers
         * (widget bindings in the screens) fire synchronously here, in the
         * LVGL thread — and only for values that actually changed.
         */
        ui_subjects_gen_update(&cmd->data.snapshot);
        break;

    case UI_CMD_SET_STATUS:
        /* TODO: update persistent status bar indicators */
        LOG_DBG("UI_CMD_SET_STATUS received (not yet implemented)");
        break;

    default:
        LOG_WRN("Unknown ui_cmd type: %d", (int)cmd->type);
        break;
    }
}

/**
 * @brief LVGL task thread entry point.
 *
 * Runs lv_timer_handler() every UI_TASK_PERIOD_MS milliseconds.  Between
 * LVGL calls, the thread:
 *   1. Drains the left encoder delta and calls carousel_navigate().
 *   2. Non-blocking polls the Zbus subscriber queue for App Layer commands.
 */
static void ui_thread_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    /*
     * Render the first frame on the 8 kB LVGL stack, then enable the display
     * backlight.  This must NOT happen in ui_module_init() (main-thread context)
     * because lv_timer_handler() with large fonts can exceed CONFIG_MAIN_STACK_SIZE
     * (2 kB) and silently corrupt adjacent memory (typically the idle thread stack).
     */
    lv_timer_handler();

    if (s_disp_dev != NULL) {
        int ret = display_blanking_off(s_disp_dev);
        if (ret < 0 && ret != -ENOSYS) {
            LOG_WRN("display_blanking_off failed: %d", ret);
        }
    }

    while (true) {
        /* ── LVGL tick ──────────────────────────────────────────────────── */
        lv_timer_handler();

        /* ── Left encoder carousel navigation ──────────────────────────── */
        int32_t delta = (int32_t)atomic_set(&s_nav_delta, 0);
        if (delta != 0) {
            carousel_navigate(delta);
        }

        /* ── Zbus commands from App Layer (non-blocking) ────────────────── */
        const struct zbus_channel *chan;
        while (zbus_sub_wait(&ui_cmd_sub, &chan, K_NO_WAIT) == 0) {
            struct ui_cmd cmd;
            if (zbus_chan_read(chan, &cmd, K_NO_WAIT) == 0) {
                handle_ui_cmd(&cmd);
            }
        }

        k_msleep(UI_TASK_PERIOD_MS);
    }
}


/* ── Public Function Implementations ─────────────────────────────────────────────────────────── */

void ui_module_init(void)
{
    /* ── 1. Shared LVGL styles ───────────────────────────────────────────── */
    ui_styles_init();

    /*
     * ── 1b. Generated LVGL subjects ─────────────────────────────────────
     * Must precede screen creation: screens bind their widgets to the
     * subjects while building.  Safe here (main thread) because the LVGL
     * task thread has not been started yet.
     */
    ui_subjects_gen_init();

    /* ── 2. Create all MVP screen objects ───────────────────────────────── */
    s_screens[SCREEN_DEBUG_LV_ACCU]  = screen_debug_lv_accu_create();
    s_screens[SCREEN_DEBUG_HV_ACCU]  = screen_debug_hv_accu_create();
    s_screens[SCREEN_DEBUG_PRESSURE] = screen_debug_pressure_create();
    s_screens[SCREEN_DEBUG_TS]       = screen_debug_tractive_system_create();
    s_screens[SCREEN_DEBUG_WRITE]    = screen_debug_write_create();
    s_screens[SCREEN_BOOT]           = screen_boot_create();
    s_screens[SCREEN_MISSION_SELECT] = screen_mission_select_create();
    s_screens[SCREEN_PRE_RTD]        = screen_checklist_create();
    s_screens[SCREEN_EV_DRIVING]     = screen_ev_driving_create();

    /* Additional screens (SCREEN_RTD, SCREEN_DEBUG …) added as implemented. */

    /* ── 3. Resolve the display device (used by the LVGL thread) ────────── */
    s_disp_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
    if (!device_is_ready(s_disp_dev)) {
        LOG_ERR("Display device not ready — backlight will not be enabled");
        s_disp_dev = NULL;
    }

    /* ── 4. Load the boot screen (no animation on cold start) ───────────── */
    s_active_screen = SCREEN_BOOT; //SCREEN_BOOT;
    s_carousel_pos  = SCREEN_BOOT;
    lv_screen_load(s_screens[s_active_screen]);

    /*
     * lv_timer_handler() and display_blanking_off() are intentionally deferred
     * to ui_thread_fn().  Calling lv_timer_handler() here (main-thread context,
     * CONFIG_MAIN_STACK_SIZE=2048) overflows the stack during large-font rendering
     * and silently corrupts adjacent kernel memory (idle thread stack).
     */

    /* ── 5. Locate the right encoder LVGL indev ─────────────────────────── */
    /*
     * Relies on Zephyr init order: lvgl_encoder0 registers first (index 0),
     * lvgl_encoder1 second (index 1).  If in-screen navigation appears on the
     * wrong encoder, swap the index here.
     */

    s_right_enc_indev = lvgl_input_get_indev(DEVICE_DT_GET(DT_ALIAS(qdec_input_right)));
    if (s_right_enc_indev == NULL) {
        LOG_WRN("Right encoder indev not found — in-screen navigation disabled");
    } else {
        LOG_DBG("Right encoder indev found");
    }

    s_left_btn_indev = lvgl_input_get_indev(DEVICE_DT_GET(DT_ALIAS(keypad_left)));
    if (s_left_btn_indev == NULL) {
        LOG_WRN("Left button indev not found — in-screen navigation disabled");
    } else {
        LOG_DBG("Left button indev found");
    }

    s_right_btn_indev = lvgl_input_get_indev(DEVICE_DT_GET(DT_ALIAS(keypad_right)));
    if (s_right_btn_indev == NULL) {
        LOG_WRN("Right button indev not found — in-screen navigation disabled");
    } else {
        LOG_DBG("Right button indev found");
    }

    /* ── 6. Start the LVGL task thread ──────────────────────────────────── */
    k_thread_create(&s_ui_thread,
                    s_ui_stack,
                    K_THREAD_STACK_SIZEOF(s_ui_stack),
                    ui_thread_fn,
                    NULL, NULL, NULL,
                    UI_THREAD_PRIORITY, 0, K_NO_WAIT);
    k_thread_name_set(&s_ui_thread, "ui_lvgl");

    LOG_INF("UI module initialised");
}
