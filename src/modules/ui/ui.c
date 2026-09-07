/**
 * @file        ui.c
 * @brief       UI module — LVGL task thread, screen carousel, input routing
 *
 * @ingroup     dcu_ui
 *
 * @details     Central coordinator for all display output.
 *
 *              ### Thread model
 *              One dedicated LVGL task thread (priority 8) calls
 *              lv_timer_handler() at UI_TASK_PERIOD_MS intervals.  All LVGL
 *              API calls (screen loads, style changes, label updates) happen
 *              inside this thread to preserve LVGL's single-thread assumption.
 *              It is the lowest-priority thread in the system: rendering must
 *              never delay CAN or the App Layer.
 *
 *              ### Screen lifecycle
 *              Screens are built on first visit by the factory in
 *              k_screen_factories[] and deleted when the next screen has been
 *              loaded, so at most one screen occupies RAM.  A screen therefore
 *              cannot hold state across visits — anything that must survive
 *              belongs in a file-scope LVGL subject, in app_state or in the
 *              settings service.
 *
 *              ### Left encoder — screen carousel
 *              INPUT_CALLBACK_DEFINE captures INPUT_REL_WHEEL events from the
 *              qdec_input_left alias in the Zephyr input workqueue context and
 *              accumulates them in atomic_t s_nav_delta.  The LVGL thread
 *              drains this delta each iteration and calls carousel_navigate()
 *              if non-zero.  This encoder is never handed to LVGL — it moves
 *              between screens, not between widgets.
 *
 *              The carousel order is k_carousel[]; the starting position is
 *              SCREEN_BOOT, found by searching that array in ui_module_init().
 *
 *              ### Right encoder and button pads — in-screen interaction
 *              Three LVGL input devices are resolved from devicetree aliases
 *              and re-pointed on every screen change (set_encoder_group()) to
 *              the groups the target screen exposes.  A screen without
 *              interactive content returns NULL, which detaches the device so
 *              LVGL ignores its events.
 *
 *              ### Zbus — three channels, polled, never blocking
 *              ui_cmd_chan (screen switches, CAN snapshots), can_status_chan
 *              (CAN icon) and vehicle_status_chan are drained with K_NO_WAIT
 *              at the end of every LVGL iteration.  Polling instead of a
 *              blocking subscriber thread keeps every LVGL call in this one
 *              thread; the cost is up to UI_TASK_PERIOD_MS of latency.
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
#include "modules/ui/screens/screen_sdc.h"
#include "modules/ui/screens/screen_checklist.h"
#include "modules/ui/screens/screen_debug_lv_accu.h"
#include "modules/ui/screens/screen_debug_hv_accu.h"
#include "modules/ui/screens/screen_debug_pressure.h"
#include "modules/ui/screens/screen_debug_tractive_system.h"
#include "modules/ui/screens/screen_debug_custom.h"
#include "modules/ui/screens/screen_settings.h"
#include "modules/ui/screens/screen_ev_driving.h"
#include "modules/ui/widgets/ui_header.h"
#include "generated/ui_subjects_gen.h"
#include "generated/ui_tx_subjects_gen.h"
#include "services/event_bus/event_bus.h"
#include "services/event_bus/events.h"

/* ── Zephyr Logging ──────────────────────────────────────────────────────────────────────────── */

LOG_MODULE_REGISTER(ui_module, CONFIG_LOG_DEFAULT_LEVEL);


/* ── Private Macros & Constants ──────────────────────────────────────────────────────────────── */

/** @brief LVGL task period in milliseconds. Also bounds the Zbus command latency. */
#define UI_TASK_PERIOD_MS       5U

/** @brief Screen transition animation duration in milliseconds. 0 = instant. */
#define UI_ANIM_DURATION_MS     0U

/**
 * @brief Stack size for the LVGL task thread.
 *
 * Large by embedded standards, and deliberately so: rendering the 80 pt and
 * 100 pt fonts is what drives the requirement. This is also why the first
 * lv_timer_handler() call was moved out of ui_module_init() — see there.
 */
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
 * Index 0 is the leftmost screen; turning the encoder counter-clockwise moves
 * towards lower indices, clockwise towards higher ones. There is no wrap-around
 * — the ends are hard stops, so the driver can reach an edge screen blindly.
 *
 * The list is a subset of enum screen_id: a screen may exist and be reachable
 * only through UI_CMD_SET_SCREEN. Every entry must have a factory below.
 *
 * SCREEN_EV_DRIVING is deliberately absent: it is entered only when the state
 * machine latches RTD, and while it is active the left encoder drives the TQG
 * Front slider instead of this carousel (see set_encoder_group() and the
 * s_left_encoder_claimed check in ui_thread_fn()). That is the carousel lock.
 */
