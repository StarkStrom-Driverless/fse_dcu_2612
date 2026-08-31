/**
 * @file        events.h
 * @brief       Zbus event and command payload type definitions
 *
 * @ingroup     dcu_event_bus
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
 *              ### Scope note
 *              The type set is deliberately wider than the firmware currently
 *              exercises: it describes the intended protocol, and several
 *              enumerators have no producer yet. Each such case is marked
 *              @c Reserved on the enumerator itself, so a reader can tell an
 *              agreed message apart from a planned one without grepping.
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

#ifndef SERVICES_EVENT_BUS_EVENTS_H
#define SERVICES_EVENT_BUS_EVENTS_H

/* ── Standard Includes ───────────────────────────────────────────────────────────────────────── */

#include <stdint.h>
#include <stdbool.h>


/* ── Common Enumerations ─────────────────────────────────────────────────────────────────────── */

/**
 * @brief Formula Student mission disciplines.
 *
 * The numeric value is the DV_Drive_Mode_SETTING raw value on the bus: the CAN
 * module casts the enum straight through (see mission_to_drive_mode() in
 * can.c), so the order here is a wire format, not a free choice.
 * MISSION_NONE is the initial / unselected state.
 *
 */
enum mission_id {
    MISSION_NONE           = 0,
    MISSION_ACCELERATION,
    MISSION_SKIDPAD,
    MISSION_TRACKDRIVE,
    MISSION_BRAKETEST,
    MISSION_INSPECTION,
    MISSION_AUTOCROSS,
    MISSION_MANUAL_DRIVING,
};

/**
 * @brief UI screen identifiers.
 *
 * Used both as the payload of UI_CMD_SET_SCREEN and as the index into the
 * screen-factory table in ui.c. SCREEN_NONE is the initial state before the
 * first screen is loaded.
 *
 * The order of this enum does not define the carousel; k_carousel[] in ui.c
 * does, and it lists a subset. Two entries here have no factory
 * (SCREEN_RTD, SCREEN_POST_RTD, SCREEN_ERROR): navigating to them logs
 * "screen not implemented" and stays put.
 *
 * @note SCREEN_ERROR must stay last — ui.c sizes its screen array from it.
 */
enum screen_id {
    SCREEN_NONE          = 0,
    SCREEN_DEBUG_HV_ACCU,     /**< Debug values of the high-voltage accumulator.   */
    SCREEN_DEBUG_LV_ACCU,     /**< Debug values of the low-voltage accumulator.    */
    SCREEN_DEBUG_PRESSURE,    /**< Debug values of air and brake pressure.         */
    SCREEN_DEBUG_TS,          /**< Debug values of the tractive system.            */
    SCREEN_DEBUG_CUSTOM,      /**< Read and write generic, unassigned values.      */
    SCREEN_BOOT,              /**< Splash screen; carousel starting position.      */
    SCREEN_MISSION_SELECT,    /**< Mission roller + SET MISSION button.            */
    SCREEN_SDC,               /**< Shutdown-circuit node overview.                 */
    SCREEN_PRE_RTD,           /**< Pre-drive screen; carries the RTD button.       */
    SCREEN_RTD,               /**< Reserved: live telemetry during mission.        */
    SCREEN_EV_DRIVING,        /**< Telemetry plus torque-vectoring / power limit.  */
    SCREEN_POST_RTD,          /**< Reserved: return-to-idle confirmation.          */
    SCREEN_ERROR,             /**< Reserved: safety-fault overlay. Keep last.      */
};


/* ── Upward Channels: Module → App ──────────────────────────────────────────────────────────── */

/* ---- can_status_chan ------------------------------------------------------------------ */

/**
 * @brief CAN connectivity and bus error event types.
 *
 * Only CAN_STATUS_CONNECTED is published today — once after a successful
 * controller start, and again on every bus-state change, with the detail in
 * can_status_event::state. The other three are the initial channel value and
 * reserved for the RX watchdog that is not implemented yet.
 */
enum can_status_type {
    CAN_STATUS_DISCONNECTED = 0, /**< Default / initial channel value.                  */
    CAN_STATUS_CONNECTED,        /**< Controller started; see @c state for detail.      */
    CAN_STATUS_TIMEOUT,          /**< Reserved: message missed its receive window.      */
    CAN_STATUS_BUS_OFF,          /**< Reserved: reported via @c state instead.          */
};

/**
 * @brief CAN controller bus state for UI reporting.
 *
 * Numeric values intentionally match Zephyr's @c enum can_state so that
 * a direct cast is valid in can.c (verified by _Static_assert there).
 */
