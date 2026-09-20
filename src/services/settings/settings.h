/**
 * @file
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
 *              ### Ownership model
 *              Three layers, each with exactly one job:
 *
 *              | Layer                | Holds                          | Thread-safe    |
 *              |----------------------|--------------------------------|----------------|
 *              | NVS (flash)          | the persisted blob             | yes            |
 *              | this service         | the authoritative values       | yes (mutex)    |
 *              | `ui_tx_subj_*`       | a UI-side mirror for widgets   | LVGL thread    |
 *
 *              The CAN TX thread reads this service, never an LVGL subject.
 *              That is the load-bearing rule: subjects belong to the LVGL
 *              thread (priority 8), and the CAN thread (priority 3) preempts
 *              it — reading a subject from there is a data race.
 *
 *              The generated subjects start at zero and are not fed from here.
 *              A screen that shows a setting seeds its own widgets with
 *              settings_get() while being built (see screen_settings.c and
 *              screen_ev_driving.c), which also picks up changes made on the
 *              other screen in the meantime.
 *
 *              Settings are deliberately *not* stored in app_state.  That holds
 *              vehicle and mission state; settings are an orthogonal concern
 *              with their own lifetime — they outlive a power cycle — and their
 *              own store.  Duplicating them into app_state would create a
 *              second source of truth for no gain.
 *
 *              The App Layer stays in the *control* path: the UI publishes
 *              intent, the App decides and calls settings_set().  The Dirigent
 *              pattern governs who may *change* a setting; it does not require
 *              the App to also *hold* it.
 *
 *              ### Data flow of a change
 *              ```
 *              Driver turns the encoder
 *                ├─ UI: lv_subject_set_int()          ← immediate feedback
 *                └─ UI: publish ui_input_chan {id, value}
 *                      └─ App: settings_set(id, value)
 *                            ├─ clamp to the schema range
 *                            ├─ update the RAM cache
 *                            ├─ schedule the write-behind (see Flash wear)
 *                            └─ publish SETTINGS_EVT_UPDATED
 *                                  └─ CAN picks it up on its next frame
 *              ```
 *
 *              The UI writes its own subject optimistically — otherwise the
 *              control would lag by two thread switches.  A screen that clamps
 *              differently than the service would converge on the next build of
 *              that screen; both clamp against the same generated schema, so
 *              they do not disagree in practice.
 *
 *              ### Flash wear
 *              Writing the current value is a no-op, and changes are coalesced
 *              into a single flash write ~2 s after the last one.  Without
 *              this, sweeping an encoder from 0 to 7 would cost eight writes.
 *              The RAM cache is updated synchronously, so a read never
 *              observes a stale value while a flush is pending.
 *
 *              ### Where the values live
 *              With CONFIG_DCU_SETTINGS_PERSIST (on for the DCU board) the
 *              blob sits in NVS on the external SPI NOR flash, in the
 *              settings_partition the shield overlay defines.
 *
 *              With the option off — the emulator, or any target without that
 *              flash — the storage path is compiled out entirely: no backend,
 *              no write delay, no schema-hash check.  The API and its
 *              semantics are unchanged, values stay authoritative for the
 *              runtime and are still clamped to the schema, but every boot
 *              starts from the defaults.  Neither case needs a change in any
 *              caller.
 *
 * @author      Mario Wegmann <mario.wegmann@web.de>
 * @date        Created: 2026-08-07
 *
 * @copyright   Copyright (c) 2026 Mario Wegmann.
 *              SPDX-License-Identifier: Apache-2.0
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


/* ── Initialization ──────────────────────────────────────────────────────────────────────────── */

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
