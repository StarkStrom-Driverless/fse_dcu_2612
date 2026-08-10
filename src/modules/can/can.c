/**
 * @file        can.c
 * @brief       CAN module — frame TX, bus management, and status reporting
 *
 * @details     Owns the CAN controller hardware instance.  All frame layouts
 *              (pack/unpack/dispatch) come from the code generator — this
 *              file contains no hand-written bit manipulation.  To add a CAN
 *              message, edit dbc/dcu_app.yaml and run
 *              `python3 tools/codegen/gen_can.py`; no change here is needed.
 *
 *              Periodic TX
 *              ───────────
 *              A dedicated worker thread (priority 3) sends DCU_2_mABX
 *              (0x196) every 100 ms.  On each cycle the thread reads the
 *              current mission and operating mode directly from app_state
 *              (the single source of truth) and packs them via the generated
 *              dcu_can_gen_dcu_2_m_abx_pack().  No local state copy is kept.
 *
 *                drive_mode = mission_to_drive_mode(app_state_get_selected_mission())
 *                rtd_active = (app_state_get_mode() == OPERATING_MODE_RTD)
 *
 *              Periodic RX
 *              ───────────
 *              Hardware RX filters (one per ID in can_rx_gen_frame_ids[])
 *              route all generated RX frames into a message queue.  The
 *              worker thread drains the queue each cycle, decodes via
 *              can_rx_gen_dispatch() into a can_data_snapshot, and publishes
 *              it to can_data_chan.  The App Layer stores it in app_state.
 *
 *              Bus status
 *              ──────────
 *              can_module_init() publishes CAN_STATUS_CONNECTED after the
 *              controller is started successfully.  A future RX watchdog
 *              will publish CAN_STATUS_TIMEOUT when frames stop arriving.
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

#include "modules/can/can.h"

/* ── Zephyr Includes ─────────────────────────────────────────────────────────────────────────── */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/can.h>
#include <zephyr/sys/__assert.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/logging/log.h>

/* ── Project Includes ────────────────────────────────────────────────────────────────────────── */

#include "app/app_state.h"
#include "generated/dcu_can_gen.h"
#include "generated/can_rx_gen.h"
#include "generated/can_tx_gen.h"
#include "services/event_bus/event_bus.h"
#include "services/event_bus/events.h"
#include "services/settings/settings.h"

/* ── Zephyr Logging ──────────────────────────────────────────────────────────────────────────── */

LOG_MODULE_REGISTER(can_module, CONFIG_LOG_DEFAULT_LEVEL);


/* ── Private Macros & Constants ──────────────────────────────────────────────────────────────── */

/** @brief CAN bus bitrate. Must match the vehicle network configuration. */
#define CAN_BITRATE_BPS         1000000U

/** @brief Timeout for a single CAN TX attempt. */
#define CAN_TX_TIMEOUT          K_MSEC(100)

/** @brief Thread stack size for the CAN worker thread. */
#define CAN_THREAD_STACK_SIZE   2048U

/** @brief Thread scheduling priority for the CAN worker (higher = more urgent). */
#define CAN_THREAD_PRIORITY     3

/** @brief Capacity of the RX frame message queue (frames buffered between cycles). */
#define CAN_RX_MSGQ_DEPTH       550U

/** @brief Base tick interval of the CAN worker thread. TX messages are sent at
 *         multiples of this value (see CAN_TX_GEN_*_PERIOD_MS in can_tx_gen.h). */
#define CAN_TX_PERIOD_MS        10U

/* ── Private Variables ───────────────────────────────────────────────────────────────────────── */

/**
 * @brief Zephyr CAN controller device handle.
 *
 * Resolved at compile time from the devicetree `chosen { zephyr,canbus }` node.
 * The board overlay must define this node for CAN to function.
 */
static const struct device *const s_can_dev =
    DEVICE_DT_GET(DT_CHOSEN(zephyr_canbus));

/** @brief Thread stack storage for the CAN worker thread. */
static K_THREAD_STACK_DEFINE(s_can_stack, CAN_THREAD_STACK_SIZE);

/** @brief Thread control block for the CAN worker thread. */
static struct k_thread s_can_thread;

