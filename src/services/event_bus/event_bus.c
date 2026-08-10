/**
 * @file        event_bus.c
 * @brief       Zbus channel definitions for the FSE DCU 2612 event bus
 *
 * @details     Defines all Zbus channels with their message types and initial
 *              values. Channels are defined with ZBUS_OBSERVERS_EMPTY; each
 *              module registers its own subscriber via ZBUS_CHAN_ADD_OBS in
 *              its own source file.
 *
 *              This file contains no business logic. It is the single
 *              translation unit that allocates channel storage.
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

#include "services/event_bus/event_bus.h"

/* ── Zephyr Includes ─────────────────────────────────────────────────────────────────────────── */

#include <zephyr/zbus/zbus.h>

/* ── Zephyr Logging ──────────────────────────────────────────────────────────────────────────── */

/* No logging in this file — pure channel storage definitions. */


/* ── Upward Channel Definitions: Module → App ────────────────────────────────────────────────── */

ZBUS_CHAN_DEFINE(can_status_chan,
    struct can_status_event,
    NULL,                  /* validator  */
    NULL,                  /* user data  */
    ZBUS_OBSERVERS_EMPTY,
    ZBUS_MSG_INIT(.type = CAN_STATUS_DISCONNECTED, .msg_id = 0U)
);

ZBUS_CHAN_DEFINE(can_data_chan,
    struct can_data_snapshot,
    NULL,
    NULL,
    ZBUS_OBSERVERS_EMPTY,
    ZBUS_MSG_INIT(0)
);

ZBUS_CHAN_DEFINE(ui_input_chan,
    struct ui_input_event,
    NULL,
    NULL,
    ZBUS_OBSERVERS_EMPTY,
    ZBUS_MSG_INIT(.type = UI_INPUT_CONFIRM, .data.mission = MISSION_NONE)
);

ZBUS_CHAN_DEFINE(settings_chan,
    struct settings_event,
    NULL,
    NULL,
    ZBUS_OBSERVERS_EMPTY,
    ZBUS_MSG_INIT(.type = SETTINGS_EVT_LOADED)
);

ZBUS_CHAN_DEFINE(feedback_chan,
    struct feedback_event,
    NULL,
    NULL,
    ZBUS_OBSERVERS_EMPTY,
    ZBUS_MSG_INIT(.type = FEEDBACK_LIGHTING_DONE)
);


/* ── Downward Channel Definitions: App → Module ──────────────────────────────────────────────── */

ZBUS_CHAN_DEFINE(ui_cmd_chan,
    struct ui_cmd,
    NULL,
    NULL,
    ZBUS_OBSERVERS_EMPTY,
    ZBUS_MSG_INIT(.type = UI_CMD_SET_SCREEN, .data.screen = SCREEN_NONE)
);

ZBUS_CHAN_DEFINE(lighting_cmd_chan,
    struct lighting_cmd,
    NULL,
    NULL,
    ZBUS_OBSERVERS_EMPTY,
    ZBUS_MSG_INIT(.type   = LIGHTING_CMD_CLEAR_ALL,
                  .zone   = LIGHTING_ZONE_ALL,
                  .layer  = LIGHTING_LAYER_BASE)
);

ZBUS_CHAN_DEFINE(audio_cmd_chan,
    struct audio_cmd,
    NULL,
    NULL,
    ZBUS_OBSERVERS_EMPTY,
    ZBUS_MSG_INIT(.type = AUDIO_CMD_STOP, .effect = AUDIO_EFFECT_NONE)
);

ZBUS_CHAN_DEFINE(can_tx_cmd_chan,
    struct can_tx_cmd,
    NULL,
    NULL,
    ZBUS_OBSERVERS_EMPTY,
    ZBUS_MSG_INIT(.type = CAN_TX_CMD_SEND_MISSION,
                  .data.mission = MISSION_NONE)
);
