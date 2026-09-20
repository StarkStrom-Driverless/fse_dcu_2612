/**
 * @file        demo.c
 * @brief       Demo module implementation — fake CAN data and a screen tour
 *
 * @ingroup     dcu_demo
 *
 * @details     Built only with CONFIG_DCU_DEMO_MODE, which depends on
 *              QEMU_TARGET. The contract is in demo.h.
 *
 *              ### What is faked, and where
 *              One struct can_data_snapshot, filled with plausible values and
 *              published on can_data_chan every DEMO_TICK_MS. That is the exact
 *              interface between the CAN module and the rest of the firmware:
 *              the App Layer forwards it to the UI, which pushes it into the
 *              generated LVGL subjects and derives the header icons from it.
 *              Every screen therefore renders as it would for real bus traffic.
 *
 *              Anything below that line is not exercised — the CAN driver, its
 *              RX filters, the decoder and the message timeouts. The alternative
 *              would have been to pack real frames and send them into the
 *              loopback controller, which covers all of that at the price of a
 *              pack call, and a physical-to-raw encode, for each of the thirteen
 *              RX messages. For screenshots that buys nothing.
 *
 *              ### The tour
 *              Every DCU_DEMO_SCREEN_PERIOD_MS the thread asks for the next
 *              screen of k_tour[] on ui_nav_chan and wraps around at the end.
 *              That channel is the App Layer's to publish on in the real
 *              firmware. Here the demo takes its place, deliberately: routing
 *              the tour through the state machine would mean faking RTD_State
 *              and AS_state edges to reach EV DRIVING and DV DRIVING, and the
 *              two would then fight over the screen. The screen change goes
 *              through the UI's normal path from there on (handle_ui_nav →
 *              ui_load_screen), without a transition animation.
 *
 *              The tour starts on the boot screen, which the UI has already
 *              loaded when this module comes up, so the first entry of k_tour[]
 *              is what is on the display at time zero.
 *
 *              ### What the values are chosen to show
 *              A car standing still, brake pedal pressed, throttle released,
 *              shutdown circuit closed, everything inside its limits — the
 *              state in which the driver would be looking at EV CHECKLIST. That is
 *              nominal on purpose: every readout is in its green range, so a
 *              screenshot shows the layout, not a fault. To show the gold or
 *              red coloring, move a value past its limit in s_snapshot; the
 *              limits are declared in dbc/dcu_app.yaml.
 *
 *              Three values need a note:
 *              - rtd_state stays 0. Raising it would make the App Layer load
 *                EV DRIVING by itself, in the middle of the tour.
 *              - as_state is 1 (AS_OFF, see state_machine.c). 3 is AS_DRIVING
 *                and would do the same for DV DRIVING.
 *              - datalogger_recording stays 0. Recording makes the logger icon
 *                blink, so a screenshot would catch it in either phase.
 *
 *              ### Mission
 *              DV DRIVING shows the mission chosen on DV MISSION, which lives in
 *              app_state. The demo selects one once, through the same
 *              UI_INPUT_MISSION_SELECTED event the mission screen publishes, so
 *              the App Layer records it the way it always does. app_state has
 *              exactly one writer and this is not a second.
 *
 * @author      Mario Wegmann <mario.wegmann@web.de>
 * @date        Created: 2026-09-20
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
 * 0.1.0    2026-09-20  Mario Wegmann   Initial creation
 * ─────────────────────────────────────────────────────────────────────────────────────────────────
 */

/* ── Corresponding Header ────────────────────────────────────────────────────────────────────── */

#include "modules/demo/demo.h"

/* ── Zephyr Includes ─────────────────────────────────────────────────────────────────────────── */

#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/logging/log.h>

/* ── Project Includes ────────────────────────────────────────────────────────────────────────── */

#include "generated/can_data_gen.h"
#include "services/event_bus/event_bus.h"
#include "services/event_bus/events.h"

/* ── Zephyr Logging ──────────────────────────────────────────────────────────────────────────── */

LOG_MODULE_REGISTER(demo, CONFIG_LOG_DEFAULT_LEVEL);


/* ── Private Macros & Constants ──────────────────────────────────────────────────────────────── */

/**
 * @brief Stack size for the demo thread.
 *
 * Holds a zbus publish and a log call; the snapshot is static, not on the
 * stack.
 */
#define DEMO_THREAD_STACK_SIZE  1024U

/**
 * @brief Scheduling priority for the demo thread.
 *
 * Behind every functional thread — a sleeping timer that publishes twice a
 * second has no business preempting the UI.
 */
#define DEMO_THREAD_PRIORITY    9

/**
 * @brief Period of the snapshot, in milliseconds.
 *
 * The rate the real signals arrive at. It also keeps timestamp_ms fresh, in
 * case something starts to look at the age of the data.
 */
#define DEMO_TICK_MS            100U