static const enum screen_id k_carousel[] = {
    SCREEN_DEBUG_LV_ACCU,   /* index 0 — leftmost                       */
    SCREEN_DEBUG_HV_ACCU,   /* index 1                                  */
    SCREEN_DEBUG_PRESSURE,  /* index 2                                  */
    SCREEN_DEBUG_TS,        /* index 3                                  */
    SCREEN_DEBUG_CUSTOM,    /* index 4                                  */
    SCREEN_SETTINGS,        /* index 5                                  */
    SCREEN_BOOT,            /* index 6 — start position after boot      */
    SCREEN_MISSION_SELECT,  /* index 7                                  */
    SCREEN_SDC,             /* index 8                                  */
    SCREEN_PRE_RTD,         /* index 9 — rightmost; carries the RTD button */
};

/** @brief Number of screens in the carousel. */
#define CAROUSEL_LEN    ARRAY_SIZE(k_carousel)


/* ── Screen factory ──────────────────────────────────────────────────────────────────────────── */

/**
 * @brief Signature every screen builder shares.
 *
 * Takes the device-status subject array that the header widget binds to — every
 * screen passes it straight to ui_header_create() — and returns the newly
 * created top-level screen object.
 */
typedef lv_obj_t *(*screen_factory_fn)(lv_subject_t *);

/**
 * @brief Builder per screen_id; NULL means "screen not implemented".
 *
 * Sparse by design — the designated initialisers keep the table aligned with
 * enum screen_id no matter how the enum is reordered, and ui_load_screen()
 * treats a NULL entry as a no-op with a warning.
 */
static const screen_factory_fn k_screen_factories[SCREEN_ID_COUNT] = {
    [SCREEN_DEBUG_LV_ACCU]  = screen_debug_lv_accu_create,
    [SCREEN_DEBUG_HV_ACCU]  = screen_debug_hv_accu_create,
    [SCREEN_DEBUG_PRESSURE] = screen_debug_pressure_create,
    [SCREEN_DEBUG_TS]       = screen_debug_tractive_system_create,
    [SCREEN_DEBUG_CUSTOM]    = screen_debug_custom_create,
    [SCREEN_SETTINGS]       = screen_settings_create,
    [SCREEN_BOOT]           = screen_boot_create,
    [SCREEN_MISSION_SELECT] = screen_mission_select_create,
    [SCREEN_SDC]            = screen_sdc_create,
    [SCREEN_PRE_RTD]        = screen_checklist_create,
    [SCREEN_EV_DRIVING]     = screen_ev_driving_create,
};


/* ── Private Variables ───────────────────────────────────────────────────────────────────────── */

/**
 * @brief Screen object cache, indexed by enum screen_id.
 *
 * NULL means the screen has not been created yet (or was destroyed after
 * navigation).  Created on first visit, destroyed on departure.
 */
static lv_obj_t *s_screens[SCREEN_ID_COUNT];

/** @brief Currently active screen identifier. */
static enum screen_id s_active_screen = SCREEN_NONE;

/**
 * @brief Current index into k_carousel[].
 *
 * @note The initialiser is an index-versus-enum mix-up without consequence:
 *       SCREEN_BOOT is a screen_id, not a carousel index, and the two do not
 *       coincide. It never matters, because ui_module_init() searches
 *       k_carousel[] for SCREEN_BOOT and overwrites this with the real index
 *       before anything reads it.
 */
static uint8_t s_carousel_pos = SCREEN_BOOT;    /* starts at SCREEN_BOOT */

/**
 * @brief Accumulated left encoder delta from the input callback.
 *
 * Written by left_encoder_cb() (input workqueue context) via atomic_add().
 * Read and cleared by the LVGL thread via atomic_set(&s_nav_delta, 0).
 */
static atomic_t s_nav_delta;

