/**
 * @file        app_state.h
 * @brief       Application state types, read accessors, and write setters
 *
 * @ingroup     dcu_app
 *
 * @details     Defines the sub-state structs that make up the global application
 *              state and declares the functions used to read and modify it.
 *
 *              ### Access model
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
 *              ### What is actually wired up
 *              The state model is broader than the current firmware consumes.
 *              Live paths today:
 *
 *                app.c writes  → mode, mission, CAN status, system flags,
 *                                CAN data snapshot
 *                can.c reads   → app_state_get_selected_mission(),
 *                                app_state_get_mode()
 *
 *              The remaining accessors compile and are correct, but nothing
 *              calls them yet — in particular the safety flags (imd_ok, ams_ok,
 *              ts_active), the active screen, the cached settings and
 *              app_state_set_debug_bits(). Debug bits live in the Settings
 *              service instead (services/settings/settings.h); the copy here is
 *              not fed and must not be read as authoritative.
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

#ifndef APP_APP_STATE_H
#define APP_APP_STATE_H

/* ── Standard Includes ───────────────────────────────────────────────────────────────────────── */

#include <stdbool.h>
#include <stdint.h>

/* ── Project Includes ────────────────────────────────────────────────────────────────────────── */

#include "services/event_bus/events.h"


/* ── Operating Mode ──────────────────────────────────────────────────────────────────────────── */

/**
 * @brief Application operating modes.
 *
 * The mode is independent of the selected FS mission (see app_state_mission).
 * Its one behavioural effect today is the CAN RTD_Button bit, which the CAN
 * module derives from `mode == OPERATING_MODE_RTD` on every TX cycle.
 *
 * Only DEBUG and RTD are reachable: the RTD button toggles between them
 * (see handle_ui_input() in app.c). PRE_RTD and POST_RTD are placeholders for
 * the planned pre-drive checklist and return-to-idle sequences; nothing sets
 * them yet.
 */
enum operating_mode {
    OPERATING_MODE_DEBUG    = 0, /**< Default. Full navigation, RTD_Button = 0.  */
    OPERATING_MODE_PRE_RTD,      /**< Reserved: guided pre-drive checklist.      */
    OPERATING_MODE_RTD,          /**< RTD button held — RTD_Button = 1.          */
    OPERATING_MODE_POST_RTD,     /**< Reserved: return-to-idle confirmation.     */
};


/* ── Application Sub-State Types ─────────────────────────────────────────────────────────────── */

/**
 * @brief System-level flags and the current operating mode.
 *
 * @note imd_ok, ams_ok and ts_active default to false (unknown) at startup and
 *       would only become true through app_state_set_safety_flags(), which no
 *       caller invokes yet. Treat them as unpopulated, not as "all faulty".
 */
struct app_state_system {
    enum operating_mode mode;           /**< Current operating mode.                    */
    bool                error_active;   /**< Recoverable error present (set on bus-off).*/
    bool                warning_active; /**< Non-critical warning present.              */
    bool                imd_ok;         /**< Reserved — not fed yet.                    */
    bool                ams_ok;         /**< Reserved — not fed yet.                    */
    bool                ts_active;      /**< Reserved — not fed yet.                    */
    /**
     * Raw 3-bit value (0–7) for the Debug_SETTING CAN signal.
     *
     * Reserved: the authoritative value lives in the Settings service
     * (SETTING_DEBUG_BITS), which is what the CAN TX path reads. This field
     * is never written by the current firmware.
     */
    uint8_t             debug_bits;
};

/**
 * @brief Mission selection and lifecycle state.
 *
 * Only `selected` is live: the CAN module reads it every TX cycle and packs it
 * into DV_Drive_Mode_SETTING. `active` and `locked` are written as false by the
 * mission-selected handler and never evaluated — the lifecycle they describe
 * belongs to the PRE_RTD/POST_RTD states that do not exist yet.
 */
struct app_state_mission {
    enum mission_id selected; /**< Mission chosen by the driver; MISSION_NONE initially. */
    bool            active;   /**< Reserved: mission running (mode == RTD).              */
    bool            locked;   /**< Reserved: mission confirmed, changes blocked.         */
};

/**
 * @brief CAN bus connectivity and error status.
 *
 * Separated from CAN data so that a single connectivity check does not
 * require copying the full data snapshot.
 */
