/**
 * @file        can.c
 * @brief       CAN module — frame TX, bus management, and status reporting
 *
 * @details     Owns the CAN controller hardware instance. Responds to
 *              can_tx_cmd_chan commands from the App Layer by packing and
 *              transmitting CAN frames according to the vehicle DBC.
 *
 *              TX command handling
 *              ───────────────────
 *              CAN_TX_CMD_SEND_MISSION
 *                Updates the internal drive-mode state and transmits
 *                DCU_2_mABX (0x196) with RTD_Button = 0.
 *
 *              CAN_TX_CMD_SEND_RTD_REQUEST
 *                Transmits DCU_2_mABX with the last known drive-mode
 *                and RTD_Button = 1. Mission selection is optional —
 *                SEND_RTD_REQUEST may be issued without a prior
 *                SEND_MISSION; the drive mode will be MISSION_NONE (0).
 *
 *              Bus status
 *              ──────────
 *              can_module_init() publishes CAN_STATUS_CONNECTED after the
 *              controller is started successfully.  A future RX watchdog
 *              will publish CAN_STATUS_TIMEOUT when frames stop arriving.
 *
 *              Thread model
 *              ────────────
 *              A dedicated worker thread (priority 3, stack 1024 B) blocks
 *              on the Zbus subscriber message queue.  The thread is spawned
 *              inside can_module_init() so that hardware initialisation is
 *              guaranteed to happen before the first Zbus message can arrive.
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

/**
 * @brief Last drive mode transmitted (or pending transmission).
 *
 * Updated whenever CAN_TX_CMD_SEND_MISSION is received.  Used as the
 * DV_Drive_Mode_SETTING field in all subsequent DCU_2_mABX frames.
 * Defaults to 0 (MISSION_NONE) so that RTD can be sent without a prior
 * mission selection.
 */
static uint8_t s_current_drive_mode;

/** @brief Thread stack storage for the CAN worker thread. */
static K_THREAD_STACK_DEFINE(s_can_stack, CAN_THREAD_STACK_SIZE);

/** @brief Thread control block for the CAN worker thread. */
static struct k_thread s_can_thread;

/** @brief Flag set after hardware initialisation completes successfully. */
static bool s_hw_ready;


/* ── Zbus Subscriber ─────────────────────────────────────────────────────────────────────────── */

/**
 * @brief Subscriber for can_tx_cmd_chan.
 *
 * Message queue depth of 4 provides a small buffer for burst commands
 * while keeping memory usage minimal on a constrained target.
 */
ZBUS_SUBSCRIBER_DEFINE(can_tx_sub, 4);

/**
 * @brief Register can_tx_sub as an observer of can_tx_cmd_chan.
 *
 * Priority 0 means this module is notified before any lower-priority
 * observers on the same channel.
 */
ZBUS_CHAN_ADD_OBS(can_tx_cmd_chan, can_tx_sub, 0);


/* ── Private Function Prototypes ─────────────────────────────────────────────────────────────── */

static int  can_hw_init(void);
static void can_send_dcu2_mabx(uint8_t drive_mode, bool rtd);
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
static void can_send_dcu2_mabx(uint8_t drive_mode, bool rtd)
{
    struct can_frame frame = {
        .id    = DCU2_MABX_CAN_ID,
        .dlc   = DCU2_MABX_DLC,
        .flags = 0,   /* standard (11-bit) frame, no CAN-FD */
    };

    DCU2_MABX_PACK(frame.data, drive_mode, rtd);

    int ret = can_send(s_can_dev, &frame, CAN_TX_TIMEOUT, NULL, NULL);
    if (ret != 0) {
        LOG_ERR("TX 0x%03X failed: %d (drive_mode=%u rtd=%d)",
                DCU2_MABX_CAN_ID, ret, drive_mode, (int)rtd);
    } else {
        LOG_INF("TX 0x%03X  drive_mode=%u  rtd=%d",
                DCU2_MABX_CAN_ID, drive_mode, (int)rtd);
    }
}

/**
 * @brief CAN worker thread entry point.
 *
 * Blocks on the Zbus subscriber queue and dispatches incoming
 * can_tx_cmd_chan commands to the appropriate TX helper.
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

    const struct zbus_channel *chan;

    while (true) {
        int rc = zbus_sub_wait(&can_tx_sub, &chan, K_FOREVER);
        if (rc != 0) {
            LOG_ERR("zbus_sub_wait error: %d", rc);
            continue;
        }

        /* Only one channel is watched — assert for safety in debug builds. */
        __ASSERT(chan == &can_tx_cmd_chan, "Unexpected channel");

        struct can_tx_cmd cmd;
        rc = zbus_chan_read(&can_tx_cmd_chan, &cmd, K_NO_WAIT);
        if (rc != 0) {
            LOG_ERR("zbus_chan_read error: %d", rc);
            continue;
        }

        switch (cmd.type) {
        case CAN_TX_CMD_SEND_MISSION:
            s_current_drive_mode = mission_to_drive_mode(cmd.data.mission);
            LOG_INF("Mission set → drive_mode=%u", s_current_drive_mode);
            can_send_dcu2_mabx(s_current_drive_mode, false);
            break;

        case CAN_TX_CMD_SEND_RTD_REQUEST:
            LOG_INF("RTD request → drive_mode=%u", s_current_drive_mode);
            can_send_dcu2_mabx(s_current_drive_mode, true);
            break;

        default:
            LOG_WRN("Unknown can_tx_cmd type: %d", (int)cmd.type);
            break;
        }
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