/**
 * @brief When the mission is selected, counted from the start of the thread.
 *
 * Half a second: after the boot screen's UI_INPUT_SCREEN_CHANGED has been
 * consumed, before the first screen change of the tour can publish on the same
 * channel — the App Layer would otherwise see only the newer of the two.
 */
#define DEMO_MISSION_DELAY_MS   500U

/** @brief Mission shown on DV DRIVING. */
#define DEMO_MISSION            MISSION_TRACKDRIVE

/**
 * @brief How long a zbus publish may wait for the channel, in milliseconds.
 *
 * The App thread holds the channel while it reads it. This thread is the
 * lowest-priority one, so waiting is free.
 */
#define DEMO_PUB_TIMEOUT_MS     10


/* ── Tour ────────────────────────────────────────────────────────────────────────────────────── */

/**
 * @brief One stop of the tour: the screen and the name the log prints for it.
 */
struct tour_stop {
    enum screen_id screen; /**< Screen to load.                                */
    const char    *name;   /**< For the log line, to match a screenshot to it. */
};

/**
 * @brief The screens in the order they are shown.
 *
 * Follows the driver's day: boot, choose a mission, check the shutdown circuit,
 * prepare, drive — then the engineering screens and finally the settings. Every
 * screen that has a factory in ui.c is in here once; SCREEN_RTD, POST_RTD and
 * ERROR are reserved in the enum and have none.
 */
static const struct tour_stop k_tour[] = {
    { SCREEN_BOOT,           "START"        },
    { SCREEN_MISSION_SELECT, "DV MISSION"   },
    { SCREEN_SDC,            "SDC"          },
    { SCREEN_PRE_RTD,        "EV CHECKLIST" },
    { SCREEN_EV_DRIVING,     "EV DRIVING"   },
    { SCREEN_DV_DRIVING,     "DV DRIVING"   },
    { SCREEN_DEBUG_TS,       "DBG TS"       },
    { SCREEN_DEBUG_PRESSURE, "DBG PRESSURE" },
    { SCREEN_DEBUG_HV_ACCU,  "DBG HV ACCU"  },
    { SCREEN_DEBUG_LV_ACCU,  "DBG LV ACCU"  },
    { SCREEN_DEBUG_CUSTOM,   "DBG CUSTOM"   },
    { SCREEN_SETTINGS,       "SETTINGS"     },
};


/* ── Private Variables ───────────────────────────────────────────────────────────────────────── */

/** @brief Stack of the demo thread. */
static K_THREAD_STACK_DEFINE(s_demo_stack, DEMO_THREAD_STACK_SIZE);

/** @brief Thread control block. */
static struct k_thread s_demo_thread;

/**
 * @brief The invented vehicle state, published unchanged every DEMO_TICK_MS.
 *
 * Static and written only by the demo thread, apart from timestamp_ms. Fields
 * not named keep their zero, i.e. "off" / "closed" / "0" — which for the flags
 * below is the right value only where the comment says so.
 *
 * Units and ranges follow dbc/dcu_app.yaml, and so do the limits the screens
 * color by (the ui_sig_* descriptors). The values below stay inside those
 * limits; if a limit in the YAML moves, check the value next to it.
 */
static struct can_data_snapshot s_snapshot = {
    /* ── mABX_2_DCU_1: status LEDs ── */
    .dv_receiving         = true,   /* DV PC icon green                          */
    .dv_ready             = true,   /* ROS icon green                            */
    .sdc_open             = false,
    .datalogger_ready     = true,   /* logger icon green                         */
    .datalogger_recording = false,  /* true would blink, see the file header     */
    .rtd_possible         = true,
    .startup_finish       = true,
    .kistler_timeout      = false,  /* Kistler icon green                        */
    .temperature_generic  = 32,
    .led_reserved_0       = false,
    .power_average        = 42,     /* kW                                        */

    /* ── mABX_2_DCU_2: temperatures, voltages, generic values ── */
    .temperature_accu_hv      = 38, /* °C                                        */
    .temperature_inverter     = 52, /* °C                                        */
    .temperature_motor        = 61, /* °C                                        */
    .voltage_accu_hv          = 496, /* V                                        */
    .voltage_tractive_system  = 494, /* V                                        */
    .custom_1                 = 1234,
    .custom_2                 = 42,

    /* ── Pressures: brake applied for the RTD request ── */
    .brake_pressure_front = 12.0f,  /* bar                                       */
    .brake_pressure_rear  = 9.2f,
    .air_pressure_front   = 8.4f,
    .air_pressure_rear    = 8.2f,

    /* ── Pedals: percent — throttle released, brake pressed ── */
    .apps_left_position   = 0,
    .apps_right_position  = 0,
    .bpps_position        = 35,

