/**
 * @file        app.c
 * @brief       App Layer — Dirigent coordinator between all modules
 *
 * @details     The App Layer is the single source of truth for the application
 *              state and the exclusive writer of app_state.  It coordinates all
 *              modules by reacting to upward Zbus events and issuing downward
 *              commands.
 *
 *              Threading
 *              ─────────
 *              A single app thread (priority 5, stack 2 kB) blocks on
 *              zbus_sub_wait() forever.  On each notification it reads the
 *              channel payload and dispatches to the appropriate handler.
 *
 *              Upward channels consumed (Module → App)
 *              ────────────────────────────────────────
 *              ui_input_chan   — driver interactions (mission selection, RTD, …)
 *              can_status_chan — CAN bus connectivity and error state
 *              can_data_chan   — decoded CAN signal snapshots
 *              safety_chan     — safety-critical signal changes
 *              settings_chan   — settings load / update events  (TODO)
 *              feedback_chan   — effect completion feedback      (TODO)
 *
 *              Downward channels produced (App → Module)
 *              ──────────────────────────────────────────
 *              can_tx_cmd_chan — CAN frame transmission commands
 *              ui_cmd_chan     — screen navigation and data push commands
 *
 *              MVP operating mode transitions
 *              ──────────────────────────────
 *              DEBUG  →  (RTD button pressed)  →  RTD
 *              Full state-machine (PRE_RTD, POST_RTD, error handling) is
 *              deferred to a later implementation phase.
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

#include "app/app.h"

/* ── Zephyr Includes ─────────────────────────────────────────────────────────────────────────── */

#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/logging/log.h>

/* ── Project Includes ────────────────────────────────────────────────────────────────────────── */

#include "app/app_state.h"
#include "services/event_bus/event_bus.h"
#include "services/event_bus/events.h"

/* ── Zephyr Logging ──────────────────────────────────────────────────────────────────────────── */

LOG_MODULE_REGISTER(app, CONFIG_LOG_DEFAULT_LEVEL);


/* ── Private Macros & Constants ──────────────────────────────────────────────────────────────── */

/** @brief Stack size for the App thread. */
#define APP_THREAD_STACK_SIZE   2048U

/** @brief Scheduling priority for the App thread. */
#define APP_THREAD_PRIORITY     5


/* ── Private Variables ───────────────────────────────────────────────────────────────────────── */

/** @brief Thread control block for the App thread. */
static struct k_thread s_app_thread;

/** @brief Last rtd_sound value — detect transitions to avoid redundant publishes. */
static bool s_last_rtd_sound;

/** @brief Stack storage for the App thread. */
static K_THREAD_STACK_DEFINE(s_app_stack, APP_THREAD_STACK_SIZE);


/* ── Zbus Subscriber ─────────────────────────────────────────────────────────────────────────── */

/**
 * @brief Single subscriber for all upward Zbus channels.
 *
 * Queue depth 8 handles bursts of events (e.g., rapid CAN data + status
 * updates) without dropping notifications.
 */
ZBUS_SUBSCRIBER_DEFINE(app_sub, 8);

ZBUS_CHAN_ADD_OBS(ui_input_chan,   app_sub, 0);
ZBUS_CHAN_ADD_OBS(can_status_chan, app_sub, 0);
ZBUS_CHAN_ADD_OBS(can_data_chan,   app_sub, 0);
ZBUS_CHAN_ADD_OBS(safety_chan,     app_sub, 0);
ZBUS_CHAN_ADD_OBS(settings_chan,   app_sub, 0);
ZBUS_CHAN_ADD_OBS(feedback_chan,   app_sub, 0);


/* ── Private Function Prototypes ─────────────────────────────────────────────────────────────── */

static void pub_ui_cmd(const struct ui_cmd *cmd);
static void handle_ui_input(const struct ui_input_event *evt);
static void handle_can_status(const struct can_status_event *evt);
static void handle_can_data(const struct can_data_snapshot *snap);
static void handle_safety(const struct safety_event *evt);
static void app_thread_fn(void *p1, void *p2, void *p3);


/* ── Private Function Implementations ───────────────────────────────────────────────────────── */

