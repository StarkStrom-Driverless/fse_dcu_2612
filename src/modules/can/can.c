/**
 * @file        can.c
 * @brief       CAN module — frame TX, bus management, and status reporting
 *
 * @ingroup     dcu_can
 *
 * @details     Owns the CAN controller hardware instance.  All frame layouts
 *              (pack/unpack/dispatch) come from the code generator — this
 *              file contains no hand-written bit manipulation.  To add a CAN
 *              message, edit dbc/dcu_app.yaml and run
 *              `python3 tools/codegen/gen_can.py`; no change here is needed.
 *
 *              ### Base tick
 *              A dedicated worker thread (priority 3) runs a CAN_TX_PERIOD_MS
 *              (10 ms) cycle.  Every cycle drains RX; TX messages fire on the
 *              subset of cycles that matches their own period, taken from the
 *              generated CAN_TX_GEN_*_PERIOD_MS constants — DCU_2_mABX (0x196)
 *              at 100 ms, i.e. every tenth tick.
 *
 *              ### Pull, not push
 *              Nothing commands a transmission.  On its send cycle the thread
 *              reads the current values from their owners and packs them via
 *              dcu_can_gen_dcu_2_m_abx_pack().  No local state copy is kept,
 *              so the frame on the bus can never lag behind the application:
 *
 *                drive_mode = mission_to_drive_mode(app_state_get_selected_mission())
 *                rtd_active = (app_state_get_mode() == OPERATING_MODE_RTD)
 *                debug      = settings_get_all()[SETTING_DEBUG_BITS]
 *
 *              RX
 *              ──
 *              Hardware RX filters (one per ID in can_rx_gen_frame_ids[])
 *              route all generated RX frames into a message queue filled from
 *              interrupt context.  The worker thread drains the queue each
 *              cycle, decodes via can_rx_gen_dispatch() into a shared
 *              can_data_snapshot, and publishes that snapshot whenever at
 *              least one frame arrived.  The App Layer stores it in app_state
 *              and forwards it to the UI.
 *
 *              ### Bus status
 *              can_module_init() publishes CAN_STATUS_CONNECTED once after the
 *              controller starts.  The worker thread then polls can_get_state()
 *              every tick and republishes on every change, carrying the new
 *              controller state — that is how the UI learns about error-warning,
 *              error-passive and bus-off.  There is no RX watchdog yet, so a
 *              silent bus that stays electrically healthy is not detected and
 *              CAN_STATUS_TIMEOUT is never published.
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

/**
 * @brief Capacity of the RX frame message queue, in frames.
 *
 * Sized far above one cycle's worth of traffic so a scheduling delay of the
 * worker thread cannot drop frames: the driver's ISR discards silently once
 * the queue is full.
 */
#define CAN_RX_MSGQ_DEPTH       550U

/**
 * @brief Base tick interval of the CAN worker thread, in milliseconds.
 *
 * TX messages are sent at multiples of this value (see CAN_TX_GEN_*_PERIOD_MS
 * in can_tx_gen.h), so it must divide every generated TX period evenly.  It
 * also bounds the RX latency: a frame waits at most one tick in the queue.
 */
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

/*
 * Verify that enum can_bus_state values (events.h) match enum can_state
 * (Zephyr).  The worker thread casts one to the other instead of translating,
 * which keeps events.h free of any Zephyr dependency — these assertions are
 * what makes that cast safe, and they fail the build if either enum moves.
 */
_Static_assert((int)CAN_STATE_ERROR_ACTIVE  == (int)CAN_BUS_STATE_ERROR_ACTIVE,  "CAN state enum mismatch");
_Static_assert((int)CAN_STATE_ERROR_WARNING == (int)CAN_BUS_STATE_ERROR_WARNING, "CAN state enum mismatch");
_Static_assert((int)CAN_STATE_ERROR_PASSIVE == (int)CAN_BUS_STATE_ERROR_PASSIVE, "CAN state enum mismatch");
_Static_assert((int)CAN_STATE_BUS_OFF       == (int)CAN_BUS_STATE_BUS_OFF,       "CAN state enum mismatch");
_Static_assert((int)CAN_STATE_STOPPED       == (int)CAN_BUS_STATE_STOPPED,       "CAN state enum mismatch");

/**
 * @brief RX message queue, filled by the CAN driver ISR for matching frames.
 *
 * Drained by the CAN worker thread once per CAN_TX_PERIOD_MS cycle.
 */
CAN_MSGQ_DEFINE(s_can_rx_msgq, CAN_RX_MSGQ_DEPTH);

/**
 * @brief Accumulator for decoded RX signal values.
 *
 * Only touched by the CAN worker thread, so it needs no lock.  Holds the
 * latest decoded value of every signal and is published as a whole after each
 * RX drain, so a cycle in which only some of the RX messages arrived keeps the
 * previous values of the others instead of zeroing them.
 */
static struct can_data_snapshot s_rx_snapshot;


/* ── Private Function Prototypes ─────────────────────────────────────────────────────────────── */

static int  can_hw_init(void);
static void can_send_dcu2_mabx(uint8_t drive_mode, bool rtd, uint8_t debug);
static bool can_drain_rx(void);
static void can_thread_fn(void *p1, void *p2, void *p3);


/* ── Private Function Implementations ───────────────────────────────────────────────────────── */

/**
 * @brief Configure the CAN controller, install RX filters, and start the bus.
 *
 * Aborts on the first failing step and leaves the controller stopped, so a
 * partially configured bus is never started.
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
 * The numeric values of enum mission_id are defined to match the DBC encoding
 * directly, so the conversion is a cast.  The function exists to name that
 * assumption at the call site rather than hide it in a cast.
 *
 * @param mission  Mission currently selected in app_state.
 * @return         Raw signal value for DV_Drive_Mode_SETTING.
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
 *      snapshot accumulator.  If anything arrived, stamp it and publish the
 *      full snapshot to can_data_chan.
 *   2. For each TX message: check s_tx_tick % (period_ms / CAN_TX_PERIOD_MS).
 *      If zero, read the current values from their owners and transmit.
 *   3. Poll the controller state and publish it on change.
 *   4. Increment s_tx_tick and sleep for the base tick.
 *
 * TX periods come from can_tx_gen.h (generated from dcu_app.yaml period_ms).
 * Adding a TX message there requires updating only the send_* call below.
 *
 * The cycle is a sleep, not a deadline: the period drifts by whatever the
 * cycle's own work costs.  That is acceptable for a 100 ms status frame and
 * keeps the loop free of timer state.
 *
 * app_state getters and settings_get_all() are mutex-protected internally and
 * safe to call from this thread.
 *
 * The thread returns immediately if hardware initialisation failed, so the
 * rest of the firmware continues to function without CAN.
 *
 * @param p1  Unused.
 * @param p2  Unused.
 * @param p3  Unused.
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
        /*
         * The error counters are read only because can_get_state() requires a
         * destination for them; the state alone drives the reporting.
         *
         * Note that .type stays CAN_STATUS_CONNECTED even when the controller
         * has gone bus-off — the detail travels in .state, which is what the
         * UI evaluates.  As a result the App Layer, which switches on .type,
         * does not currently learn about a bus-off from this path.
         */
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
        /*
         * Hardware failure — the thread is still created, and returns on its
         * first run.  Creating it unconditionally keeps this function's
         * control flow simple and costs only the stack, which is statically
         * allocated either way.
         */
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
