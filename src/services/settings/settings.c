/**
 * @file        settings.c
 * @brief       Persistent settings service — NVS-backed, schema-generated
 *
 * @details     Implementation notes; the API contract is documented in
 *              settings.h and the design rationale in docs/settings_module.md.
 *
 *              Storage format
 *              ──────────────
 *              All settings live in a single NVS record under "dcu/v" rather
 *              than one record per key.  At SETTING_COUNT + 4 bytes there is
 *              nothing to gain from individual records, while a single blob
 *              gives atomic all-or-nothing saves and one write instead of N.
 *
 *              Schema drift
 *              ────────────
 *              The blob carries SETTINGS_SCHEMA_HASH, generated from the
 *              schema's names, order, bounds and defaults.  On mismatch the
 *              blob is discarded and defaults apply.  This is what makes
 *              inserting a setting in the middle of the enum safe — without
 *              it, every setting after the insertion point would silently
 *              inherit its neighbour's value.
 *
 *              Locking
 *              ───────
 *              s_lock guards s_values only.  It is never held across a call
 *              into the settings subsystem: the flush handler copies the blob
 *              under the lock, releases it, and only then writes to flash.
 *
 * @author      Mario Wegmann <mario.wegmann@web.de>
 * @date        Created: 2026-08-07
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
 * 0.1.0    2026-08-07  Mario Wegmann   Initial creation
 * ─────────────────────────────────────────────────────────────────────────────────────────────────
 */

/* ── Corresponding Header ────────────────────────────────────────────────────────────────────── */

#include "services/settings/settings.h"

/* ── Standard Includes ───────────────────────────────────────────────────────────────────────── */

#include <errno.h>
#include <string.h>

/* ── Zephyr Includes ─────────────────────────────────────────────────────────────────────────── */

#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/logging/log.h>
#ifdef CONFIG_DCU_SETTINGS_PERSIST
#include <zephyr/settings/settings.h>
#endif

/* ── Project Includes ────────────────────────────────────────────────────────────────────────── */

#include "services/event_bus/event_bus.h"
#include "services/event_bus/events.h"

/* ── Zephyr Logging ──────────────────────────────────────────────────────────────────────────── */

LOG_MODULE_REGISTER(settings_service, CONFIG_LOG_DEFAULT_LEVEL);


/* ── Private Macros & Constants ──────────────────────────────────────────────────────────────── */

#ifdef CONFIG_DCU_SETTINGS_PERSIST

/** @brief Settings subtree owned by this service. */
#define SETTINGS_SUBTREE        "dcu"

/** @brief Leaf name of the blob record inside the subtree. */
#define SETTINGS_BLOB_LEAF      "v"

/** @brief Full key passed to settings_save_one(). */
#define SETTINGS_BLOB_KEY       SETTINGS_SUBTREE "/" SETTINGS_BLOB_LEAF

/**
 * @brief Write-behind delay.
 *
 * Coalesces a burst of changes (an encoder sweep) into one flash write.
 * Long enough to absorb a full turn, short enough that a value is on flash
 * well before the driver can power the car down.
 */
#define SETTINGS_FLUSH_DELAY    K_SECONDS(2)


/* ── Private Types ───────────────────────────────────────────────────────────────────────────── */

/**
 * @brief On-flash representation. Layout is tied to SETTINGS_SCHEMA_HASH.
 *
 * Packed so the stored size stays independent of the compiler's padding
 * choices — a size change would otherwise silently invalidate saved data.
 */
struct settings_blob {
    uint32_t schema_hash;
    uint8_t  values[SETTING_COUNT];
} __packed;

#endif /* CONFIG_DCU_SETTINGS_PERSIST */


/* ── Private Variables ───────────────────────────────────────────────────────────────────────── */

/** @brief Authoritative runtime values. Guarded by s_lock. */
static uint8_t s_values[SETTING_COUNT];

/** @brief Guards s_values. Never held across a settings-subsystem call. */
static K_MUTEX_DEFINE(s_lock);


