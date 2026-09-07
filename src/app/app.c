/**
 * @file        app.c
 * @brief       App Layer — Dirigent coordinator between all modules
 *
 * @ingroup     dcu_app
 *
 * @details     The App Layer is the single source of truth for the application
 *              state and the exclusive writer of app_state.  It coordinates all
 *              modules by reacting to upward Zbus events and issuing downward
 *              commands.
 *
 *              ### Threading
 *              A single app thread (priority 5, stack 2 kB) blocks on
 *              zbus_sub_wait() forever.  On each notification it reads the
 *              channel payload and dispatches to the appropriate handler.
 *
 *              ### Upward channels consumed (Module → App)
 *
 *              | Channel         | Carries                                     |
 *              |-----------------|---------------------------------------------|
 *              | ui_input_chan   | Driver interactions (mission, RTD, settings) |
 *              | can_status_chan | CAN bus connectivity and error state         |
 *              | can_data_chan   | Decoded CAN signal snapshots                 |
 *              | settings_chan   | Settings load / update events — TODO         |
 *              | feedback_chan   | Effect completion feedback — TODO            |
 *
 *              ### Downward channels produced (App → Module)
 *
 *              | Channel        | Carries                                    |
 *              |----------------|--------------------------------------------|
 *              | ui_cmd_chan    | Screen navigation and data push commands   |
 *              | audio_cmd_chan | Piezo on/off, following the RTD sound signal |
 *
 *              No frame-level CAN commands are published.  The CAN module pulls
 *              mission and operating mode out of app_state on its own TX cycle,
 *              so can_tx_cmd_chan stays unused (see modules/can/can.c).
 *
 *              ### Operating mode transitions
 *
 *              Owned by src/app/state_machine.c (Zephyr SMF), not by this file.
 *              app.c only forwards the trigger: a long press of the RTD button
 *              becomes SM_EVENT_RTD_REQUEST, and the state machine's entry
 *              action raises the mode and switches the UI to the EV driving
 *              screen. There is no path back to `DEBUG` — leaving RTD means a
 *              power cycle. PRE_RTD and POST_RTD are still unused.
 *
 *              The mode is what the CAN module turns into the RTD_Button bit.
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

#include "app/app.h"

/* ── Zephyr Includes ─────────────────────────────────────────────────────────────────────────── */

#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/logging/log.h>

/* ── Project Includes ────────────────────────────────────────────────────────────────────────── */

#include "app/app_state.h"
#include "app/state_machine.h"
#include "services/event_bus/event_bus.h"
#include "services/event_bus/events.h"
#include "services/settings/settings.h"

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
ZBUS_CHAN_ADD_OBS(settings_chan,   app_sub, 0);
ZBUS_CHAN_ADD_OBS(feedback_chan,   app_sub, 0);


/* ── Private Function Prototypes ─────────────────────────────────────────────────────────────── */

static void pub_ui_cmd(const struct ui_cmd *cmd);
static void handle_ui_input(const struct ui_input_event *evt);
static void handle_can_status(const struct can_status_event *evt);
static void handle_can_data(const struct can_data_snapshot *snap);
static void app_thread_fn(void *p1, void *p2, void *p3);


/* ── Private Function Implementations ───────────────────────────────────────────────────────── */