/*
 * The LVGL input devices routed to the active screen.
 *
 * Each is resolved once in ui_module_init() and re-pointed at a new group on
 * every screen transition (set_encoder_group()). NULL means the device was not
 * found; routing then skips it and that input is simply inactive.
 *
 * The left encoder is a special case: for a carousel screen it has no LVGL
 * group and its rotation is handled by left_encoder_cb() below (screen
 * navigation). A screen can instead claim it for a widget by returning a
 * group from its own accessor — today only the EV driving screen does, for the
 * TQG Front slider — and then the carousel is not fed (see
 * s_left_encoder_claimed).
 */

/** @brief Right encoder (alias qdec_input_right) — moves within a screen. */
static lv_indev_t *s_right_enc_indev;

/** @brief Left encoder as an LVGL indev (node lvgl_encoder0) — TQG Front on EV driving. */
static lv_indev_t *s_left_enc_indev;

/** @brief Left button pad (alias keypad_left). */
static lv_indev_t *s_left_btn_indev;

/** @brief Right button pad (alias keypad_right). */
static lv_indev_t *s_right_btn_indev;

/** @brief Dedicated RTD button pad (alias keypad_rtd) — routed to PRE_RTD only. */
static lv_indev_t *s_rtd_btn_indev;

/**
 * @brief True while the active screen has claimed the left encoder for a widget.
 *
 * Written by set_encoder_group(), read by ui_thread_fn(). When true the
 * accumulated left-encoder delta is dropped instead of driving the carousel —
 * LVGL moves the claimed widget through s_left_enc_indev's group instead.
 */
static bool s_left_encoder_claimed;

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

/**
 * @brief One LVGL subject per device status slot; the header icons observe them.
 *
 * File-scope on purpose: the subjects outlive the screens. A screen passes the
 * array to ui_header_create() while building, LVGL drops the observers when
 * the screen is deleted, and the values survive for the next screen.
 */
static lv_subject_t s_device_status[UI_DEVICE_SLOT_COUNT];



/* ── Zbus Subscriber ─────────────────────────────────────────────────────────────────────────── */

/** @brief Subscriber for ui_cmd_chan (App Layer → UI module). */
ZBUS_SUBSCRIBER_DEFINE(ui_cmd_sub, 4);
ZBUS_CHAN_ADD_OBS(ui_cmd_chan, ui_cmd_sub, 0);

/** @brief Subscriber for vehicle_status_chan. Reserved — nothing publishes there. */
ZBUS_SUBSCRIBER_DEFINE(vehicle_status_sub, 4);
ZBUS_CHAN_ADD_OBS(vehicle_status_chan, vehicle_status_sub, 0);

/** @brief Subscriber for can_status_chan (CAN module → all). */
ZBUS_SUBSCRIBER_DEFINE(can_status_sub, 2);
ZBUS_CHAN_ADD_OBS(can_status_chan, can_status_sub, 0);


/* ── Left Encoder Input Callback ─────────────────────────────────────────────────────────────── */

/**
 * @brief Capture left encoder rotation events for carousel navigation.
 *
 * Runs in the Zephyr input workqueue context — LVGL API must not be called
 * here.  The delta is accumulated atomically and drained by the LVGL thread,
 * which is the whole reason for the atomic: this callback and the LVGL thread
 * touch s_nav_delta concurrently and share no lock.
 *
 * The sign is inverted here, so that after this point:
 *
 *   Positive accumulated delta → navigate right in the carousel
 *   Negative accumulated delta → navigate left
 *
 * Which physical direction that is comes from how the encoder counts, and
 * inverting at the input boundary is what keeps that a property of the device:
 * carousel_navigate() only ever sees a direction, never a raw reading. Flip
 * this one sign to swap clockwise and counter-clockwise; nothing downstream
 * needs to know.
 *
 * @param evt        Input event; only INPUT_EV_REL / INPUT_REL_WHEEL is used.
 * @param user_data  Unused.
 */
static void left_encoder_cb(struct input_event *evt, void *user_data)
{
    ARG_UNUSED(user_data);

    if (evt->type == INPUT_EV_REL && evt->code == INPUT_REL_WHEEL) {
        atomic_add(&s_nav_delta, (atomic_val_t)evt->value);
    }
}

/*
 * Register the callback for the left encoder only.  Events from the right
 * encoder and the keypad devices go to LVGL and never reach this callback.
 */
INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(DT_ALIAS(qdec_input_left)), left_encoder_cb, NULL);


/* ── Private Function Prototypes ─────────────────────────────────────────────────────────────── */