/**
 * @brief Publish a command to ui_cmd_chan.
 * Logs an error if the channel is full.
 */
static void pub_ui_cmd(const struct ui_cmd *cmd)
{
    int ret = zbus_chan_pub(&ui_cmd_chan, cmd, K_NO_WAIT);
    if (ret != 0) {
        LOG_ERR("ui_cmd_chan publish failed: %d", ret);
    }
}

/* ──────────────────────────────────────────────────────────────────────────────────────────────
 * UI input handler
 * ────────────────────────────────────────────────────────────────────────────────────────────── */

/**
 * @brief Handle a UI input event from the driver.
 *
 * UI_INPUT_MISSION_SELECTED
 *   Records the selected mission in app_state and transmits it over CAN.
 *   The mission is not yet locked — the driver can change it again before
 *   pressing RTD.
 *
 * UI_INPUT_RTD_REQUEST
 *   Locks the current mission, sets operating mode to RTD, transmits the
 *   RTD CAN frame, and navigates to SCREEN_RTD.  If SCREEN_RTD is not yet
 *   implemented, ui.c logs a warning and stays on the current screen.
 *
 * UI_INPUT_BACK
 *   Navigates back to the boot screen.  The left-encoder carousel is handled
 *   inside ui.c; this handles the physical ESC button.
 *
 * UI_INPUT_TIMESTAMP
 *   Logs the current Zephyr uptime as an event marker.  Useful for
 *   synchronising external measurements with the firmware timeline.
 */
static void handle_ui_input(const struct ui_input_event *evt)
{
    LOG_INF("Handle UI Input Event");

    switch (evt->type) {

    case UI_INPUT_MISSION_SELECTED: {
        enum mission_id mission = evt->data.mission;

        struct app_state_mission state = {
            .selected = mission,
            .active   = false,
            .locked   = false,
        };
        app_state_set_mission(&state);

        /*
         * No explicit CAN command needed: the CAN module reads
         * app_state_get_selected_mission() directly on every 100 ms cycle.
         */
        LOG_INF("Mission selected: %d", (int)mission);
        break;
    }

    case UI_INPUT_RTD_REQUEST: {
        /* Lock the mission so the driver cannot change it mid-run */
        struct app_state_mission mission;
        app_state_get_mission(&mission);
        mission.active = true;
        mission.locked = true;
        app_state_set_mission(&mission);

        /*
         * Setting the mode to RTD is sufficient: the CAN module checks
         * app_state_get_mode() == OPERATING_MODE_RTD each cycle and sets
         * RTD_Button = 1 automatically.
         */
        app_state_set_mode(OPERATING_MODE_RTD);

        /* Navigate to the RTD screen — ui.c logs a warning if not yet built */
        struct ui_cmd ui_nav = {
            .type        = UI_CMD_SET_SCREEN,
            .data.screen = SCREEN_RTD,
        };
        pub_ui_cmd(&ui_nav);

        LOG_INF("RTD request sent — operating mode: RTD, mission: %d",
                (int)mission.selected);
        break;
    }

    case UI_INPUT_BACK: {
        /* ESC button: return to boot screen unconditionally */
        struct ui_cmd cmd = {
            .type        = UI_CMD_SET_SCREEN,
            .data.screen = SCREEN_BOOT,
        };
        pub_ui_cmd(&cmd);

        LOG_DBG("Back → SCREEN_BOOT");
        break;
    }

    case UI_INPUT_DEBUG_BITS_SELECTED: {
        uint8_t bits = evt->data.debug_bits;
        app_state_set_debug_bits(bits);
        LOG_INF("Debug bits set: %u (0x%02X)", (unsigned)bits, (unsigned)bits);
        break;
    }

    case UI_INPUT_TIMESTAMP:
        LOG_INF("Timestamp: %lld ms", (long long)k_uptime_get());
        break;

    case UI_INPUT_CONFIRM:
    case UI_INPUT_ENCODER_UP:
    case UI_INPUT_ENCODER_DOWN:
    case UI_INPUT_ENCODER_CLICK:
        /*
         * Low-level encoder / confirm events are consumed by LVGL's group
         * system.  The App Layer only acts on high-level semantic events.
         */
        break;

    default:
        LOG_WRN("Unhandled ui_input type: %d", (int)evt->type);
        break;
    }
}