/** @brief Flag set after hardware initialisation completes successfully. */
static bool s_hw_ready;

/** @brief Tick counter incremented once per CAN_TX_PERIOD_MS cycle. Used to
 *         schedule TX messages at their individual period_ms via modulo. */
static uint32_t s_tx_tick;

/** @brief Last-known CAN bus state; used to detect and publish state changes. */
static enum can_state s_can_state_prev = CAN_STATE_STOPPED;

/* Verify that enum can_bus_state values (events.h) match enum can_state (Zephyr). */
_Static_assert((int)CAN_STATE_ERROR_ACTIVE  == (int)CAN_BUS_STATE_ERROR_ACTIVE,  "CAN state enum mismatch");
_Static_assert((int)CAN_STATE_ERROR_WARNING == (int)CAN_BUS_STATE_ERROR_WARNING, "CAN state enum mismatch");
_Static_assert((int)CAN_STATE_ERROR_PASSIVE == (int)CAN_BUS_STATE_ERROR_PASSIVE, "CAN state enum mismatch");
_Static_assert((int)CAN_STATE_BUS_OFF       == (int)CAN_BUS_STATE_BUS_OFF,       "CAN state enum mismatch");
_Static_assert((int)CAN_STATE_STOPPED       == (int)CAN_BUS_STATE_STOPPED,       "CAN state enum mismatch");

/**
 * @brief RX message queue, filled by the CAN driver ISR for matching frames.
 *
 * Drained by the CAN worker thread once per 100 ms cycle.  Depth 16 buffers
 * more than one full period of both mABX frames at their expected rates.
 */
CAN_MSGQ_DEFINE(s_can_rx_msgq, CAN_RX_MSGQ_DEPTH);

/**
 * @brief Accumulator for decoded RX signal values.
 *
 * Only touched by the CAN worker thread.  Holds the latest decoded value of
 * every signal; published as a whole to can_data_chan after each RX drain so
 * partial updates (only one of the two frames received) keep earlier values.
 */
static struct can_data_snapshot s_rx_snapshot; // 80UL = 320 Byte?


/* ── Private Function Prototypes ─────────────────────────────────────────────────────────────── */

static int  can_hw_init(void);
static void can_send_dcu2_mabx(uint8_t drive_mode, bool rtd, uint8_t debug);
static bool can_drain_rx(void);
static void can_thread_fn(void *p1, void *p2, void *p3);


/* ── Private Function Implementations ───────────────────────────────────────────────────────── */

/**
 * @brief Configure the CAN controller and start the bus.
 *
 * @return 0 on success, negative errno on failure.
 */
static int can_hw_init(void)
{
    int ret;

    if (!device_is_ready(s_can_dev)) {
        LOG_ERR("CAN device not ready (%s)", s_can_dev->name);
        return -ENODEV;
    }

    ret = can_set_bitrate(s_can_dev, CAN_BITRATE_BPS);
    if (ret != 0) {
        LOG_ERR("can_set_bitrate(%u) failed: %d", CAN_BITRATE_BPS, ret);
        return ret;
    }

    ret = can_set_mode(s_can_dev, CAN_MODE_NORMAL);
    if (ret != 0) {
        LOG_ERR("can_set_mode(NORMAL) failed: %d", ret);
        return ret;
    }

    ret = can_start(s_can_dev);
    if (ret != 0) {
        LOG_ERR("can_start() failed: %d", ret);
        return ret;
    }

    /*
     * ── RX filters: route all generated RX frame IDs into the msgq ──────
     *
     * The ID list comes from the code generator (dcu_app.yaml, direction:
     * rx).  Adding a message there automatically registers its filter here
     * — no manual edit required.
     */
    for (size_t i = 0U; i < CAN_RX_GEN_NUM_FRAMES; i++) {
        const struct can_filter filter = {
            .id    = can_rx_gen_frame_ids[i],
            .mask  = CAN_STD_ID_MASK,
            .flags = 0U,
        };

        ret = can_add_rx_filter_msgq(s_can_dev, &s_can_rx_msgq, &filter);
        if (ret < 0) {
            LOG_ERR("RX filter 0x%03X failed: %d", filter.id, ret);
            return ret;
        }
    }

    LOG_INF("CAN controller ready (%s, %u bps)", s_can_dev->name, CAN_BITRATE_BPS);
    return 0;
}