enum can_bus_state {
    CAN_BUS_STATE_ERROR_ACTIVE  = 0, /**< Normal operation — error counters low.    */
    CAN_BUS_STATE_ERROR_WARNING = 1, /**< Warning — TX or RX error counter ≥ 96.    */
    CAN_BUS_STATE_ERROR_PASSIVE = 2, /**< Error-passive — error counter ≥ 128.      */
    CAN_BUS_STATE_BUS_OFF       = 3, /**< Bus-off — controller silent until recover. */
    CAN_BUS_STATE_STOPPED       = 4, /**< Controller stopped (not started).          */
};

/**
 * @brief Payload for can_status_chan.
 *
 * Two consumers, reading different fields: the App Layer switches on @c type to
 * maintain the connectivity flags, the UI module maps @c state onto the CAN
 * status icon in the header bar.
 */
struct can_status_event {
    enum can_status_type type;       /**< What happened.                            */
    uint32_t             msg_id;     /**< Non-zero only for CAN_STATUS_TIMEOUT.     */
    enum can_bus_state   state;      /**< Current CAN controller bus state.         */
};

/* ---- can_data_chan -------------------------------------------------------------------- */

/*
 * struct can_data_snapshot is generated from dbc/dcu_app.yaml — the field
 * list follows the app_name mappings there.  The CAN module publishes it
 * whenever its 10 ms cycle drained at least one frame, so the rate follows the
 * bus, not a fixed period.  The App Layer stores it in app_state and forwards
 * it to the UI module via ui_cmd_chan.
 *
 * It is an accumulator, not a per-frame message: fields keep their previous
 * value until their own frame arrives.
 *
 * To add a signal: edit dbc/dcu_app.yaml, then run
 *   python3 tools/codegen/gen_can.py
 */
#include "generated/can_data_gen.h"

/* ---- ui_input_chan -------------------------------------------------------------------- */

/**
 * @brief Semantic driver input event types produced by the UI module.
 *
 * These are intents, not key presses. Raw button and encoder events are
 * consumed by LVGL through the input devices and never reach this channel;
 * a screen publishes here only when a widget interaction means something to
 * the vehicle.
 *
 * ### Input routing (see ui.c)
 *  Left  encoder  →  screen carousel, handled entirely inside ui.c
 *  Right encoder  →  focused widget of the active screen's LVGL group
 *  Button pads    →  LVGL keypad indevs bound to per-screen groups
 *
 * ### Who publishes what
 *  UI_INPUT_MISSION_SELECTED     screen_mission_select.c (SET MISSION button)
 *  UI_INPUT_RTD_REQUEST/_RELEASE screen_checklist.c      (RTD button hold)
 *  UI_INPUT_TORQUE_VECT_ON/_OFF  screen_ev_driving.c     (TQ Vect toggle)
 *  UI_INPUT_DEBUG_BITS_SELECTED  screen_debug_custom.c    (SET BITS button)
 */
enum ui_input_type {
    UI_INPUT_CONFIRM          = 0, /**< Reserved. Also the channel's initial value.*/
    UI_INPUT_BACK,                 /**< Reserved: cancel / return to boot screen.  */
    UI_INPUT_ENCODER_UP,           /**< Reserved: spare encoder step.              */
    UI_INPUT_ENCODER_DOWN,         /**< Reserved: spare encoder step.              */
    UI_INPUT_ENCODER_CLICK,        /**< Reserved: encoder button press.            */
    UI_INPUT_MISSION_SELECTED,     /**< Mission confirmed; payload: data.mission.  */
    UI_INPUT_RTD_REQUEST,          /**< RTD button held: raise the RTD signal.     */
    UI_INPUT_RTD_RELEASE,          /**< RTD button released: drop the RTD signal.  */
    UI_INPUT_TORQUE_VECT_ON,       /**< Driver enabled torque vectoring.           */
    UI_INPUT_TORQUE_VECT_OFF,      /**< Driver disabled torque vectoring.          */
    UI_INPUT_DEBUG_BITS_SELECTED,  /**< Debug bits set; payload: data.debug_bits.  */
    UI_INPUT_TIMESTAMP,            /**< Reserved: log an event marker.             */
};

/**
 * @brief Payload for ui_input_chan.
 *
 * The union member is selected by @c type; event types not listed below carry
 * no payload and leave @c data unread.
 *
 * @note Publisher and subscriber must name the same member. Writing one and
 *       reading another happens to work for small values on a little-endian
 *       target, but it is type punning through a union and breaks silently as
 *       soon as either assumption changes.
 */