struct app_state_can_status {
    bool connected; /**< Frames are flowing (set from CAN_STATUS_* events). */
    bool bus_off;   /**< Controller entered bus-off and is silent.          */
};

/**
 * @brief Cached application settings relevant to run-time behaviour.
 *
 * Reserved. The intent is to mirror the values the App Layer needs frequently
 * after a SETTINGS_EVT_LOADED / _UPDATED event, but the settings handler in
 * app.c is still a TODO, so the struct keeps its initial value. Read settings
 * through settings_get() instead.
 */
struct app_state_settings {
    uint8_t display_brightness; /**< 0–100 %. Initial value: 80. */
};


/* ── Initialisation ──────────────────────────────────────────────────────────────────────────── */

/**
 * @brief Initialisation hook for the application state.
 *
 * The defaults themselves come from the static initialiser in app_state.c, so
 * the state is already valid before this runs. The function exists as the
 * documented place to add start-up work that cannot be expressed statically —
 * restoring persisted values, for instance — and currently only logs.
 *
 * Called once by app_module_init() before the App thread starts.
 */
void app_state_init(void);


/* ── Read Accessors ──────────────────────────────────────────────────────────────────────────── */

/* --- Atomic getters (return by value) ------------------------------------------------- */

/** @brief Return the current operating mode. */
enum operating_mode app_state_get_mode(void);

/** @brief Return the ID of the currently active UI screen.
 *  @note Reserved — the UI module tracks the active screen internally and
 *        never publishes it here, so this returns SCREEN_NONE. */
enum screen_id      app_state_get_active_screen(void);

/** @brief Return the currently selected FS mission. */
enum mission_id     app_state_get_selected_mission(void);

/** @brief Return true if the mission is locked (confirmed and transmitted). */
bool                app_state_is_mission_locked(void);

/** @brief Return true if the traction system is active.
 *  @note Reserved — see the note on struct app_state_system. */
bool                app_state_is_ts_active(void);

/** @brief Return true if an active error condition is present. */
bool                app_state_is_error_active(void);

/** @brief Return true if the CAN bus is connected and receiving frames. */
bool                app_state_is_can_connected(void);

/** @brief Return the current Debug_SETTING raw value (0–7).
 *  @note Reserved — always 0. The live value is settings_get(SETTING_DEBUG_BITS). */
uint8_t             app_state_get_debug_bits(void);

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
 *          They must only be called from within src/app/. Calling them from
 *          any other module violates the Dirigent architecture contract —
 *          see the developer manual, "Software architecture".
 *
 *          The rule is a convention, not a compiler-enforced one: the
 *          declarations have to be visible here because app.c includes this
 *          same header.
 */

/**
 * @brief Set the operating mode.
 *
 * The CAN module samples the mode on every TX cycle, so this is what raises
 * and lowers the RTD_Button bit on the bus.
 *
 * @param mode  New operating mode.
 */
void app_state_set_mode(enum operating_mode mode);

/** @brief Set the active UI screen.
 *  @note Reserved — no caller; see app_state_get_active_screen(). */
void app_state_set_active_screen(enum screen_id screen);

/**
 * @brief Replace the mission sub-state atomically.
 * @param mission  Non-null pointer to the mission state to copy in.
 */
void app_state_set_mission(const struct app_state_mission *mission);

/**
 * @brief Update system error and warning flags.
 * @param error_active    True if a recoverable error is present.
 * @param warning_active  True if a non-critical warning is present.
 */
void app_state_set_system_flags(bool error_active, bool warning_active);

/**
 * @brief Update safety-critical signal states decoded from CAN.
 *
 * @note Reserved — no caller. The CAN snapshot carries the raw signals, but
 *       nothing derives these flags from it yet.
 *
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
 * @brief Set the Debug_SETTING raw value.
 *
 * @note Reserved — no caller. Use settings_set(SETTING_DEBUG_BITS, …), which
 *       clamps against the generated schema and persists the value.
 *
 * @param bits  3-bit debug value (0–7); values > 7 are clamped to 7.
 */
void app_state_set_debug_bits(uint8_t bits);

/**
 * @brief Replace the cached settings sub-state atomically.
 *
 * @note Reserved — no caller; see struct app_state_settings.
 *
 * @param settings  Non-null pointer to the settings to copy in.
 */
void app_state_set_settings(const struct app_state_settings *settings);

#endif /* APP_APP_STATE_H */