static lv_indev_t *get_encoder_indev(uint8_t index);
static void        set_encoder_group(enum screen_id id);
static void        ui_load_screen(enum screen_id id, lv_scr_load_anim_t anim);
static void        carousel_navigate(int32_t delta);
static void        handle_ui_cmd(const struct ui_cmd *cmd);
static void        handle_vehicle_status(const struct vehicle_status *status);
static void        handle_can_status(const struct can_status_event *evt);
static void        ui_thread_fn(void *p1, void *p2, void *p3);


/* ── Private Function Implementations ───────────────────────────────────────────────────────── */

/**
 * @brief Return the Nth LVGL encoder indev (0-indexed).
 *
 * Walks the registered input devices and counts the encoder-type ones, so the
 * index refers to registration order rather than to a devicetree node.
 *
 * @note Superseded and unused. ui_module_init() resolves the input devices
 *       with lvgl_input_get_indev() from their devicetree aliases instead,
 *       which does not depend on Zephyr's init order. Kept as a fallback for
 *       a board whose aliases are missing.
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
 * @brief Point every routable input device at the target screen's groups.
 *
 * Interactive screens expose group accessors; screens without interactive
 * content have none and fall through to the default branch, where every group
 * stays NULL. Assigning NULL detaches the device, so LVGL discards its events
 * instead of delivering them to the previous screen's widgets — which by then
 * are about to be deleted.
 *
 * Two devices are only ever claimed by a single screen: the dedicated RTD
 * button pad (PRE_RTD) and the left encoder (EV_DRIVING, for the TQG Front
 * slider). A non-NULL left-encoder group also flips s_left_encoder_claimed,
 * which is what stops the carousel from moving on that screen.
 *
 * Called before the screen load, so focus is already correct when the new
 * screen appears.
 *
 * @param id  The screen that is about to become active.
 */
static void set_encoder_group(enum screen_id id)
{
    lv_group_t *right_encoder_group = NULL;
    lv_group_t *right_button_group = NULL;
    lv_group_t *left_button_group = NULL;
    lv_group_t *left_encoder_group = NULL;
    lv_group_t *rtd_button_group = NULL;

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
        rtd_button_group = screen_checklist_get_rtd_button_group();
        break;
    case SCREEN_SDC:
        right_encoder_group = screen_sdc_get_right_encoder_group();
        left_button_group = screen_sdc_get_left_button_group();
        right_button_group = screen_sdc_get_right_button_group();
        break;
    case SCREEN_DEBUG_CUSTOM:
        right_encoder_group = screen_debug_custom_get_right_encoder_group();
        left_button_group = screen_debug_custom_get_left_button_group();
        right_button_group = screen_debug_custom_get_right_button_group();
        break;
    case SCREEN_SETTINGS:
        right_encoder_group = screen_settings_get_right_encoder_group();
        left_button_group = screen_settings_get_left_button_group();
        right_button_group = screen_settings_get_right_button_group();
        break;

    case SCREEN_EV_DRIVING:
        right_encoder_group = screen_ev_driving_get_right_encoder_group();
        left_button_group = screen_ev_driving_get_left_button_group();
        right_button_group = screen_ev_driving_get_right_button_group();
        left_encoder_group = screen_ev_driving_get_left_encoder_group();
        break;

    default:
        right_encoder_group = NULL;
        right_button_group = NULL;
        left_button_group = NULL;
        left_encoder_group = NULL;
        rtd_button_group = NULL;
        break;
    }

    if (s_right_enc_indev != NULL) {
        lv_indev_set_group(s_right_enc_indev, right_encoder_group);
    }

    if (s_left_enc_indev != NULL) {
        lv_indev_set_group(s_left_enc_indev, left_encoder_group);
    }

    if (s_left_btn_indev != NULL) {
        lv_indev_set_group(s_left_btn_indev, left_button_group);
    }

    if (s_right_btn_indev != NULL) {
        lv_indev_set_group(s_right_btn_indev, right_button_group);
    }

    if (s_rtd_btn_indev != NULL) {
        lv_indev_set_group(s_rtd_btn_indev, rtd_button_group);
    }

    s_left_encoder_claimed = (left_encoder_group != NULL);
}

