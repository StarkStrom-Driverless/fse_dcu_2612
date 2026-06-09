/**
 * @file        can.c
 * @brief       CAN module — frame TX, bus management, and status reporting
 *
 * @details     Owns the CAN controller hardware instance. Responds to
 *              can_tx_cmd_chan commands from the App Layer by packing and
 *              transmitting CAN frames according to the vehicle DBC.
 *
 *              Periodic TX
 *              ───────────
 *              A dedicated worker thread (priority 3, stack 1024 B) sends
 *              DCU_2_mABX (0x196) every 100 ms.  On each cycle the thread
 *              reads the current mission and operating mode directly from
 *              app_state (the single source of truth) and packs them into
 *              the outgoing frame.  No local state copy is maintained.
 *
 *                drive_mode = mission_to_drive_mode(app_state_get_selected_mission())
 *                rtd_active = (app_state_get_mode() == OPERATING_MODE_RTD)
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

#include "modules/can/can_signals.h"
#include "app/app_state.h"
#include "services/event_bus/event_bus.h"
#include "services/event_bus/events.h"

/* ── Zephyr Logging ──────────────────────────────────────────────────────────────────────────── */

LOG_MODULE_REGISTER(can_module, CONFIG_LOG_DEFAULT_LEVEL);


/* ── Private Macros & Constants ──────────────────────────────────────────────────────────────── */

/** @brief CAN bus bitrate. Must match the vehicle network configuration. */
#define CAN_BITRATE_BPS         1000000U

/** @brief Timeout for a single CAN TX attempt. */
#define CAN_TX_TIMEOUT          K_MSEC(100)

/** @brief Thread stack size for the CAN worker thread. */
#define CAN_THREAD_STACK_SIZE   1024U

/** @brief Thread scheduling priority for the CAN worker (higher = more urgent). */
#define CAN_THREAD_PRIORITY     3


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


/* ── Private Function Prototypes ─────────────────────────────────────────────────────────────── */

static int  can_hw_init(void);
static void can_send_dcu2_mabx(uint8_t drive_mode, bool rtd, uint8_t debug);
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

    LOG_INF("CAN controller ready (%s, %u bps)", s_can_dev->name, CAN_BITRATE_BPS);
    return 0;
}

/**
 * @brief Build and transmit a DCU_2_mABX frame.
 *
 * Packs @p drive_mode and @p rtd into the 3-byte payload according to the
 * DBC signal layout defined in can_signals.h, then calls can_send().
 *
 * Logs an error if the frame cannot be delivered within CAN_TX_TIMEOUT.
 *
 * @param drive_mode  DV_Drive_Mode_SETTING raw value (0–7).
 * @param rtd         true → RTD_Button = 1 (Ready-to-Drive request).
 */
static void can_send_dcu2_mabx(uint8_t drive_mode, bool rtd, uint8_t debug)
{
    struct can_frame frame = {
        .id    = DCU2_MABX_CAN_ID,
        .dlc   = DCU2_MABX_DLC,
        .flags = 0,   /* standard (11-bit) frame, no CAN-FD */
    };

    DCU2_MABX_PACK(frame.data, drive_mode, rtd, debug);

    int ret = can_send(s_can_dev, &frame, CAN_TX_TIMEOUT, NULL, NULL);
    if (ret != 0) {
        LOG_ERR("TX 0x%03X failed: %d (drive_mode=%u rtd=%d debug=%u)",
                DCU2_MABX_CAN_ID, ret, drive_mode, (int)rtd, debug);
    } else {
        LOG_DBG("TX 0x%03X  drive_mode=%u  rtd=%d  debug=%u",
                DCU2_MABX_CAN_ID, drive_mode, (int)rtd, debug);
    }
}

/** @brief TX period: DCU_2_mABX is sent cyclically at this interval. */
#define CAN_TX_PERIOD_MS    100U

/**
 * @brief CAN worker thread entry point.
 *
 * Runs a fixed 100 ms send cycle:
 *
 *   1. Read drive_mode and RTD flag directly from app_state — the single
 *      source of truth.  No local copy is kept; every cycle reflects the
 *      latest state set by the App Layer.
 *   2. Transmit DCU_2_mABX with the current values.
 *   3. Sleep for the remainder of the 100 ms period.
 *
 * app_state_get_selected_mission() and app_state_get_mode() are thread-safe
 * (mutex-protected inside app_state.c) so they are safe to call here.
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
        /* ── 1. Read current state from the App Layer ────────────────── */
        uint8_t drive_mode = mission_to_drive_mode(app_state_get_selected_mission());
        bool    rtd_active = (app_state_get_mode() == OPERATING_MODE_RTD);
        uint8_t debug      = app_state_get_debug_bits();

        /* ── 2. Transmit ─────────────────────────────────────────────── */
        can_send_dcu2_mabx(drive_mode, rtd_active, debug);

        /* ── 3. Wait for next 100 ms slot ────────────────────────────── */
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

        /* Publish initial connected status to the event bus. */
        struct can_status_event status_evt = {
            .type   = CAN_STATUS_CONNECTED,
            .msg_id = 0U,
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
