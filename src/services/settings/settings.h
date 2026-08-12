/**
 * @file        settings.h
 * @brief       Persistent settings service — NVS-backed, schema-generated
 *
 * @ingroup     dcu_settings
 *
 * @details     Stores the values that must survive a power cycle.  The set of
 *              settings, their bounds and their defaults are not defined here
 *              — they come from src/generated/settings_schema_gen.h, which the
 *              code generator derives from dbc/dcu_app.yaml.  Adding a setting
 *              therefore never touches this service.
 *
 *              ### Access model
 *              Reads are synchronous and safe from any thread; the values live
 *              in a mutex-protected RAM cache.  Writes update that cache
 *              immediately, schedule a delayed flash write (see below), and
 *              publish SETTINGS_EVT_UPDATED on settings_chan.
 *
 *              There is no settings_cmd_chan — this API is the write
 *              interface.  Per the Dirigent pattern only the App Layer should
 *              call settings_set(); the UI publishes intent on ui_input_chan
 *              and lets the App decide.
 *
 *              ### Source of truth
 *              This service owns the runtime values, not app_state.  The CAN
 *              TX thread reads them from here; the LVGL subjects in
 *              ui_tx_subjects_gen.h are a UI-side mirror only and must never
 *              be read from another thread.  See docs/settings_module.md §2.
 *
 *              ### Flash wear
 *              Writing the current value is a no-op, and changes are coalesced
 *              into a single flash write ~2 s after the last one.  Without
 *              this, sweeping an encoder from 0 to 7 would cost eight writes.
 *              The RAM cache is updated synchronously, so a read never
 *              observes a stale value while a flush is pending.
 *
 *              ### Persistence is currently off
 *              CONFIG_DCU_SETTINGS_PERSIST is @c n in prj.conf, so the flash
 *              path is compiled out entirely: no storage backend, no write
 *              delay, no schema-hash check.  The API and its semantics are
 *              unchanged — values stay authoritative for the runtime and are
 *              still clamped to the schema — but every boot starts from the
 *              defaults.  Enabling the option needs no change in any caller.
 *
 * @author      Mario Wegmann <mario.wegmann@web.de>
 * @date        Created: 2026-08-07
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
 * 0.1.0    2026-08-07  Mario Wegmann   Initial creation
 * ─────────────────────────────────────────────────────────────────────────────────────────────────
 */

#ifndef SERVICES_SETTINGS_SETTINGS_H
#define SERVICES_SETTINGS_SETTINGS_H

/* ── Standard Includes ───────────────────────────────────────────────────────────────────────── */

#include <stdint.h>

/* ── Project Includes ────────────────────────────────────────────────────────────────────────── */

#include "generated/settings_schema_gen.h"

/**
 * @defgroup dcu_settings Settings service
 * @ingroup  dcu_services
 * @brief Runtime-authoritative, optionally flash-backed application settings.
 *
 * The settings themselves are not declared here — the code generator derives
 * them from dbc/dcu_app.yaml into @c settings_schema_gen.h. This service only
 * provides the cache, the clamping and the persistence around them.
 * @{
 */


/* ── Initialisation ──────────────────────────────────────────────────────────────────────────── */

/**
 * @brief Load persisted settings from flash, or fall back to defaults.
 *
 * Must be called once from main() after app_module_init() (the App's Zbus
 * subscriber has to be draining) and before can_module_init() and
 * ui_module_init(), so the first CAN frame and the first screen both see
 * correct values.
 *
 * Never fails hard: if the flash backend is unavailable or the stored data
 * does not match the current schema, the service runs on schema defaults and
 * logs the reason.  SETTINGS_EVT_LOADED is published either way, so a
 * subscriber can treat it as "values are usable now" without inspecting how
 * they were obtained.
 */
void settings_service_init(void);


/* ── Read Accessors ──────────────────────────────────────────────────────────────────────────── */

/**
 * @brief Return the current value of a setting.
 * @param id  Setting identifier from enum setting_id.
 * @return    The current value, or 0 if @p id is out of range.
 */
uint8_t settings_get(enum setting_id id);

/**
 * @brief Copy all settings into @p out with a single mutex acquisition.
 *
 * Intended for the CAN TX path, which needs several values per cycle. Beyond
 * saving lock operations this gives the caller a consistent set: with repeated
 * settings_get() calls a concurrent write could land between two of them.
 *
 * @param out  Destination array of SETTING_COUNT bytes. Not bounds-checked.
 */
void settings_get_all(uint8_t out[SETTING_COUNT]);


/* ── Write Accessors ─────────────────────────────────────────────────────────────────────────── */

/**
 * @brief Set a setting, clamping it to the schema range.
 *
 * Updates the RAM cache immediately and schedules the flash write.  Publishes
 * SETTINGS_EVT_UPDATED only when the value actually changed.
 *
 * @param id     Setting identifier from enum setting_id.
 * @param value  Desired value; clamped to [min, max] from the schema.
 * @retval 0        Value applied unchanged (or already current).
 * @retval -ERANGE  Value was clamped; the clamped value was applied.
 * @retval -EINVAL  @p id is out of range; nothing changed.
 */
int settings_set(enum setting_id id, uint8_t value);

/**
 * @brief Reset every setting to its schema default and persist immediately.
 *
 * Bypasses the write-behind delay — a factory reset should not be lost to a
 * power cut moments later.  Publishes SETTINGS_EVT_FACTORY_RESET.
 *
 * @note No caller yet; there is no UI path to a factory reset.
 */
void settings_factory_reset(void);

/** @} */ /* dcu_settings */

#endif /* SERVICES_SETTINGS_SETTINGS_H */