/**
 * @brief Build if necessary, then load a screen, and release the previous one.
 *
 * The single entry point for every screen change — both carousel navigation
 * and UI_CMD_SET_SCREEN go through here, so the create/load/delete sequence
 * exists in exactly one place.
 *
 * Order matters: the previous screen is deleted only *after* the new one has
 * been loaded, and via lv_obj_delete_async() so the deletion happens once LVGL
 * is out of the current event dispatch and no longer holds a reference.
 *
 * Does nothing but log if @p id is out of range or has no factory.
 *
 * @param id    Target screen.
 * @param anim  LVGL screen-load animation to use.
 */
static void ui_load_screen(enum screen_id id, lv_scr_load_anim_t anim)
{
    if (id == SCREEN_NONE || id >= SCREEN_ID_COUNT) {
        LOG_WRN("Screen %d out of range", (int)id);
        return;
    }

    if (s_screens[id] == NULL) {
        if (k_screen_factories[id] == NULL) {
            LOG_WRN("Screen %d not implemented", (int)id);
            return;
        }
        s_screens[id] = k_screen_factories[id](s_device_status);
    }

    enum screen_id prev = s_active_screen;

    set_encoder_group(id);
    lv_screen_load_anim(s_screens[id], anim, UI_ANIM_DURATION_MS, 0, false);
    s_active_screen = id;

    if (prev != SCREEN_NONE && prev != id && s_screens[prev] != NULL) {
        lv_obj_delete_async(s_screens[prev]);
        s_screens[prev] = NULL;
    }

    /*
     * Tell the App Layer where the driver ended up. It is the only writer of
     * app_state, and the lighting module reads the active screen from there to
     * decide what to put on the strip — the UI never talks to it directly.
     */
    struct ui_input_event evt = {
        .type        = UI_INPUT_SCREEN_CHANGED,
        .data.screen = id,
    };
    int rc = zbus_chan_pub(&ui_input_chan, &evt, K_NO_WAIT);
    if (rc != 0) {
        LOG_WRN("UI_INPUT_SCREEN_CHANGED publish failed: %d", rc);
    }

    LOG_DBG("Screen transition → %d", (int)id);
}

/**
 * @brief Navigate the screen carousel by the given encoder delta.
 *
 * Only the sign of @p delta is used: several accumulated detents still move
 * one screen. That is intentional — a fast turn should not skip past several
 * screens, each of which would be built and immediately destroyed.
 *
 * The position is clamped at both ends; there is no wrap-around.
 *
 * Transitions are currently instant (LV_SCREEN_LOAD_ANIM_NONE). The
 * directional slide the commented-out lines describe follows the
 * phone-launcher convention and can be re-enabled there.
 *
 * @param delta  Signed step count; the sign is a navigation direction, not a
 *               raw encoder reading — negative moves left in the carousel,
 *               positive moves right. left_encoder_cb() has already mapped
 *               the physical rotation onto it.
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
 * @brief Derive per-device icon status from the latest CAN snapshot.
 *
 * Called on every UI_CMD_UPDATE_DATA. Writes the five slots that can be
 * derived from CAN signals; UI_DEVICE_CAN comes from handle_can_status()
 * instead, and UI_DEVICE_MABX has no source and keeps its initial FAULT.
 *
 * Writing unconditionally is cheap: lv_subject_set_int() only notifies
 * observers when the value actually changes.
 *
 * @param snap  Snapshot received from the App Layer.
 */
