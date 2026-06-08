/**
 * @file        app_state.h
 * @brief       Application state types, read accessors, and write setters
 *
 * @details     Defines the sub-state structs that make up the global application
 *              state and declares the functions used to read and modify it.
 *
 *              Access model
 *              ────────────
 *              The root state struct is opaque — it is defined and allocated only
 *              in app_state.c. All access goes through the functions declared here.
 *
 *              Read accessors (app_state_get_* / app_state_is_*) may be called
 *              from any thread; they acquire an internal mutex before copying.
 *
 *              Write setters (app_state_set_*) are intended exclusively for the
 *              App Layer (src/app/). No other module should call them. This
 *              constraint is enforced by convention; the compiler does not prevent
 *              misuse.
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

#pragma once

/* ── Standard Includes ───────────────────────────────────────────────────────────────────────── */

#include <stdbool.h>
#include <stdint.h>

/* ── Project Includes ────────────────────────────────────────────────────────────────────────── */

#include "services/event_bus/events.h"


/* ── Operating Mode ──────────────────────────────────────────────────────────────────────────── */

/**
 * @brief Application operating modes managed by the state machine.
 *
 * The operating mode controls display behaviour and permitted transitions.
 * It is independent of the selected FS mission (see app_state_mission).
 *
 * DEBUG    — Full navigation; CAN signal monitor; no mission restrictions.
 * PRE_RTD  — Guided pre-drive checklist; navigation restricted to checklist.
 * RTD      — Mission active; live telemetry; mode transitions locked.
 * POST_RTD — Return-to-idle confirmation sequence; then back to DEBUG.
 */
enum operating_mode {
    OPERATING_MODE_DEBUG    = 0,
    OPERATING_MODE_PRE_RTD,
    OPERATING_MODE_RTD,
    OPERATING_MODE_POST_RTD,
};


/* ── Application Sub-State Types ─────────────────────────────────────────────────────────────── */

/**
 * @brief System-level flags and the current operating mode.
 *
 * @note imd_ok and ams_ok default to false (unknown) at startup.
 *       They are set true only after receiving an explicit OK signal over CAN.
 */
struct app_state_system {
    enum operating_mode mode;
    bool                error_active;
    bool                warning_active;
    bool                imd_ok;
    bool                ams_ok;
    bool                ts_active;
};

/**
 * @brief Mission selection and lifecycle state.
 *
 * selected  — The FS mission chosen by the driver. MISSION_NONE until selected.
 * active    — True while the mission is running (operating mode == RTD).
 * locked    — True once the mission has been confirmed and transmitted to the
 *             vehicle; prevents further changes until POST_RTD is completed.
 */
struct app_state_mission {
    enum mission_id selected;
    bool            active;
    bool            locked;
};

/**
 * @brief CAN bus connectivity and error status.
 *
 * Separated from CAN data so that a single connectivity check does not
 * require copying the full data snapshot.
 */
struct app_state_can_status {
    bool connected;
    bool bus_off;
};

/**
 * @brief Cached application settings relevant to run-time behaviour.
 *
 * These values are populated from the Settings module after boot and
 * updated whenever a SETTINGS_EVT_UPDATED event is received.
 */
struct app_state_settings {
    uint8_t display_brightness; /**< 0–100 %. Default: 80. */
};


/* ── Initialisation ──────────────────────────────────────────────────────────────────────────── */

/**
 * @brief Initialise the application state to default values.
 *
 * Must be called once from main() before any module is started.
 * Sets operating mode to DEBUG and all flags to their safe initial states.
 */
void app_state_init(void);


/* ── Read Accessors ──────────────────────────────────────────────────────────────────────────── */

/* --- Atomic getters (return by value) ------------------------------------------------- */

/** @brief Return the current operating mode. */
enum operating_mode app_state_get_mode(void);

/** @brief Return the ID of the currently active UI screen. */
enum screen_id      app_state_get_active_screen(void);

/** @brief Return the currently selected FS mission. */
enum mission_id     app_state_get_selected_mission(void);

/** @brief Return true if the mission is locked (confirmed and transmitted). */
bool                app_state_is_mission_locked(void);

/** @brief Return true if the traction system is active. */
bool                app_state_is_ts_active(void);

/** @brief Return true if an active error condition is present. */
bool                app_state_is_error_active(void);

/** @brief Return true if the CAN bus is connected and receiving frames. */
bool                app_state_is_can_connected(void);

/* --- Struct-level getters (copy to caller buffer) ------------------------------------- */

/**
 * @brief Copy the system sub-state into @p out.
 * @param out  Non-null pointer to the destination struct.
 */
void app_state_get_system(struct app_state_system *out);

/**
 * @brief Copy the mission sub-state into @p out.
 * @param out  Non-null pointer to the destination struct.
 */
void app_state_get_mission(struct app_state_mission *out);

/**
 * @brief Copy the CAN connectivity status into @p out.
 * @param out  Non-null pointer to the destination struct.
 */
void app_state_get_can_status(struct app_state_can_status *out);

/**
 * @brief Copy the latest decoded CAN data snapshot into @p out.
 * @param out  Non-null pointer to the destination struct.
 */
void app_state_get_can_data(struct can_data_snapshot *out);

/**
 * @brief Copy the cached settings sub-state into @p out.
 * @param out  Non-null pointer to the destination struct.
 */
void app_state_get_settings(struct app_state_settings *out);


/* ── Write Setters (App Layer only) ──────────────────────────────────────────────────────────── */

/*
 * WARNING: The following functions modify the global application state.
 *          They must only be called from within src/app/ (app.c or
 *          state_machine.c). Calling them from any other module violates
 *          the Dirigent architecture contract. See docs/architecture.md.
 */

/** @brief Set the operating mode. */
void app_state_set_mode(enum operating_mode mode);

/** @brief Set the active UI screen. */
void app_state_set_active_screen(enum screen_id screen);

/** @brief Replace the mission sub-state atomically. */
void app_state_set_mission(const struct app_state_mission *mission);

/**
 * @brief Update system error and warning flags.
 * @param error_active    True if a recoverable error is present.
 * @param warning_active  True if a non-critical warning is present.
 */
void app_state_set_system_flags(bool error_active, bool warning_active);

/**
 * @brief Update safety-critical signal states decoded from CAN.
 * @param imd_ok     True if the IMD reports no fault.
 * @param ams_ok     True if the AMS reports no fault.
 * @param ts_active  True if the traction system is active.
 */
void app_state_set_safety_flags(bool imd_ok, bool ams_ok, bool ts_active);

/**
 * @brief Update CAN bus connectivity status.
 * @param connected  True if frames are being received.
 * @param bus_off    True if the controller entered bus-off state.
 */
void app_state_set_can_status(bool connected, bool bus_off);

/**
 * @brief Replace the cached CAN data snapshot with a new one.
 * @param data  Non-null pointer to the snapshot to copy in.
 */
void app_state_update_can_data(const struct can_data_snapshot *data);

/**
 * @brief Replace the cached settings sub-state atomically.
 * @param settings  Non-null pointer to the settings to copy in.
 */
void app_state_set_settings(const struct app_state_settings *settings);