struct ui_input_event {
    enum ui_input_type type; /**< Which intent; selects the union member. */
    /** Event payload; the active member follows @c type. */
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

/* ---- settings_chan -------------------------------------------------------------------- */

/**
 * @brief Settings lifecycle event types.
 *
 * All three are published by the settings service. The App Layer subscribes but
 * does not act on them yet (see the settings_chan branch in app.c).
 */
enum settings_event_type {
    SETTINGS_EVT_LOADED        = 0, /**< Boot-time load finished — values are usable. */
    SETTINGS_EVT_UPDATED,           /**< A setting changed value.                     */
    SETTINGS_EVT_FACTORY_RESET,     /**< All settings reset to schema defaults.       */
};

/**
 * @brief Payload for settings_chan.
 *
 * Carries no value: subscribers read the new state through settings_get(),
 * which keeps this service the single source of truth.
 */
struct settings_event {
    enum settings_event_type type; /**< What happened to the settings. */
};

/* ---- feedback_chan -------------------------------------------------------------------- */

/**
 * @brief Effect completion signals from Lighting and Audio modules.
 *
 * @note Reserved in full. Neither module reports completion yet, so nothing is
 *       ever published on feedback_chan.
 */
enum feedback_type {
    FEEDBACK_LIGHTING_DONE = 0, /**< Transient lighting effect finished. */
    FEEDBACK_AUDIO_DONE,        /**< Transient audio effect finished.    */
};

/** @brief Payload for feedback_chan. */
struct feedback_event {
    enum feedback_type type; /**< Which effect finished. */
};


/* ── Cross-Module Status: App → All ─────────────────────────────────────────────────────────── */

/* ---- vehicle_status_chan ------------------------------------------------------------- */

/**
 * @brief Header status bar device slot identifiers.
 *
 * Order matches the visual left-to-right icon order in the header; the icon
 * per slot is chosen by k_slot_cfg[] in ui_header.c.
 *
 * Each slot has one LVGL subject in ui.c. Five of them are derived from the
 * CAN snapshot (update_device_status()), UI_DEVICE_CAN from the controller
 * bus state; UI_DEVICE_MABX has no source yet and stays at its initial OK.
 *
 * @note UI_DEVICE_SLOT_COUNT is used as an array size — keep it last.
 */
enum ui_device_slot {
    UI_DEVICE_LOGGER = 0,     /**< Data logger                */
    UI_DEVICE_ROS,            /**< Autonomous Driving ROS     */
    UI_DEVICE_DV_PC,          /**< Autonomous Driving PC      */
    UI_DEVICE_KISTLER,        /**< Kistler measurement system */
    UI_DEVICE_MABX,           /**< MABX control system        */
    UI_DEVICE_SDCS,           /**< Shutdown circuits          */
    UI_DEVICE_CAN,            /**< CAN network                */
    UI_DEVICE_SLOT_COUNT,
};

/**
 * @brief Visual status of a single device slot.
 *
 * Rendered by the header widget (ui_header.c):
 *
 *   OK     → green icon, solid
 *   WARN   → gold icon,  solid
 *   FAULT  → red icon,   solid
 *   ACTIVE → red icon,   blinking at 400 ms half-period
 *
 * ACTIVE means "demands attention", not "healthy": it marks the data logger
 * while recording and the CAN controller while bus-off or stopped.
 */
enum ui_device_status {
    UI_DEVICE_STATUS_OK      = 0,
    UI_DEVICE_STATUS_WARN    = 1,
    UI_DEVICE_STATUS_FAULT   = 2,
    UI_DEVICE_STATUS_ACTIVE  = 3,
};

/**
 * @brief Payload for vehicle_status_chan — the full slot array at once.
 *
 * @note Reserved. The UI module subscribes and would apply it, but no module
 *       publishes on this channel; the status subjects are fed directly inside
 *       ui.c instead.
 */
struct vehicle_status {
    /** State of every header slot, indexed by @ref ui_device_slot. */
    enum ui_device_status slots[UI_DEVICE_SLOT_COUNT];
};


/* ── Downward Channels: App → Module ────────────────────────────────────────────────────────── */

/* ---- ui_cmd_chan ---------------------------------------------------------------------- */

/** @brief Command types for the UI module. */
enum ui_cmd_type {
    UI_CMD_SET_SCREEN   = 0, /**< Navigate to a named screen.                 */
    UI_CMD_UPDATE_DATA,      /**< Push a new CAN data snapshot for rendering. */
};

/**
 * @brief Payload for ui_cmd_chan.
 *
 * The snapshot is carried by value, which makes this the largest message on
 * the bus — sized so the UI thread never dereferences memory owned by another
 * thread.
 */
struct ui_cmd {
    enum ui_cmd_type type; /**< Which command; selects the union member. */
    /** Command payload; the active member follows @c type. */
    union {
        enum screen_id           screen;   /**< UI_CMD_SET_SCREEN  */
        struct can_data_snapshot snapshot; /**< UI_CMD_UPDATE_DATA */
    } data;
};

/* ---- lighting_cmd_chan ---------------------------------------------------------------- */

/**
 * @brief Physical LED zone identifiers.
 *
 * The digital strip is divided into three semantic zones. LIGHTING_ZONE_ALL
 * broadcasts a command to all zones simultaneously.
 *
 *  LEDs  0 ..  9  →  ZONE_LEFT   (HV Temperature progress bar)
 *  LEDs 10 .. 12  →  ZONE_CENTER (TS Off / AMS / IMD status indicators)
 *  LEDs 13 .. 22  →  ZONE_RIGHT  (HV SoC progress bar)
 *
 * @note Reserved, together with the rest of the lighting command protocol
 *       below. The lighting module does not subscribe to lighting_cmd_chan
 *       yet; it renders one fixed animation across the whole strip and knows
 *       nothing about zones, layers, states or effects. Nothing in this
 *       section describes current behaviour.
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
    enum lighting_cmd_type  type;  /**< Which command; selects the union member. */
    enum lighting_zone_id   zone;  /**< Target zone, or LIGHTING_ZONE_ALL. */
    enum lighting_layer     layer; /**< Render layer the command applies to.  */
    /** Command payload; the active member follows @c type. */
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

/**
 * @brief Named audio effects for the piezo buzzer.
 *
 * @note The audio module does not distinguish them: the piezo is driven by a
 *       plain GPIO, so any AUDIO_CMD_PLAY_EFFECT switches it on and
 *       AUDIO_CMD_STOP switches it off. The descriptions below are the
 *       intended sounds once tone generation exists.
 */
enum audio_effect_id {
    AUDIO_EFFECT_NONE          = 0,
    AUDIO_EFFECT_CONFIRM,       /**< Single short beep.             */
    AUDIO_EFFECT_ABORT,         /**< Double short beep.             */
    AUDIO_EFFECT_RTD_READY,     /**< Three ascending tones.         */
    AUDIO_EFFECT_SAFETY_FAULT,  /**< Continuous alarm (persistent). */
};

/** @brief Command types for the Audio module. */
enum audio_cmd_type {
    AUDIO_CMD_PLAY_EFFECT = 0, /**< Sound on.                            */
    AUDIO_CMD_STOP,            /**< Sound off. Also the initial value.   */
};

/**
 * @brief Payload for audio_cmd_chan.
 *
 * Published by the App Layer on every change of the rtd_sound CAN signal.
 */
struct audio_cmd {
    enum audio_cmd_type  type;   /**< Sound on or off. */
    enum audio_effect_id effect; /**< Valid for AUDIO_CMD_PLAY_EFFECT only. */
};

/* ---- can_tx_cmd_chan ------------------------------------------------------------------ */

/**
 * @brief CAN transmit command types.
 *
 * @note Reserved. The design moved away from event-driven transmission: the
 *       CAN module sends DCU_2_mABX on a fixed 100 ms schedule and pulls
 *       mission and mode out of app_state itself, so nothing publishes on
 *       can_tx_cmd_chan and the CAN module does not subscribe to it. Kept for
 *       a future aperiodic frame that genuinely needs to be requested.
 */
enum can_tx_cmd_type {
    CAN_TX_CMD_SEND_MISSION     = 0, /**< Transmit mission selection frame.  */
    CAN_TX_CMD_SEND_RTD_REQUEST,     /**< Transmit RTD request frame.        */
};

/** @brief Payload for can_tx_cmd_chan. */
struct can_tx_cmd {
    enum can_tx_cmd_type type; /**< Which frame to transmit. */
    /** Command payload; the active member follows @c type. */
    union {
        enum mission_id mission; /**< Valid for CAN_TX_CMD_SEND_MISSION. */
    } data;
};

#endif /* SERVICES_EVENT_BUS_EVENTS_H */