/* ── Private Function Prototypes ─────────────────────────────────────────────────────────────── */

static void    apply_defaults(void);
static uint8_t clamp_to_schema(enum setting_id id, uint8_t value);
static void    publish(enum settings_event_type type);

#ifdef CONFIG_DCU_SETTINGS_PERSIST
static void    flush_work_fn(struct k_work *work);
static int     blob_set(const char *key, size_t len,
                        settings_read_cb read_cb, void *cb_arg);

/** @brief Deferred flash write; see SETTINGS_FLUSH_DELAY. */
static K_WORK_DELAYABLE_DEFINE(s_flush_work, flush_work_fn);

/** @brief Schedule the deferred write. No-op when persistence is compiled out. */
#define settings_schedule_flush(delay) k_work_reschedule(&s_flush_work, (delay))
#else
#define settings_schedule_flush(delay) ((void)0)
#endif


/* ── Private Function Implementations ───────────────────────────────────────────────────────── */

/** @brief Load every setting with its schema default. Caller holds s_lock. */
static void apply_defaults(void)
{
    for (size_t i = 0U; i < SETTING_COUNT; i++) {
        s_values[i] = settings_schema[i].def;
    }
}

/** @brief Clamp @p value into the schema range of @p id. */
static uint8_t clamp_to_schema(enum setting_id id, uint8_t value)
{
    const setting_desc_t *desc = &settings_schema[id];

    if (value < desc->min) {
        return desc->min;
    }
    if (value > desc->max) {
        return desc->max;
    }
    return value;
}

/** @brief Publish a lifecycle event on settings_chan. */
static void publish(enum settings_event_type type)
{
    struct settings_event evt = { .type = type };

    int rc = zbus_chan_pub(&settings_chan, &evt, K_NO_WAIT);
    if (rc != 0) {
        LOG_WRN("settings_chan publish failed: %d", rc);
    }
}

#ifdef CONFIG_DCU_SETTINGS_PERSIST

/**
 * @brief Write the current values to flash.
 *
 * Runs on the system workqueue.  The mutex is released before the flash
 * access so a slow erase never blocks a reader.
 */
static void flush_work_fn(struct k_work *work)
{
    ARG_UNUSED(work);

    struct settings_blob blob = { .schema_hash = SETTINGS_SCHEMA_HASH };

    k_mutex_lock(&s_lock, K_FOREVER);
    memcpy(blob.values, s_values, sizeof(blob.values));
    k_mutex_unlock(&s_lock);

    int rc = settings_save_one(SETTINGS_BLOB_KEY, &blob, sizeof(blob));
    if (rc != 0) {
        LOG_ERR("Persisting settings failed: %d", rc);
    } else {
        LOG_DBG("Settings persisted (%zu bytes)", sizeof(blob));
    }
}

/**
 * @brief Settings subsystem load callback for the "dcu" subtree.
 *
 * Returning 0 without touching s_values leaves the defaults in place, which
 * is the intended behaviour for every recoverable inconsistency — a missing,
 * truncated or schema-stale record must not prevent the system from booting.
 */
