/**
 * @file        app_state.c
 * @brief       Application state storage, accessors, and write setters
 *
 * @ingroup     dcu_app
 *
 * @details     Owns the single static instance of the root application state.
 *              All reads and writes go through the functions declared in
 *              app_state.h; the root struct is opaque to all other translation
 *              units.
 *
 *              ### Thread safety
 *              A k_mutex serialises concurrent access, and it is needed: the
 *              app thread (priority 5) writes while the CAN worker thread
 *              (priority 3) reads mission and mode on every TX cycle, and the
 *              CAN thread preempts the app thread. Contention is still low —
 *              every critical section is a plain struct copy with no blocking
 *              call inside — so K_FOREVER cannot deadlock here.
 *
 *              Getters copy under the lock and return by value, so a caller
 *              never holds a reference into the shared state.
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

#include "app/app_state.h"

/* ── Zephyr Includes ─────────────────────────────────────────────────────────────────────────── */

#include <zephyr/kernel.h>
#include <zephyr/sys/__assert.h>
#include <zephyr/logging/log.h>

/* ── Zephyr Logging ──────────────────────────────────────────────────────────────────────────── */

LOG_MODULE_REGISTER(app_state, CONFIG_LOG_DEFAULT_LEVEL);


/* ── Private Type Definitions ────────────────────────────────────────────────────────────────── */

/**
 * @brief Root application state.
 *
 * This struct is intentionally not exposed in app_state.h. All external
 * access must use the accessor and setter functions.
 */
struct app_state {
    struct app_state_system     system;        /**< Operating mode and system flags.   */
    struct app_state_mission    mission;       /**< Selected mission and lifecycle.    */
    enum   screen_id            active_screen; /**< Reserved; see app_state.h.         */
    struct app_state_can_status can_status;    /**< CAN connectivity.                  */
    struct can_data_snapshot    can_data;      /**< Latest decoded CAN signal values.  */
    struct app_state_settings   settings;      /**< Reserved; see app_state.h.         */
};


/* ── Private Variables ───────────────────────────────────────────────────────────────────────── */

/** @brief Guards every access to s_state. Held only for plain struct copies. */
K_MUTEX_DEFINE(s_mutex);

/**
 * @brief Global application state instance with safe initial values.
 *
 * This initialiser — not app_state_init() — is what establishes the defaults:
 * operating mode DEBUG, no mission, CAN disconnected, all flags false
 * (i.e. "unknown", never "confirmed OK"), display brightness 80 %.
 */
static struct app_state s_state = {
    .system = {
        .mode           = OPERATING_MODE_DEBUG,
        .error_active   = false,
        .warning_active = false,
        .imd_ok         = false,
        .ams_ok         = false,
        .ts_active      = false,
        .debug_bits     = 0U,
    },
    .mission = {
        .selected = MISSION_NONE,
        .active   = false,
        .locked   = false,
    },
    .active_screen = SCREEN_NONE,
    .can_status = {
        .connected = false,
        .bus_off   = false,
    },
    .can_data    = { 0 },
    .settings = {
        .display_brightness = 80U,
    },
};


/* ── Public Function Implementations ─────────────────────────────────────────────────────────── */

void app_state_init(void)
{
    /*
     * The static initialiser above already sets the default values, so there
     * is nothing to reset here.  The function is kept as the explicit hook for
     * start-up work that cannot be expressed statically — loading persisted
     * values, for instance — and to give main()'s init sequence one obvious
     * place to call.
     */
    LOG_INF("Application state initialised (mode=DEBUG)");
}

/* --- Atomic getters ------------------------------------------------------------------- */

enum operating_mode app_state_get_mode(void)
{
    k_mutex_lock(&s_mutex, K_FOREVER);
    enum operating_mode mode = s_state.system.mode;
    k_mutex_unlock(&s_mutex);
    return mode;
}

enum screen_id app_state_get_active_screen(void)
{
    k_mutex_lock(&s_mutex, K_FOREVER);
    enum screen_id screen = s_state.active_screen;
    k_mutex_unlock(&s_mutex);
    return screen;
}

enum mission_id app_state_get_selected_mission(void)
{
    k_mutex_lock(&s_mutex, K_FOREVER);
    enum mission_id mission = s_state.mission.selected;
    k_mutex_unlock(&s_mutex);
    return mission;
}

bool app_state_is_mission_locked(void)
{
    k_mutex_lock(&s_mutex, K_FOREVER);
    bool locked = s_state.mission.locked;
    k_mutex_unlock(&s_mutex);
    return locked;
}

bool app_state_is_ts_active(void)
{
    k_mutex_lock(&s_mutex, K_FOREVER);
    bool ts_active = s_state.system.ts_active;
    k_mutex_unlock(&s_mutex);
    return ts_active;
}

