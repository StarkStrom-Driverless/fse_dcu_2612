/**
 * @file        events.h
 * @brief       Zbus event and command payload type definitions
 *
 * @details     Central header for all Zbus channel message types used in the
 *              FSE DCU 2612 firmware. Includes both upward event structs
 *              (Module → App) and downward command structs (App → Module).
 *
 *              This header has no dependency on Zephyr or LVGL APIs and may
 *              be included by any module that publishes or subscribes to a
 *              Zbus channel. For the channel objects themselves, include
 *              "services/event_bus/event_bus.h".
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

#ifndef SERVICES_EVENT_BUS_EVENTS_H
#define SERVICES_EVENT_BUS_EVENTS_H

/* ── Standard Includes ───────────────────────────────────────────────────────────────────────── */

#include <stdint.h>
#include <stdbool.h>


/* ── Common Enumerations ─────────────────────────────────────────────────────────────────────── */

/**
 * @brief Formula Student mission disciplines.
 *
 * Sent to the vehicle via CAN when the driver selects a mission.
 * MISSION_NONE is the initial / unselected state.
 */
enum mission_id {
    MISSION_NONE           = 0,
    MISSION_ACCELERATION,
    MISSION_SKIDPAD,
    MISSION_AUTOCROSS,
    MISSION_ENDURANCE,
    MISSION_INSPECTION,
    MISSION_MANUAL_DRIVING,
};

/**
 * @brief UI screen identifiers used by the App Layer to navigate the display.
 *
 * SCREEN_NONE is the initial state before the first screen is created.
 *
 * Carousel order (left encoder, left → right):
 *   [SCREEN_MISSION_SELECT] ←← [SCREEN_BOOT] ··· (future screens)
 */
enum screen_id {
    SCREEN_NONE          = 0,
    SCREEN_DEBUG_HV_ACCU,     /**< Show debug values for High Voltage Accu         */
    SCREEN_DEBUG_LV_ACCU,     /**< Show debug values for Low Voltage Accu          */
    SCREEN_DEBUG_PRESSURE,    /**< Show debug values of air and brake pressure     */
    SCREEN_DEBUG_TS,          /**< Show debug values for tractive system           */
    SCREEN_DEBUG_WRITE,       /**< Send generic debug Bits.                        */
    SCREEN_BOOT,              /**< Initial splash; starting position in carousel.  */
    SCREEN_MISSION_SELECT,    /**< Mission roller + OK + RTD buttons.              */
    SCREEN_PRE_RTD,           /**< Pre-drive checklist (future).                   */
    SCREEN_RTD,               /**< Live telemetry during mission.                  */
    SCREEN_EV_DRIVING,        /**< Show telemetry and adjust vehicle in EV driving */
    SCREEN_POST_RTD,          /**< Return-to-idle confirmation (future).           */
    SCREEN_ERROR,             /**< Safety fault overlay (future).                  */
};


/* ── Upward Channels: Module → App ──────────────────────────────────────────────────────────── */

/* ---- can_status_chan ------------------------------------------------------------------ */

/** @brief CAN connectivity and bus error event types. */
enum can_status_type {
    CAN_STATUS_DISCONNECTED = 0, /**< Default / initial state.                          */
    CAN_STATUS_CONNECTED,        /**< CAN bus communication established.                */
    CAN_STATUS_TIMEOUT,          /**< A message has not been received within its window.*/
    CAN_STATUS_BUS_OFF,          /**< CAN controller entered bus-off state.             */
};

/** @brief Payload for can_status_chan. */
struct can_status_event {
    enum can_status_type type;
    uint32_t             msg_id; /**< Non-zero only for CAN_STATUS_TIMEOUT. */
};

/* ---- can_data_chan -------------------------------------------------------------------- */

/*
 * struct can_data_snapshot is generated from dbc/dcu_app.yaml — the field
 * list follows the app_name mappings there.  Published by the CAN module
 * at ~100 ms; forwarded to the UI module via ui_cmd_chan by the App Layer.
 *
 * To add a signal: edit dbc/dcu_app.yaml, then run
 *   python3 tools/codegen/gen_can.py
 */
#include "generated/can_data_gen.h"

/* ---- ui_input_chan -------------------------------------------------------------------- */

/**
 * @brief Semantic driver input event types produced by the UI module.
 *
 * Physical button mapping
 * ───────────────────────
 *  ESC button       →  UI_INPUT_BACK
 *  OK  button       →  UI_INPUT_CONFIRM  (or triggers focused LVGL widget)
 *  RTD button       →  UI_INPUT_RTD_REQUEST
 *  Timestamp button →  UI_INPUT_TIMESTAMP
 *
 * Encoder mapping
 * ───────────────
 *  Left  encoder    →  screen carousel navigation (handled inside ui.c,
 *                       not published to this channel)
 *  Right encoder    →  in-screen widget navigation via LVGL group
 */