/**
 * @brief Build and transmit a DCU_2_mABX frame.
 *
 * Uses the generated pack function (dcu_can_gen.h) — the bit layout comes
 * straight from the DBC via the code generator.
 *
 * Logs an error if the frame cannot be delivered within CAN_TX_TIMEOUT.
 *
 * @param drive_mode  DV_Drive_Mode_SETTING raw value (0–7).
 * @param rtd         true → RTD_Button = 1 (Ready-to-Drive request).
 * @param debug       Debug_SETTING raw value (0–7).
 */
static void can_send_dcu2_mabx(uint8_t drive_mode, bool rtd, uint8_t debug)
{
    struct can_frame frame = {
        .id    = DCU_CAN_GEN_DCU_2_M_ABX_FRAME_ID,
        .dlc   = DCU_CAN_GEN_DCU_2_M_ABX_LENGTH,
        .flags = 0,   /* standard (11-bit) frame, no CAN-FD */
    };

    const struct dcu_can_gen_dcu_2_m_abx_t msg = {
        .debug_setting         = debug,
        .dv_drive_mode_setting = drive_mode,
        .rtd_button            = rtd ? 1U : 0U,
    };

    if (dcu_can_gen_dcu_2_m_abx_pack(frame.data, &msg, sizeof(frame.data)) < 0) {
        LOG_ERR("TX 0x%03X pack failed", frame.id);
        return;
    }

    int ret = can_send(s_can_dev, &frame, CAN_TX_TIMEOUT, NULL, NULL);
    if (ret != 0) {
        LOG_ERR("TX 0x%03X failed: %d (drive_mode=%u rtd=%d debug=%u)",
                frame.id, ret, drive_mode, (int)rtd, debug);
    } else {
        LOG_DBG("TX 0x%03X  drive_mode=%u  rtd=%d  debug=%u",
                frame.id, drive_mode, (int)rtd, debug);
    }
}

/**
 * @brief Drain all queued RX frames and decode them into the snapshot.
 *
 * Decoding is fully generated: can_rx_gen_dispatch() knows every
 * direction:rx message from dcu_app.yaml.  Adding a message there requires
 * no change in this file.
 *
 * @return true if at least one frame was decoded (snapshot changed).
 */
static bool can_drain_rx(void)
{
    struct can_frame frame;
    bool updated = false;

    while (k_msgq_get(&s_can_rx_msgq, &frame, K_NO_WAIT) == 0) {
        if (can_rx_gen_dispatch(frame.id, frame.data, can_dlc_to_bytes(frame.dlc),
                                &s_rx_snapshot)) {
            updated = true;
        } else {
            /* Filters only match generated IDs — should not happen. */
            LOG_WRN("Unexpected RX frame id 0x%03X", frame.id);
        }
    }

    return updated;
}

/**
 * @brief Convert a mission_id to the DV_Drive_Mode_SETTING raw value.
 *
 * The numeric values of enum mission_id are defined to match the DBC
 * encoding directly (MISSION_NONE = 0 … MISSION_MANUAL_DRIVING = 6).
 */
static inline uint8_t mission_to_drive_mode(enum mission_id mission)
{
    return (uint8_t)mission;
}

/**
 * @brief CAN worker thread entry point.
 *
 * Runs a fixed CAN_TX_PERIOD_MS (10 ms) base tick:
 *
 *   1. Drain the RX message queue — decode every received frame into the
 *      snapshot accumulator.  If anything arrived, publish the full snapshot
 *      to can_data_chan (the App Layer stores it in app_state).
 *   2. For each TX message: check s_tx_tick % (period_ms / CAN_TX_PERIOD_MS).
 *      If zero, read current state from app_state and transmit the frame.
 *   3. Increment s_tx_tick and sleep for the remainder of the base tick.
 *
 * TX periods come from can_tx_gen.h (generated from dcu_app.yaml period_ms).
 * Adding a TX message there requires updating only the send_* call below.
 *
 * app_state getters are thread-safe (mutex-protected inside app_state.c)
 * so they are safe to call here.
 *
 * The thread exits silently if hardware initialisation failed so that the
 * rest of the firmware continues to function without CAN.
 */