/**
 * @brief Publish a command to ui_cmd_chan.
 *
 * Never blocks: a full channel costs the command, not the App thread.
 * Logs an error in that case.
 *
 * @param cmd  Command to copy into the channel.
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
 * None of these handlers touches the CAN driver.  They change app_state or a
 * setting; the CAN module picks the new value up on its next TX cycle.
 *
 * UI_INPUT_MISSION_SELECTED
 *   Records the selected mission in app_state, unlocked and inactive — the
 *   driver may still change it.  The CAN module reads it every cycle.
 *
 * UI_INPUT_RTD_REQUEST
 *   Forwarded to the state machine as SM_EVENT_RTD_REQUEST, which latches
 *   OPERATING_MODE_RTD and loads the EV driving screen.  UI_INPUT_RTD_RELEASE
 *   is no longer produced (the on-screen button latches on long press); the
 *   case is kept only so an old event cannot fall through to the warning.
 *
 * UI_INPUT_BACK
 *   Navigates back to the boot screen.  The left-encoder carousel is handled
 *   inside ui.c; this handles the physical ESC button.
 *
 * UI_INPUT_DEBUG_BITS_SELECTED
 *   Hands the raw value to the Settings service, which clamps, persists and
 *   owns it.  app_state_set_debug_bits() is deliberately not used here.
 *
 * UI_INPUT_SETTING_SELECTED
 *   The generic form of the above, published by the settings screen: carries a
 *   setting_id and a value, both forwarded straight to settings_set().  The
 *   settings service clamps against the generated schema and persists.
 *
 * UI_INPUT_TIMESTAMP
 *   Logs the current Zephyr uptime as an event marker.  Useful for
 *   synchronising external measurements with the firmware timeline.
 *
 * Everything else (confirm, encoder steps, torque-vectoring toggles) is
 * consumed by LVGL or by the screen itself and ignored here.
 *
 * @param evt  Event read from ui_input_chan.
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

    case UI_INPUT_RTD_REQUEST:
        /*
         * RTD button long-pressed. The state machine latches
         * OPERATING_MODE_RTD (→ CAN RTD_Button = 1 on the next TX cycle) and
         * loads the EV driving screen. No direct app_state write here.
         */
        state_machine_post(SM_EVENT_RTD_REQUEST);
        LOG_INF("RTD requested");
        break;

    case UI_INPUT_RTD_RELEASE:
        /* Reserved: there is no RTD exit path — see app/state_machine.c. */
        break;

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
        /*
         * The Settings service owns this value: it clamps to the schema range,
         * persists it, and the CAN module reads it back on the next TX cycle.
         */
        (void)settings_set(SETTING_DEBUG_BITS, bits);
        LOG_INF("Debug bits set: %u (0x%02X)", (unsigned)bits, (unsigned)bits);
        break;
    }

    case UI_INPUT_SETTING_SELECTED: {
        enum setting_id id  = evt->data.setting.id;
        uint8_t         val = evt->data.setting.val;
        /*
         * Same contract as UI_INPUT_DEBUG_BITS_SELECTED, one setting wider:
         * the Settings service clamps to the schema range, persists and owns
         * the value; the CAN TX path reads it back on its next cycle.
         */
        (void)settings_set(id, val);
        LOG_INF("Setting %d set: %u", (int)id, (unsigned)val);
        break;
    }

    case UI_INPUT_SCREEN_CHANGED:
        /*
         * Purely a record. Nothing acts on it here — it exists so modules
         * outside the UI can tell what the driver is looking at, which the
         * lighting module uses to pick between the strip's two modes.
         */
        app_state_set_active_screen(evt->data.screen);
        break;

    case UI_INPUT_TIMESTAMP:
        LOG_INF("Timestamp: %lld ms", (long long)k_uptime_get());
        break;

    case UI_INPUT_TORQUE_VECT_ON:
    case UI_INPUT_TORQUE_VECT_OFF:
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

/**
 * @brief Handle a CAN connectivity or bus-error event.
 *
 * Maps the event onto the connectivity flags in app_state.  A bus-off
 * additionally raises the system error flag, because at that point the
 * controller is silent and no vehicle data can be trusted any more.
 *
 * The bus state carried in evt->state is not evaluated here — the UI module
 * subscribes to the same channel and derives its CAN icon from it directly.
 *
 * @param evt  Event read from can_status_chan.
 */
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

/**
 * @brief Handle a decoded CAN snapshot from the CAN module.
 *
 * Stores the snapshot in app_state, forwards it to the UI, and turns the
 * rtd_sound signal into piezo commands.
 *
 * The RTD sound is edge-triggered: only a change of snap->rtd_sound produces
 * an audio command, so the ~10 Hz snapshot rate does not flood audio_cmd_chan
 * with identical messages.
 *
 * @param snap  Snapshot read from can_data_chan.
 */
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
 *
 * @param p1  Unused.
 * @param p2  Unused.
 * @param p3  Unused.
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

    /* Operating-mode state machine — starts in MANUAL_IDLE (mode DEBUG). */
    state_machine_init();

    k_thread_create(&s_app_thread,
                    s_app_stack,
                    K_THREAD_STACK_SIZEOF(s_app_stack),
                    app_thread_fn,
                    NULL, NULL, NULL,
                    APP_THREAD_PRIORITY, 0, K_NO_WAIT);
    k_thread_name_set(&s_app_thread, "app");

    LOG_INF("App module initialised");
}