/* ──────────────────────────────────────────────────────────────────────────────────────────────
 * CAN status handler
 * ────────────────────────────────────────────────────────────────────────────────────────────── */

static void handle_can_status(const struct can_status_event *evt)
{
    switch (evt->type) {
    case CAN_STATUS_CONNECTED:
        app_state_set_can_status(true, false);
        LOG_INF("CAN: connected");
        break;

    case CAN_STATUS_DISCONNECTED:
        app_state_set_can_status(false, false);
        LOG_WRN("CAN: disconnected");
        break;

    case CAN_STATUS_TIMEOUT:
        app_state_set_can_status(false, false);
        LOG_WRN("CAN: timeout (msg_id=0x%03X)", evt->msg_id);
        break;

    case CAN_STATUS_BUS_OFF:
        app_state_set_can_status(false, true);
        app_state_set_system_flags(true, false);
        LOG_ERR("CAN: bus-off");
        break;

    default:
        LOG_WRN("Unknown CAN status type: %d", (int)evt->type);
        break;
    }
}

/* ──────────────────────────────────────────────────────────────────────────────────────────────
 * CAN data handler
 * ────────────────────────────────────────────────────────────────────────────────────────────── */

static void handle_can_data(const struct can_data_snapshot *snap)
{
    app_state_update_can_data(snap);

    /*
     * Forward to the UI module unconditionally.  The LVGL subjects perform
     * change detection themselves (observers fire only on value change), so
     * screens that do not display CAN data cost nothing.
     */
    struct ui_cmd cmd = {
        .type          = UI_CMD_UPDATE_DATA,
        .data.snapshot = *snap,
    };
    pub_ui_cmd(&cmd);

    /* RTD sound: drive piezo on/off whenever the signal changes. */
    if (snap->rtd_sound != s_last_rtd_sound) {
        s_last_rtd_sound = snap->rtd_sound;

        struct audio_cmd acmd;
        if (snap->rtd_sound) {
            acmd.type   = AUDIO_CMD_PLAY_EFFECT;
            acmd.effect = AUDIO_EFFECT_RTD_READY;
            // LOG_INF("RTD sound ON");
        } else {
            acmd.type   = AUDIO_CMD_STOP;
            acmd.effect = AUDIO_EFFECT_NONE;
            // LOG_INF("RTD sound OFF");
        }

        int ret = zbus_chan_pub(&audio_cmd_chan, &acmd, K_NO_WAIT);
        if (ret != 0) {
            LOG_ERR("audio_cmd_chan publish failed: %d", ret);
        }
    }
}

/* ──────────────────────────────────────────────────────────────────────────────────────────────
 * Safety event handler
 * ────────────────────────────────────────────────────────────────────────────────────────────── */

/**
 * @brief Update safety flags in app_state based on incoming CAN safety events.
 *
 * The current system sub-state is read first so unrelated flags are not
 * inadvertently cleared during a delta update.
 *
 * @note Full safety response (SCREEN_ERROR, lighting/audio override) is
 *       deferred to a future implementation phase.  Lighting and Audio modules
 *       already subscribe directly to safety_chan for immediate override.
 */
static void handle_safety(const struct safety_event *evt)
{
    struct app_state_system sys;
    app_state_get_system(&sys);

    switch (evt->type) {
    case SAFETY_EVT_IMD_FAULT:
        app_state_set_safety_flags(false, sys.ams_ok, sys.ts_active);
        app_state_set_system_flags(true, sys.warning_active);
        LOG_ERR("Safety: IMD fault");
        break;

    case SAFETY_EVT_AMS_FAULT:
        app_state_set_safety_flags(sys.imd_ok, false, sys.ts_active);
        app_state_set_system_flags(true, sys.warning_active);
        LOG_ERR("Safety: AMS fault");
        break;

    case SAFETY_EVT_SHUTDOWN_OPEN:
        app_state_set_safety_flags(sys.imd_ok, sys.ams_ok, false);
        LOG_WRN("Safety: shutdown circuit open");
        break;

    case SAFETY_EVT_SHUTDOWN_CLOSED:
        LOG_INF("Safety: shutdown circuit closed");
        break;

    case SAFETY_EVT_TS_OFF:
        app_state_set_safety_flags(sys.imd_ok, sys.ams_ok, false);
        LOG_INF("Safety: TS off");
        break;

    case SAFETY_EVT_TS_ACTIVE:
        app_state_set_safety_flags(sys.imd_ok, sys.ams_ok, true);
        LOG_INF("Safety: TS active");
        break;

    default:
        LOG_WRN("Unknown safety event type: %d", (int)evt->type);
        break;
    }
}