    /* ── Shutdown circuit: 1 = contact closed, all of them ── */
    .sdc_bspd     = true,
    .sdc_cockpit  = true,
    .sdc_ascu     = true,
    .sdc_motor_rl = true,
    .sdc_hvd      = true,
    .sdc_motor_rr = true,
    .sdc_sdb_mh   = true,
    .sdc_res      = true,
    .sdc_motor_fl = true,
    .sdc_bots     = true,
    .sdc_motor_fr = true,
    .sdc_inertia  = true,

    /* ── Low-voltage battery ── */
    .lv_accu_voltage = 24.3f,       /* V                                         */

    /* ── MABX_2_Vehicle ── */
    .rtd_sound = false,
    .rtd_state = false,             /* true would load EV DRIVING by itself      */
    .ams_error = false,
    .imd_error = false,

    /* ── DV_system_status ── */
    .as_state  = 1,                 /* AS_OFF; 3 (AS_DRIVING) would load DV DRIVING */
};


/* ── Private Function Prototypes ─────────────────────────────────────────────────────────────── */

static void publish_snapshot(void);
static void select_mission(void);
static void request_screen(const struct tour_stop *stop);
static void demo_thread_fn(void *p1, void *p2, void *p3);


/* ── Private Function Implementations ───────────────────────────────────────────────────────── */

/**
 * @brief Publish s_snapshot on can_data_chan, stamped with the current uptime.
 */
static void publish_snapshot(void)
{
    s_snapshot.timestamp_ms = k_uptime_get();

    int rc = zbus_chan_pub(&can_data_chan, &s_snapshot, K_MSEC(DEMO_PUB_TIMEOUT_MS));
    if (rc != 0) {
        LOG_WRN("can_data_chan publish failed: %d", rc);
    }
}

/**
 * @brief Select DEMO_MISSION, the way the mission screen does.
 *
 * Publishes UI_INPUT_MISSION_SELECTED; the App Layer stores it in app_state.
 * DV MISSION and DV DRIVING read it from there when they are built.
 */
static void select_mission(void)
{
    struct ui_input_event evt = {
        .type         = UI_INPUT_MISSION_SELECTED,
        .data.mission = DEMO_MISSION,
    };

    int rc = zbus_chan_pub(&ui_input_chan, &evt, K_MSEC(DEMO_PUB_TIMEOUT_MS));
    if (rc != 0) {
        LOG_WRN("UI_INPUT_MISSION_SELECTED publish failed: %d", rc);
    }
}

/**
 * @brief Ask the UI to load the next screen of the tour.
 *
 * @param stop  The stop to move to.
 */
static void request_screen(const struct tour_stop *stop)
{
    const struct ui_nav_cmd cmd = { .screen = stop->screen };

    int rc = zbus_chan_pub(&ui_nav_chan, &cmd, K_MSEC(DEMO_PUB_TIMEOUT_MS));
    if (rc != 0) {
        LOG_WRN("ui_nav_chan publish failed: %d", rc);
        return;
    }

    LOG_INF("Demo: %s", stop->name);
}

/**
 * @brief Demo thread — publish the snapshot, advance the tour on schedule.
 *
 * Time is counted in ticks rather than read from the uptime, so the period
 * cannot slip by more than one tick, and a tick that ran long does not cause
 * two screens to be requested back to back.
 *
 * @param p1  Unused.
 * @param p2  Unused.
 * @param p3  Unused.
 */
static void demo_thread_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    uint32_t elapsed_ms   = 0U;   /* since the current screen came up */
    uint32_t running_ms   = 0U;   /* since the thread started         */
    size_t   tour_idx     = 0U;
    bool     mission_sent = false;

    LOG_INF("Demo mode: %u screens, %u ms each", (unsigned)ARRAY_SIZE(k_tour),
            (unsigned)CONFIG_DCU_DEMO_SCREEN_PERIOD_MS);
    LOG_INF("Demo: %s", k_tour[tour_idx].name);

    /* The first snapshot goes out at once, so the header is populated early. */
    publish_snapshot();

    while (true) {
        k_msleep(DEMO_TICK_MS);
        elapsed_ms += DEMO_TICK_MS;
        running_ms += DEMO_TICK_MS;

        publish_snapshot();

        if (!mission_sent && running_ms >= DEMO_MISSION_DELAY_MS) {
            select_mission();
            mission_sent = true;
        }

        if (elapsed_ms >= CONFIG_DCU_DEMO_SCREEN_PERIOD_MS) {
            elapsed_ms = 0U;
            tour_idx   = (tour_idx + 1U) % ARRAY_SIZE(k_tour);
            request_screen(&k_tour[tour_idx]);
        }
    }
}


/* ── Public Function Implementations ─────────────────────────────────────────────────────────── */

void demo_module_init(void)
{
    k_thread_create(&s_demo_thread,
                    s_demo_stack,
                    K_THREAD_STACK_SIZEOF(s_demo_stack),
                    demo_thread_fn,
                    NULL, NULL, NULL,
                    DEMO_THREAD_PRIORITY,
                    0,
                    K_NO_WAIT);

    k_thread_name_set(&s_demo_thread, "demo");
}
