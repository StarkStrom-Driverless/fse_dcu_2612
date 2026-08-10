/**
 * @file        event_bus.h
 * @brief       Zbus channel declarations for the FSE DCU 2612 event bus
 *
 * @details     Declares all Zbus channels used in the firmware. Include this
 *              header (together with events.h) in any module that publishes
 *              to or subscribes from a channel.
 *
 *              Channel directions:
 *                Upward   (Module → App) : can_status_chan, can_data_chan,
 *                                          ui_input_chan, settings_chan,
 *                                          feedback_chan
 *                Downward (App → Module) : ui_cmd_chan, lighting_cmd_chan,
 *                                          audio_cmd_chan, can_tx_cmd_chan
 *
 *              Subscribers register themselves using ZBUS_CHAN_ADD_OBS in
 *              their own source files. See docs/event_system.md for the
 *              complete subscriber model.
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

#ifndef SERVICES_EVENT_BUS_EVENT_BUS_H
#define SERVICES_EVENT_BUS_EVENT_BUS_H

/* ── Zephyr Includes ─────────────────────────────────────────────────────────────────────────── */

#include <zephyr/zbus/zbus.h>

/* ── Project Includes ────────────────────────────────────────────────────────────────────────── */

#include "services/event_bus/events.h"


/* ── Upward Channel Declarations: Module → App ───────────────────────────────────────────────── */

/** CAN connectivity and bus error events (published by CAN module). */
ZBUS_CHAN_DECLARE(can_status_chan);

/** Periodic decoded CAN signal snapshot (published by CAN module). */
ZBUS_CHAN_DECLARE(can_data_chan);

/** Semantic driver input events (published by UI module). */
ZBUS_CHAN_DECLARE(ui_input_chan);

/** Settings lifecycle events (published by Settings module). */
ZBUS_CHAN_DECLARE(settings_chan);

/** Effect completion signals from Lighting and Audio modules. */
ZBUS_CHAN_DECLARE(feedback_chan);


/* ── Downward Channel Declarations: App → Module ─────────────────────────────────────────────── */

/** Screen navigation and data update commands (published by App, consumed by UI). */
ZBUS_CHAN_DECLARE(ui_cmd_chan);

/** LED zone state and animation effect commands (published by App, consumed by Lighting). */
ZBUS_CHAN_DECLARE(lighting_cmd_chan);

/** Sound effect commands (published by App, consumed by Audio). */
ZBUS_CHAN_DECLARE(audio_cmd_chan);

/** CAN frame transmit requests (published by App, consumed by CAN module). */
ZBUS_CHAN_DECLARE(can_tx_cmd_chan);

#endif /* SERVICES_EVENT_BUS_EVENT_BUS_H */