static void can_thread_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    if (!s_hw_ready) {
        LOG_WRN("CAN HW not ready — worker thread exiting");
        return;
    }

    while (true) {
        /* ── 1. Drain RX queue and publish decoded snapshot ──────────── */
        if (can_drain_rx()) {
            s_rx_snapshot.timestamp_ms = k_uptime_get();

            int rc = zbus_chan_pub(&can_data_chan, &s_rx_snapshot, K_NO_WAIT);
            if (rc != 0) {
                LOG_WRN("can_data_chan publish failed: %d", rc);
            }
        }

        /* ── 2. Transmit scheduled TX messages ──────────────────────── */
        if (s_tx_tick % (CAN_TX_GEN_DCU_2_M_ABX_PERIOD_MS / CAN_TX_PERIOD_MS) == 0) {
            /*
             * Volatile state comes from app_state, persistent settings from
             * the Settings service — one acquisition for all of them.  The
             * LVGL ui_tx_subj_* subjects are a UI mirror and must not be read
             * from this thread (see docs/settings_module.md §2).
             */
            uint8_t settings[SETTING_COUNT];
            settings_get_all(settings);

            uint8_t drive_mode = mission_to_drive_mode(app_state_get_selected_mission());
            bool    rtd_active = (app_state_get_mode() == OPERATING_MODE_RTD);
            uint8_t debug      = settings[SETTING_DEBUG_BITS];
            can_send_dcu2_mabx(drive_mode, rtd_active, debug);
        }

        /* ── 3. Poll CAN bus state; publish to can_status_chan on change ─ */
        enum can_state cur_state;
        struct can_bus_err_cnt err_cnt;
        if (can_get_state(s_can_dev, &cur_state, &err_cnt) == 0 &&
            cur_state != s_can_state_prev) {
            s_can_state_prev = cur_state;
            struct can_status_event state_evt = {
                .type   = CAN_STATUS_CONNECTED,
                .msg_id = 0U,
                .state  = (enum can_bus_state)cur_state,
            };
            int rc = zbus_chan_pub(&can_status_chan, &state_evt, K_NO_WAIT);
            if (rc != 0) {
                LOG_WRN("can_status_chan (state) publish failed: %d", rc);
            }
        }

        /* ── 4. Advance tick and wait for next base slot ─────────────── */
        s_tx_tick++;
        k_msleep(CAN_TX_PERIOD_MS);
    }
}


/* ── Public Function Implementations ─────────────────────────────────────────────────────────── */

void can_module_init(void)
{
    int ret = can_hw_init();
    if (ret != 0) {
        /* Hardware failure — thread will exit gracefully on first run. */
        s_hw_ready = false;
        LOG_ERR("CAN module init failed (%d) — TX disabled", ret);
    } else {
        s_hw_ready = true;

        /* can_start() leaves the controller in ERROR_ACTIVE. */
        s_can_state_prev = CAN_STATE_ERROR_ACTIVE;

        /* Publish initial connected status to the event bus. */
        struct can_status_event status_evt = {
            .type   = CAN_STATUS_CONNECTED,
            .msg_id = 0U,
            .state  = CAN_BUS_STATE_ERROR_ACTIVE,
        };
        ret = zbus_chan_pub(&can_status_chan, &status_evt, K_NO_WAIT);
        if (ret != 0) {
            LOG_WRN("Failed to publish CAN_STATUS_CONNECTED: %d", ret);
        }
    }

    k_thread_create(&s_can_thread,
                    s_can_stack,
                    K_THREAD_STACK_SIZEOF(s_can_stack),
                    can_thread_fn,
                    NULL, NULL, NULL,
                    CAN_THREAD_PRIORITY,
                    0,
                    K_NO_WAIT);

    k_thread_name_set(&s_can_thread, "can_worker");

    LOG_INF("CAN module initialised");
}