/* ──────────────────────────────────────────────────────────────────────────────────────────────
 * App thread
 * ────────────────────────────────────────────────────────────────────────────────────────────── */

/**
 * @brief App thread entry point.
 *
 * Blocks on zbus_sub_wait() until any subscribed channel publishes, then
 * reads the payload and dispatches to the appropriate handler.
 *
 * The thread never yields voluntarily between events — it sleeps inside
 * zbus_sub_wait() until the OS wakes it.
 */
static void app_thread_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    const struct zbus_channel *chan;

    while (true) {
        int rc = zbus_sub_wait(&app_sub, &chan, K_FOREVER);
        if (rc != 0) {
            LOG_ERR("zbus_sub_wait error: %d", rc);
            continue;
        }

        /*
         * Use K_MSEC(10) instead of K_NO_WAIT for all zbus_chan_read calls.
         *
         * Race condition with K_NO_WAIT:
         *   zbus_chan_pub() holds the channel mutex while notifying observers
         *   (calling k_msgq_put on the subscriber's queue).  If the publisher
         *   thread has LOWER priority than this App thread (e.g., LVGL thread
         *   pri 8 < App thread pri 5), the App thread preempts immediately
         *   after k_msgq_put — before the publisher releases the mutex.
         *   zbus_chan_read with K_NO_WAIT then returns -EAGAIN (mutex busy),
         *   silently dropping the event.
         *
         *   K_MSEC(10) lets the App thread wait briefly for the publisher to
         *   release the mutex.  This covers the LVGL → Zbus → App path and
         *   any future low-priority publisher.
         */
        if (chan == &ui_input_chan) {
            struct ui_input_event evt;
            int rc2 = zbus_chan_read(&ui_input_chan, &evt, K_MSEC(10));
            if (rc2 == 0) {
                handle_ui_input(&evt);
            } else {
                LOG_ERR("ui_input_chan read failed: %d", rc2);
            }

        } else if (chan == &can_status_chan) {
            struct can_status_event evt;
            if (zbus_chan_read(&can_status_chan, &evt, K_MSEC(10)) == 0) {
                handle_can_status(&evt);
            }

        } else if (chan == &can_data_chan) {
            struct can_data_snapshot snap;
            if (zbus_chan_read(&can_data_chan, &snap, K_MSEC(10)) == 0) {
                handle_can_data(&snap);
            }

        } else if (chan == &safety_chan) {
            struct safety_event evt;
            if (zbus_chan_read(&safety_chan, &evt, K_MSEC(10)) == 0) {
                handle_safety(&evt);
            }

        } else if (chan == &settings_chan) {
            /* TODO: reload display brightness and other run-time settings */
            LOG_DBG("settings_chan event (not yet handled)");

        } else if (chan == &feedback_chan) {
            /* TODO: react to lighting/audio effect completion */
            LOG_DBG("feedback_chan event (not yet handled)");

        } else {
            LOG_WRN("Unexpected channel notification");
        }
    }
}


/* ── Public Function Implementations ─────────────────────────────────────────────────────────── */

void app_module_init(void)
{
    /* Reset all state fields to safe defaults */
    app_state_init();

    k_thread_create(&s_app_thread,
                    s_app_stack,
                    K_THREAD_STACK_SIZEOF(s_app_stack),
                    app_thread_fn,
                    NULL, NULL, NULL,
                    APP_THREAD_PRIORITY, 0, K_NO_WAIT);
    k_thread_name_set(&s_app_thread, "app");

    LOG_INF("App module initialised");
}