static void update_device_status(const struct can_data_snapshot *snap)
{
    /* KISTLER: timeout flag clear → frames arriving → OK */
    lv_subject_set_int(&s_device_status[UI_DEVICE_KISTLER],
        snap->kistler_timeout ? UI_DEVICE_STATUS_FAULT : UI_DEVICE_STATUS_OK);

    /*
     * SDCS: every node has to report closed for the chain to be intact.
     *
     * Each SDC signal is 1 while its own contact is closed, so the healthy
     * state is all bits set — one node dropping to 0 breaks the circuit and
     * takes the icon to FAULT. Same polarity as the SDC screen; the two must
     * not disagree.
     */
    bool sdc_all_closed = snap->sdc_bspd     && snap->sdc_cockpit  &&
                          snap->sdc_ascu     && snap->sdc_motor_rl &&
                          snap->sdc_hvd      && snap->sdc_motor_rr &&
                          snap->sdc_sdb_mh   && snap->sdc_res      &&
                          snap->sdc_motor_fl && snap->sdc_bots     &&
                          snap->sdc_motor_fr && snap->sdc_inertia;
    lv_subject_set_int(&s_device_status[UI_DEVICE_SDCS],
        sdc_all_closed ? UI_DEVICE_STATUS_OK : UI_DEVICE_STATUS_FAULT);

    /* DV_PC: actively receiving mission data → OK */
    lv_subject_set_int(&s_device_status[UI_DEVICE_DV_PC],
        snap->dv_receiving ? UI_DEVICE_STATUS_OK : UI_DEVICE_STATUS_FAULT);

    /* ROS: autonomy stack ready → OK */
    lv_subject_set_int(&s_device_status[UI_DEVICE_ROS],
        snap->dv_ready ? UI_DEVICE_STATUS_OK : UI_DEVICE_STATUS_FAULT);

    /* LOGGER: recording → ACTIVE (blinking), ready → OK, else FAULT */
    enum ui_device_status logger_status;
    if (snap->datalogger_recording) {
        logger_status = UI_DEVICE_STATUS_ACTIVE;
    } else if (snap->datalogger_ready) {
        logger_status = UI_DEVICE_STATUS_OK;
    } else {
        logger_status = UI_DEVICE_STATUS_FAULT;
    }
    lv_subject_set_int(&s_device_status[UI_DEVICE_LOGGER], logger_status);
}

/**
 * @brief Process a single ui_cmd received from the App Layer.
 *
 * UI_CMD_SET_SCREEN  — switches to the requested screen. If the target is in
 *                      the carousel, the carousel position is moved with it,
 *                      so the next encoder step continues from where the
 *                      driver now is rather than from where they last turned.
 *
 * UI_CMD_UPDATE_DATA — pushes a CAN snapshot into the generated LVGL subjects
 *                      and re-derives the header status icons. Bound widgets
 *                      update themselves; nothing here knows which screen is
 *                      on the display.
 *
 * @param cmd  Command read from ui_cmd_chan.
 */
static void handle_ui_cmd(const struct ui_cmd *cmd)
{
    switch (cmd->type) {
    case UI_CMD_SET_SCREEN: {
        enum screen_id target = cmd->data.screen;
        lv_scr_load_anim_t anim = LV_SCR_LOAD_ANIM_NONE;

        for (uint8_t i = 0U; i < (uint8_t)CAROUSEL_LEN; i++) {
            if (k_carousel[i] == target) {
                s_carousel_pos = i;
                break;
            }
        }

        ui_load_screen(target, anim);
        break;
    }

    case UI_CMD_UPDATE_DATA:
        ui_subjects_gen_update(&cmd->data.snapshot);
        update_device_status(&cmd->data.snapshot);
        break;

    default:
        LOG_WRN("Unknown ui_cmd type: %d", (int)cmd->type);
        break;
    }
}

/**
 * @brief Handle a vehicle_status_chan update — apply a full slot array at once.
 *
 * Header icons on the active screen react automatically through their
 * observers.
 *
 * @note Never called: no module publishes on vehicle_status_chan. The status
 *       subjects are fed by update_device_status() and handle_can_status()
 *       instead. Note also that this would overwrite all seven slots,
 *       including the two those functions own.
 *
 * @param status  Full set of slot states.
 */
static void handle_vehicle_status(const struct vehicle_status *status)
{
    LOG_WRN("New vehicle status");
    for (int i = 0; i < UI_DEVICE_SLOT_COUNT; i++) {
        lv_subject_set_int(&s_device_status[i], (int32_t)status->slots[i]);
    }
}

/**
 * @brief Map a CAN bus state event to the UI_DEVICE_CAN icon status.
 *
 * CAN_BUS_STATE_ERROR_ACTIVE  → OK     (normal operation)
 * CAN_BUS_STATE_ERROR_WARNING → WARN   (error counters elevated)
 * CAN_BUS_STATE_ERROR_PASSIVE → FAULT  (error-passive, TX limited)
 * CAN_BUS_STATE_BUS_OFF       → ACTIVE (controller silent — blinking)
 * CAN_BUS_STATE_STOPPED       → ACTIVE (controller not started — blinking)
 *
 * The two worst states map to ACTIVE rather than FAULT on purpose: ACTIVE
 * blinks, and a bus the DCU has dropped off entirely should be the one icon
 * that moves.
 *
 * Only evt->state is read; evt->type carries no additional information on this
 * path (see the note in can.c).
 *
 * @param evt  Event read from can_status_chan.
 */