enum ui_input_type {
    UI_INPUT_CONFIRM          = 0, /**< OK button: confirm focused widget.         */
    UI_INPUT_BACK,                 /**< ESC button: cancel / go back.              */
    UI_INPUT_ENCODER_UP,           /**< Right encoder: clockwise (spare/fallback). */
    UI_INPUT_ENCODER_DOWN,         /**< Right encoder: CCW (spare/fallback).       */
    UI_INPUT_ENCODER_CLICK,        /**< Right encoder button press.                */
    UI_INPUT_MISSION_SELECTED,     /**< Driver confirmed a mission via OK button.  */
    UI_INPUT_RTD_REQUEST,          /**< RTD button pressed: request Ready-to-Drive.*/
    UI_INPUT_TORQUE_VECT_ON,       /**< Driver activated Torque Vectoring          */
    UI_INPUT_TORQUE_VECT_OFF,      /**< Driver disabled Torque Vectoring          */
    UI_INPUT_DEBUG_BITS_SELECTED,  /**< Engineer set debug bits via OK button.     */
    UI_INPUT_TIMESTAMP,            /**< Timestamp button pressed: log event marker.*/
};

/** @brief Payload for ui_input_chan. */
struct ui_input_event {
    enum ui_input_type type;
    union {
        /** Valid when type == UI_INPUT_MISSION_SELECTED. */
        enum mission_id mission;
        /**
         * Valid when type == UI_INPUT_DEBUG_BITS_SELECTED.
         * Raw 3-bit value (0–7) for the Debug_SETTING CAN signal.
         */
        uint8_t debug_bits;
    } data;
};

/* ---- safety_chan ---------------------------------------------------------------------- */

/** @brief Safety-critical event types decoded from CAN or GPIO. */
enum safety_event_type {
    SAFETY_EVT_IMD_FAULT       = 0,
    SAFETY_EVT_AMS_FAULT,
    SAFETY_EVT_SHUTDOWN_OPEN,
    SAFETY_EVT_SHUTDOWN_CLOSED,
    SAFETY_EVT_TS_OFF,
    SAFETY_EVT_TS_ACTIVE,
};

/** @brief Payload for safety_chan. */
struct safety_event {
    enum safety_event_type type;
};

/* ---- settings_chan -------------------------------------------------------------------- */

/** @brief Settings lifecycle event types. */
enum settings_event_type {
    SETTINGS_EVT_LOADED        = 0, /**< Initial NVM load completed on boot. */
    SETTINGS_EVT_UPDATED,           /**< A setting was changed and persisted. */
    SETTINGS_EVT_FACTORY_RESET,     /**< All settings cleared to defaults.   */
};

/** @brief Payload for settings_chan. */
struct settings_event {
    enum settings_event_type type;
};

/* ---- feedback_chan -------------------------------------------------------------------- */

/** @brief Effect completion signals from Lighting and Audio modules. */
enum feedback_type {
    FEEDBACK_LIGHTING_DONE = 0, /**< Transient lighting effect finished. */
    FEEDBACK_AUDIO_DONE,        /**< Transient audio effect finished.    */
};

/** @brief Payload for feedback_chan. */
struct feedback_event {
    enum feedback_type type;
};


/* ── Downward Channels: App → Module ────────────────────────────────────────────────────────── */

/* ---- ui_cmd_chan ---------------------------------------------------------------------- */

/** @brief Command types for the UI module. */
enum ui_cmd_type {
    UI_CMD_SET_SCREEN   = 0, /**< Navigate to a named screen.                     */
    UI_CMD_UPDATE_DATA,      /**< Push a new CAN data snapshot for rendering.      */
    UI_CMD_SET_STATUS,       /**< Update connection and safety indicator flags.    */
};

/** @brief Status flags rendered on all screens as persistent indicators. */
struct ui_status_flags {
    bool can_connected;
    bool safety_fault;
    bool ts_active;
};

/** @brief Payload for ui_cmd_chan. */
struct ui_cmd {
    enum ui_cmd_type type;
    union {
        enum screen_id           screen;   /**< UI_CMD_SET_SCREEN  */
        struct can_data_snapshot snapshot; /**< UI_CMD_UPDATE_DATA */
        struct ui_status_flags   status;   /**< UI_CMD_SET_STATUS  */
    } data;
};

/* ---- lighting_cmd_chan ---------------------------------------------------------------- */

/**
 * @brief Physical LED zone identifiers.
 *
 * The APA102 strip is divided into three semantic zones. LIGHTING_ZONE_ALL
 * broadcasts a command to all zones simultaneously.
 *
 *  LEDs  0 ..  9  →  ZONE_LEFT   (HV Temperature progress bar)
 *  LEDs 10 .. 12  →  ZONE_CENTER (TS Off / AMS / IMD status indicators)
 *  LEDs 13 .. 22  →  ZONE_RIGHT  (HV SoC progress bar)
 */
enum lighting_zone_id {
    LIGHTING_ZONE_LEFT   = 0,
    LIGHTING_ZONE_CENTER,
    LIGHTING_ZONE_RIGHT,
    LIGHTING_ZONE_ALL,
};