bool app_state_is_error_active(void)
{
    k_mutex_lock(&s_mutex, K_FOREVER);
    bool error = s_state.system.error_active;
    k_mutex_unlock(&s_mutex);
    return error;
}

bool app_state_is_can_connected(void)
{
    k_mutex_lock(&s_mutex, K_FOREVER);
    bool connected = s_state.can_status.connected;
    k_mutex_unlock(&s_mutex);
    return connected;
}

uint8_t app_state_get_debug_bits(void)
{
    k_mutex_lock(&s_mutex, K_FOREVER);
    uint8_t bits = s_state.system.debug_bits;
    k_mutex_unlock(&s_mutex);
    return bits;
}

/* --- Struct-level getters ------------------------------------------------------------- */

void app_state_get_system(struct app_state_system *out)
{
    __ASSERT_NO_MSG(out != NULL);
    k_mutex_lock(&s_mutex, K_FOREVER);
    *out = s_state.system;
    k_mutex_unlock(&s_mutex);
}

void app_state_get_mission(struct app_state_mission *out)
{
    __ASSERT_NO_MSG(out != NULL);
    k_mutex_lock(&s_mutex, K_FOREVER);
    *out = s_state.mission;
    k_mutex_unlock(&s_mutex);
}

void app_state_get_can_status(struct app_state_can_status *out)
{
    __ASSERT_NO_MSG(out != NULL);
    k_mutex_lock(&s_mutex, K_FOREVER);
    *out = s_state.can_status;
    k_mutex_unlock(&s_mutex);
}

void app_state_get_can_data(struct can_data_snapshot *out)
{
    __ASSERT_NO_MSG(out != NULL);
    k_mutex_lock(&s_mutex, K_FOREVER);
    *out = s_state.can_data;
    k_mutex_unlock(&s_mutex);
}

void app_state_get_settings(struct app_state_settings *out)
{
    __ASSERT_NO_MSG(out != NULL);
    k_mutex_lock(&s_mutex, K_FOREVER);
    *out = s_state.settings;
    k_mutex_unlock(&s_mutex);
}

/* --- Write setters (App Layer only) --------------------------------------------------- */

void app_state_set_mode(enum operating_mode mode)
{
    k_mutex_lock(&s_mutex, K_FOREVER);
    s_state.system.mode = mode;
    k_mutex_unlock(&s_mutex);
    LOG_DBG("Operating mode → %d", (int)mode);
}

void app_state_set_active_screen(enum screen_id screen)
{
    k_mutex_lock(&s_mutex, K_FOREVER);
    s_state.active_screen = screen;
    k_mutex_unlock(&s_mutex);
}

void app_state_set_mission(const struct app_state_mission *mission)
{
    __ASSERT_NO_MSG(mission != NULL);
    k_mutex_lock(&s_mutex, K_FOREVER);
    s_state.mission = *mission;
    k_mutex_unlock(&s_mutex);
}

void app_state_set_system_flags(bool error_active, bool warning_active)
{
    k_mutex_lock(&s_mutex, K_FOREVER);
    s_state.system.error_active   = error_active;
    s_state.system.warning_active = warning_active;
    k_mutex_unlock(&s_mutex);
}

void app_state_set_safety_flags(bool imd_ok, bool ams_ok, bool ts_active)
{
    k_mutex_lock(&s_mutex, K_FOREVER);
    s_state.system.imd_ok    = imd_ok;
    s_state.system.ams_ok    = ams_ok;
    s_state.system.ts_active = ts_active;
    k_mutex_unlock(&s_mutex);
}

void app_state_set_can_status(bool connected, bool bus_off)
{
    k_mutex_lock(&s_mutex, K_FOREVER);
    s_state.can_status.connected = connected;
    s_state.can_status.bus_off   = bus_off;
    k_mutex_unlock(&s_mutex);
}

void app_state_update_can_data(const struct can_data_snapshot *data)
{
    __ASSERT_NO_MSG(data != NULL);
    k_mutex_lock(&s_mutex, K_FOREVER);
    s_state.can_data = *data;
    k_mutex_unlock(&s_mutex);
}

void app_state_set_debug_bits(uint8_t bits)
{
    /* Clamp to 3-bit range (0–7) */
    uint8_t clamped = (bits > 7U) ? 7U : bits;
    k_mutex_lock(&s_mutex, K_FOREVER);
    s_state.system.debug_bits = clamped;
    k_mutex_unlock(&s_mutex);
    LOG_DBG("Debug bits → %u", (unsigned)clamped);
}

void app_state_set_settings(const struct app_state_settings *settings)
{
    __ASSERT_NO_MSG(settings != NULL);
    k_mutex_lock(&s_mutex, K_FOREVER);
    s_state.settings = *settings;
    k_mutex_unlock(&s_mutex);
}