static void handle_can_status(const struct can_status_event *evt)
{
    enum ui_device_status s;

    switch (evt->state) {
    case CAN_BUS_STATE_ERROR_ACTIVE:  s = UI_DEVICE_STATUS_OK;     break;
    case CAN_BUS_STATE_ERROR_WARNING: s = UI_DEVICE_STATUS_WARN;   break;
    case CAN_BUS_STATE_ERROR_PASSIVE: s = UI_DEVICE_STATUS_FAULT;  break;
    case CAN_BUS_STATE_BUS_OFF:       /* fall through */
    case CAN_BUS_STATE_STOPPED:       s = UI_DEVICE_STATUS_ACTIVE; break;
    default:                          s = UI_DEVICE_STATUS_FAULT;  break;
    }

    lv_subject_set_int(&s_device_status[UI_DEVICE_CAN], (int32_t)s);
}

/**
 * @brief LVGL task thread entry point.
 *
 * Renders the first frame and enables the backlight, then loops:
 *   1. lv_timer_handler() — LVGL's own timers, animations and rendering.
 *   2. Drain the left encoder delta and navigate the carousel.
 *   3. Drain all three Zbus subscriber queues with K_NO_WAIT.
 *   4. Sleep UI_TASK_PERIOD_MS.
 *
 * Everything the UI does happens here, which is what keeps LVGL's
 * single-thread requirement satisfied without a single lock.
 *
 * @param p1  Unused.
 * @param p2  Unused.
 * @param p3  Unused.
 */
static void ui_thread_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    /*
     * Render the first frame on this thread's stack, then enable the display
     * backlight — in that order, so the panel never shows an unpainted frame.
     *
     * This must NOT happen in ui_module_init() (main-thread context) because
     * lv_timer_handler() with large fonts can exceed CONFIG_MAIN_STACK_SIZE
     * (2 kB) and silently corrupt adjacent memory (typically the idle thread
     * stack).  The symptom is a crash somewhere else entirely, which is why
     * the split is worth keeping.
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

        /* ── Left encoder: carousel, unless the active screen claimed it ── */
        int32_t delta = (int32_t)atomic_set(&s_nav_delta, 0);
        if (delta != 0 && !s_left_encoder_claimed) {
            carousel_navigate(delta);
        }
        /*
         * When claimed (EV driving), the delta is read and dropped here;
         * LVGL moves the TQG Front slider through s_left_enc_indev's group.
         */

        /* ── Zbus commands from App Layer (non-blocking) ────────────────── */
        const struct zbus_channel *chan;
        while (zbus_sub_wait(&ui_cmd_sub, &chan, K_NO_WAIT) == 0) {
            struct ui_cmd cmd;
            if (zbus_chan_read(chan, &cmd, K_NO_WAIT) == 0) {
                handle_ui_cmd(&cmd);
            }
        }

        /* ── Vehicle status updates (non-blocking) ───────────────────────── */
        while (zbus_sub_wait(&vehicle_status_sub, &chan, K_NO_WAIT) == 0) {
            struct vehicle_status status;
            if (zbus_chan_read(chan, &status, K_NO_WAIT) == 0) {
                handle_vehicle_status(&status);
            }
        }

        /* ── CAN bus state updates (non-blocking) ────────────────────────── */
        while (zbus_sub_wait(&can_status_sub, &chan, K_NO_WAIT) == 0) {
            struct can_status_event evt;
            if (zbus_chan_read(chan, &evt, K_NO_WAIT) == 0) {
                handle_can_status(&evt);
            }
        }

        k_msleep(UI_TASK_PERIOD_MS);
    }
}


/* ── Public Function Implementations ─────────────────────────────────────────────────────────── */

uint8_t ui_carousel_get_length(void)
{
    return (uint8_t)CAROUSEL_LEN;
}

uint8_t ui_carousel_get_position(void)
{
    return s_carousel_pos;
}