/**
 * @brief Lighting render layer / priority.
 *
 * Each zone independently maintains all three layers.
 * Rendering priority: OVERRIDE > EFFECT > BASE.
 *
 * BASE    – Persistent state; stays until explicitly replaced.
 * EFFECT  – Transient animation; zone reverts to BASE when finished.
 * OVERRIDE– Safety-critical; cannot be interrupted by BASE or EFFECT commands.
 *           Must be explicitly cleared via LIGHTING_CMD_CLEAR_OVERRIDE.
 */
enum lighting_layer {
    LIGHTING_LAYER_BASE     = 0,
    LIGHTING_LAYER_EFFECT,
    LIGHTING_LAYER_OVERRIDE,
};

/** @brief Persistent visual states for the BASE layer. */
enum lighting_state_id {
    LIGHTING_STATE_OFF          = 0,
    LIGHTING_STATE_IDLE,
    LIGHTING_STATE_PROGRESS,    /**< Progress bar; level set by value_pct. */
    LIGHTING_STATE_STATUS_OK,
    LIGHTING_STATE_STATUS_WARN,
    LIGHTING_STATE_STATUS_FAULT,
    LIGHTING_STATE_RTD,
};

/** @brief Named animation effects for the EFFECT and OVERRIDE layers. */
enum lighting_effect_id {
    LIGHTING_EFFECT_NONE         = 0,
    LIGHTING_EFFECT_CONFIRM,     /**< Single green flash (~200 ms).          */
    LIGHTING_EFFECT_ABORT,       /**< Double red flash.                      */
    LIGHTING_EFFECT_STARTUP,     /**< Sequential LED sweep on boot.          */
    LIGHTING_EFFECT_SAFETY_FAULT,/**< Fast red strobe; loops until cleared.  */
};

/** @brief Command types for the Lighting module. */
enum lighting_cmd_type {
    LIGHTING_CMD_SET_STATE      = 0, /**< Set persistent BASE state for a zone.    */
    LIGHTING_CMD_PLAY_EFFECT,        /**< Play transient effect on a zone.         */
    LIGHTING_CMD_STOP_EFFECT,        /**< Stop active effect; revert to BASE.      */
    LIGHTING_CMD_CLEAR_OVERRIDE,     /**< Release OVERRIDE; return to EFFECT/BASE. */
    LIGHTING_CMD_CLEAR_ALL,          /**< Reset all zones to LIGHTING_STATE_OFF.   */
};

/** @brief Payload for lighting_cmd_chan. */
struct lighting_cmd {
    enum lighting_cmd_type  type;
    enum lighting_zone_id   zone;  /**< Target zone, or LIGHTING_ZONE_ALL. */
    enum lighting_layer     layer;
    union {
        /** Used for LIGHTING_CMD_SET_STATE. */
        struct {
            enum lighting_state_id state;
            uint8_t                value_pct; /**< 0–100 for progress-bar zones. */
        } state;
        /** Used for LIGHTING_CMD_PLAY_EFFECT. */
        struct {
            enum lighting_effect_id effect;
            uint32_t                duration_ms; /**< 0 = loop until stopped. */
        } effect;
    } data;
};

/* ---- audio_cmd_chan ------------------------------------------------------------------- */

/** @brief Named audio effects for the piezo buzzer. */
enum audio_effect_id {
    AUDIO_EFFECT_NONE          = 0,
    AUDIO_EFFECT_CONFIRM,       /**< Single short beep.             */
    AUDIO_EFFECT_ABORT,         /**< Double short beep.             */
    AUDIO_EFFECT_RTD_READY,     /**< Three ascending tones.         */
    AUDIO_EFFECT_SAFETY_FAULT,  /**< Continuous alarm (persistent). */
};

/** @brief Command types for the Audio module. */
enum audio_cmd_type {
    AUDIO_CMD_PLAY_EFFECT = 0, /**< Play a named sound effect.           */
    AUDIO_CMD_STOP,            /**< Stop any active effect immediately.  */
};

/** @brief Payload for audio_cmd_chan. */
struct audio_cmd {
    enum audio_cmd_type  type;
    enum audio_effect_id effect; /**< Valid for AUDIO_CMD_PLAY_EFFECT only. */
};

/* ---- can_tx_cmd_chan ------------------------------------------------------------------ */

/** @brief CAN transmit command types. */
enum can_tx_cmd_type {
    CAN_TX_CMD_SEND_MISSION     = 0, /**< Transmit mission selection frame.  */
    CAN_TX_CMD_SEND_RTD_REQUEST,     /**< Transmit RTD request frame.        */
};

/** @brief Payload for can_tx_cmd_chan. */
struct can_tx_cmd {
    enum can_tx_cmd_type type;
    union {
        enum mission_id mission; /**< Valid for CAN_TX_CMD_SEND_MISSION. */
    } data;
};

#endif /* SERVICES_EVENT_BUS_EVENTS_H */