static int blob_set(const char *key, size_t len,
                    settings_read_cb read_cb, void *cb_arg)
{
    struct settings_blob blob;

    if (!settings_name_steq(key, SETTINGS_BLOB_LEAF, NULL)) {
        return -ENOENT;
    }

    if (len != sizeof(blob)) {
        LOG_WRN("Stored blob is %zu bytes, expected %zu — using defaults",
                len, sizeof(blob));
        return 0;
    }

    ssize_t rc = read_cb(cb_arg, &blob, sizeof(blob));
    if (rc < 0) {
        LOG_ERR("Reading settings blob failed: %d — using defaults", (int)rc);
        return 0;
    }

    if (blob.schema_hash != (uint32_t)SETTINGS_SCHEMA_HASH) {
        LOG_WRN("Schema changed (stored 0x%08X, expected 0x%08X) — using defaults",
                blob.schema_hash, (uint32_t)SETTINGS_SCHEMA_HASH);
        return 0;
    }

    /*
     * Clamp on load as well: the hash guarantees the schema is unchanged, but
     * not that the bytes on flash are intact.
     */
    k_mutex_lock(&s_lock, K_FOREVER);
    for (size_t i = 0U; i < SETTING_COUNT; i++) {
        s_values[i] = clamp_to_schema((enum setting_id)i, blob.values[i]);
    }
    k_mutex_unlock(&s_lock);

    return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(dcu_settings, SETTINGS_SUBTREE,
                               NULL,      /* h_get    */
                               blob_set,  /* h_set    */
                               NULL,      /* h_commit */
                               NULL);     /* h_export */

#endif /* CONFIG_DCU_SETTINGS_PERSIST */


/* ── Public Function Implementations ─────────────────────────────────────────────────────────── */

void settings_service_init(void)
{
    k_mutex_lock(&s_lock, K_FOREVER);
    apply_defaults();
    k_mutex_unlock(&s_lock);

#ifdef CONFIG_DCU_SETTINGS_PERSIST
    int rc = settings_subsys_init();
    if (rc != 0) {
        LOG_ERR("settings_subsys_init failed: %d — running on defaults", rc);
    } else {
        rc = settings_load_subtree(SETTINGS_SUBTREE);
        if (rc != 0) {
            LOG_ERR("settings_load_subtree failed: %d — running on defaults", rc);
        }
    }
#else
    /*
     * No non-volatile storage on the board yet.  The service keeps its full
     * API and the values stay authoritative for the runtime — they just start
     * from the schema defaults on every boot.
     */
    LOG_WRN("Persistence disabled (CONFIG_DCU_SETTINGS_PERSIST=n) — "
            "settings reset to defaults on every boot");
#endif

    for (size_t i = 0U; i < SETTING_COUNT; i++) {
        LOG_INF("%-20s = %u", settings_schema[i].name, s_values[i]);
    }

    publish(SETTINGS_EVT_LOADED);
}

uint8_t settings_get(enum setting_id id)
{
    if (id >= SETTING_COUNT) {
        LOG_ERR("settings_get: id %d out of range", (int)id);
        return 0U;
    }

    k_mutex_lock(&s_lock, K_FOREVER);
    uint8_t value = s_values[id];
    k_mutex_unlock(&s_lock);

    return value;
}

void settings_get_all(uint8_t out[SETTING_COUNT])
{
    k_mutex_lock(&s_lock, K_FOREVER);
    memcpy(out, s_values, SETTING_COUNT);
    k_mutex_unlock(&s_lock);
}

int settings_set(enum setting_id id, uint8_t value)
{
    if (id >= SETTING_COUNT) {
        LOG_ERR("settings_set: id %d out of range", (int)id);
        return -EINVAL;
    }

    uint8_t clamped = clamp_to_schema(id, value);
    bool    changed;

    k_mutex_lock(&s_lock, K_FOREVER);
    changed = (s_values[id] != clamped);
    s_values[id] = clamped;
    k_mutex_unlock(&s_lock);

    if (changed) {
        settings_schedule_flush(SETTINGS_FLUSH_DELAY);
        publish(SETTINGS_EVT_UPDATED);
        LOG_DBG("%s = %u", settings_schema[id].name, clamped);
    }

    if (clamped != value) {
        LOG_WRN("%s: %u clamped to %u", settings_schema[id].name, value, clamped);
        return -ERANGE;
    }

    return 0;
}

void settings_factory_reset(void)
{
    LOG_WRN("Factory reset — all settings back to defaults");

    k_mutex_lock(&s_lock, K_FOREVER);
    apply_defaults();
    k_mutex_unlock(&s_lock);

    /* Do not let a factory reset be lost to a power cut during the delay. */
    settings_schedule_flush(K_NO_WAIT);

    publish(SETTINGS_EVT_FACTORY_RESET);
}