void ui_module_init(void)
{
    /* ── 1. Shared LVGL styles ───────────────────────────────────────────── */
    ui_styles_init();

    /*
     * ── 1a. Generated LVGL subjects ─────────────────────────────────────
     * Must precede screen creation: screens bind their widgets to the
     * subjects while building.  Safe here (main thread) because the LVGL
     * task thread has not been started yet.
     */
    ui_subjects_gen_init();
    ui_tx_subjects_gen_init();

    /*
     * ── 1b. Device status subjects ───────────────────────────────────────
     * Must also precede screen creation: header icons subscribe to these
     * subjects during ui_header_create() and expect them to be initialised.
     *
     * All slots start at FAULT, not OK. Nothing is known about any device
     * until its first CAN frame arrives, and on a status display the honest
     * rendering of "unknown" is the pessimistic one: a green icon that has
     * never been confirmed invites the driver to trust a device that may not
     * even be powered. Red resolving to green as the bus comes up is a
     * start-up sequence anyone can read; green that silently stays green is
     * indistinguishable from a working system.
     *
     * The slots that have a source clear themselves within a cycle or two —
     * see update_device_status() and handle_can_status(). UI_DEVICE_MABX has
     * no source at all and therefore stays red for now.
     */
    for (int i = 0; i < UI_DEVICE_SLOT_COUNT; i++) {
        lv_subject_init_int(&s_device_status[i], (int32_t)UI_DEVICE_STATUS_FAULT);
    }

    /* ── 2. Resolve the display device (used by the LVGL thread) ────────── */
    s_disp_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
    if (!device_is_ready(s_disp_dev)) {
        LOG_ERR("Display device not ready — backlight will not be enabled");
        s_disp_dev = NULL;
    }

    /* ── 3. Load the boot screen (no animation on cold start) ───────────── */

    /*
     * Find SCREEN_BOOT in the carousel rather than hard-coding its index, so
     * reordering k_carousel[] cannot leave the start position pointing at a
     * different screen.  Index 0 is the fallback if it is not in the list.
     */
    s_carousel_pos = 0;
    for (uint8_t i = 0; i < (uint8_t)CAROUSEL_LEN; i++) {
        if (k_carousel[i] == SCREEN_BOOT) {
            s_carousel_pos = i;
            break;
        }
    }
    ui_load_screen(SCREEN_BOOT, LV_SCR_LOAD_ANIM_NONE);

    /*
     * lv_timer_handler() and display_blanking_off() are intentionally deferred
     * to ui_thread_fn().  Calling lv_timer_handler() here (main-thread context,
     * CONFIG_MAIN_STACK_SIZE=2048) overflows the stack during large-font rendering
     * and silently corrupts adjacent kernel memory (idle thread stack).
     */

    /* ── 4. Resolve the LVGL input devices ──────────────────────────────── */
    /*
     * Looked up by devicetree alias (or node label, where no alias exists), so
     * the mapping is defined by the shield and not by Zephyr's init order.  A
     * missing device is a warning, not a failure: the display keeps working,
     * that one input does not.
     */

    s_right_enc_indev = lvgl_input_get_indev(DEVICE_DT_GET(DT_ALIAS(qdec_input_right)));
    if (s_right_enc_indev == NULL) {
        LOG_WRN("Right encoder indev not found — in-screen navigation disabled");
    } else {
        LOG_DBG("Right encoder indev found");
    }

    /*
     * Left encoder: the qdec_input_left alias points at the raw device that
     * left_encoder_cb() hooks for the carousel; the LVGL wrapper for the same
     * physical encoder is the lvgl_encoder0 node, used only when a screen
     * claims the left encoder for a widget.
     */
    s_left_enc_indev = lvgl_input_get_indev(DEVICE_DT_GET(DT_NODELABEL(lvgl_encoder0)));
    if (s_left_enc_indev == NULL) {
        LOG_WRN("Left encoder indev not found — TQG Front control disabled");
    } else {
        LOG_DBG("Left encoder indev found");
    }

    s_rtd_btn_indev = lvgl_input_get_indev(DEVICE_DT_GET(DT_ALIAS(keypad_rtd)));
    if (s_rtd_btn_indev == NULL) {
        LOG_WRN("RTD button indev not found — RTD request disabled");
    } else {
        LOG_DBG("RTD button indev found");
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

    /* ── 5. Start the LVGL task thread ──────────────────────────────────── */
    k_thread_create(&s_ui_thread,
                    s_ui_stack,
                    K_THREAD_STACK_SIZEOF(s_ui_stack),
                    ui_thread_fn,
                    NULL, NULL, NULL,
                    UI_THREAD_PRIORITY, 0, K_NO_WAIT);
    k_thread_name_set(&s_ui_thread, "ui_lvgl");

    LOG_INF("UI module initialised");
}
