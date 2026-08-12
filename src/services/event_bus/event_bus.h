/**
 * @file        event_bus.h
 * @brief       Zbus channel declarations for the FSE DCU 2612 event bus
 *
 * @ingroup     dcu_event_bus
 *
 * @details     Declares all Zbus channels used in the firmware. Include this
 *              header (together with events.h) in any module that publishes
 *              to or subscribes from a channel.
 *
 *              Channel directions:
 *                Upward      (Module → App)  : can_status_chan, can_data_chan,
 *                                              ui_input_chan, settings_chan,
 *                                              feedback_chan
 *                Cross-module (App → All)    : vehicle_status_chan
 *                Downward    (App → Module)  : ui_cmd_chan, lighting_cmd_chan,
 *                                              audio_cmd_chan, can_tx_cmd_chan
 *
 *              Subscribers register themselves using ZBUS_CHAN_ADD_OBS in
 *              their own source files — the channel definitions in event_bus.c
 *              carry no observer list, so a module can be added or removed
 *              without touching this service.
 *
 *              ### Who is actually attached
 *              | Channel             | Publisher      | Subscriber      |
 *              |---------------------|----------------|-----------------|
 *              | can_status_chan     | CAN            | App, UI         |
 *              | can_data_chan       | CAN            | App             |
 *              | ui_input_chan       | UI screens     | App             |
 *              | settings_chan       | Settings       | App (ignored)   |
 *              | ui_cmd_chan         | App            | UI              |
 *              | audio_cmd_chan      | App            | Audio           |
 *              | feedback_chan       | —              | App (ignored)   |
 *              | vehicle_status_chan | —              | UI              |
 *              | lighting_cmd_chan   | —              | —               |
 *              | can_tx_cmd_chan     | —              | —               |
 *
 *              The four channels without a publisher are declared protocol,
 *              not dead code: they define the interface the corresponding
 *              features will use. See the @c Reserved notes in events.h.
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

#ifndef SERVICES_EVENT_BUS_EVENT_BUS_H
#define SERVICES_EVENT_BUS_EVENT_BUS_H

/* ── Zephyr Includes ─────────────────────────────────────────────────────────────────────────── */

#include <zephyr/zbus/zbus.h>

/* ── Project Includes ────────────────────────────────────────────────────────────────────────── */

#include "services/event_bus/events.h"

/**
 * @defgroup dcu_event_bus Event bus
 * @ingroup  dcu_services
 * @brief Zbus channels and the payload types travelling on them.
 *
 * The only path between modules. Channel storage lives in event_bus.c, the
 * message types in events.h; neither contains vehicle logic.
 * @{
 */


/* ── Upward Channel Declarations: Module → App ───────────────────────────────────────────────── */

/** CAN connectivity and bus error events (published by CAN module). */
ZBUS_CHAN_DECLARE(can_status_chan);

/** Periodic decoded CAN signal snapshot (published by CAN module). */
ZBUS_CHAN_DECLARE(can_data_chan);

/** Semantic driver input events (published by UI module). */
ZBUS_CHAN_DECLARE(ui_input_chan);

/** Settings lifecycle events (published by Settings service). */
ZBUS_CHAN_DECLARE(settings_chan);

/** Effect completion signals from Lighting and Audio. Reserved — no publisher. */
ZBUS_CHAN_DECLARE(feedback_chan);


/* ── Cross-Module Channel Declarations: App → All ───────────────────────────────────────────── */

/** Vehicle device health status. Reserved — the UI subscribes, nobody publishes. */
ZBUS_CHAN_DECLARE(vehicle_status_chan);


/* ── Downward Channel Declarations: App → Module ─────────────────────────────────────────────── */

/** Screen navigation and data update commands (published by App, consumed by UI). */
ZBUS_CHAN_DECLARE(ui_cmd_chan);

/** LED zone state and effect commands. Reserved — Lighting does not subscribe yet. */
ZBUS_CHAN_DECLARE(lighting_cmd_chan);

/** Sound effect commands (published by App, consumed by Audio). */
ZBUS_CHAN_DECLARE(audio_cmd_chan);

/** CAN frame transmit requests. Reserved — no publisher, no subscriber. */
ZBUS_CHAN_DECLARE(can_tx_cmd_chan);

/** @} */ /* dcu_event_bus */

#endif /* SERVICES_EVENT_BUS_EVENT_BUS_H */
