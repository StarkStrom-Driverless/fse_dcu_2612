/**
 * @file        lighting.c
 * @brief       Lighting module — APA102 LED strip effects
 *
 * @details     Runs a dedicated thread (priority 7) that drives the APA102
 *              LED strip.  On startup the KITT scanner effect plays
 *              continuously: a bright red cursor bounces back and forth across
 *              the strip with an exponentially fading red tail.
 *
 *              Effect timing
 *              ─────────────
 *              Each step advances the cursor by one LED and sleeps for
 *              LIGHTING_STEP_MS (50 ms).  A full sweep across 12 LEDs takes
 *              11 × 50 ms = 550 ms one-way, giving a ~1.1 s full cycle.
 *
 *              Future extension
 *              ────────────────
 *              A Zbus subscriber for lighting_cmd_chan will be added when the
 *              App Layer begins issuing lighting commands (zone states, effects,
 *              override layer for safety faults).
 *
 * @author      Mario Wegmann <mario.wegmann@web.de>
 * @date        Created: 2026-07-06
 *
 * @version     0.1.0
 *
 * @copyright   Copyright (c) 2026 Mario Wegmann
 *              SPDX-License-Identifier: Apache-2.0
 *
 * @note        Target RTOS : Zephyr RTOS (https://zephyrproject.org)
 *
 * ─────────────────────────────────────────────────────────────────────────────────────────────────
 * Revision History
 * ─────────────────────────────────────────────────────────────────────────────────────────────────
 * Version  Date        Author          Description
 * 0.1.0    2026-07-06  Mario Wegmann   Initial creation
 * ─────────────────────────────────────────────────────────────────────────────────────────────────
 */

/* ── Corresponding Header ────────────────────────────────────────────────────────────────────── */

#include "modules/lighting/lighting.h"

/* ── Zephyr Includes ─────────────────────────────────────────────────────────────────────────── */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>

#include <string.h>

/* ── Zephyr Logging ──────────────────────────────────────────────────────────────────────────── */

LOG_MODULE_REGISTER(lighting_module, CONFIG_LOG_DEFAULT_LEVEL);


/* ── Private Macros & Constants ──────────────────────────────────────────────────────────────── */

#define STRIP_NODE              DT_ALIAS(led_strip)

#if DT_NODE_HAS_PROP(STRIP_NODE, chain_length)
#define LIGHTING_NUM_PIXELS     DT_PROP(STRIP_NODE, chain_length)
#else
#error "LED strip alias led_strip has no chain-length property"
#endif

/** @brief Thread stack size for the lighting thread. */
#define LIGHTING_THREAD_STACK_SIZE  4096U

/** @brief Scheduling priority for the lighting thread. */
#define LIGHTING_THREAD_PRIORITY    7

/** @brief Time between KITT scanner steps in milliseconds. */
#define LIGHTING_STEP_MS            50U

/**
 * @brief KITT scanner tail brightness table.
 *
 * Index 0 = cursor (brightest), index 1..N = trailing LEDs in the direction
 * the scanner came from, each dimmer than the previous.
 */
static const uint8_t k_kitt_trail[] = {255, 100, 35, 10};


/* ── Private Variables ───────────────────────────────────────────────────────────────────────── */

static const struct device *const s_strip = DEVICE_DT_GET(STRIP_NODE);

static struct led_rgb s_pixels[LIGHTING_NUM_PIXELS];

static struct k_thread s_lighting_thread;
static K_THREAD_STACK_DEFINE(s_lighting_stack, LIGHTING_THREAD_STACK_SIZE);


/* ── Private Function Implementations ───────────────────────────────────────────────────────── */

/**
 * @brief Render one KITT scanner frame and advance the cursor.
 *
 * Paints the cursor LED at full brightness and the trailing LEDs with
 * decreasing brightness in the direction the cursor came from, then
 * advances the cursor and flips direction at strip boundaries.
 *
 * @param cursor  Current cursor position (0 … LIGHTING_NUM_PIXELS-1).
 * @param dir     Current scan direction (+1 = right, -1 = left).
 */
static void kitt_step(int32_t *cursor, int32_t *dir)
{
    memset(s_pixels, 0, sizeof(s_pixels));

    for (int32_t k = 0; k < (int32_t)ARRAY_SIZE(k_kitt_trail); k++) {
        int32_t pos = *cursor - (*dir * k);
        if (pos >= 0 && pos < (int32_t)LIGHTING_NUM_PIXELS) {
            s_pixels[pos].r = k_kitt_trail[k];
        }
    }

    int ret = led_strip_update_rgb(s_strip, s_pixels, LIGHTING_NUM_PIXELS);
    if (ret != 0) {
        LOG_ERR("led_strip_update_rgb failed: %d", ret);
    }

    *cursor += *dir;
    if (*cursor <= 0) {
        *cursor = 0;
        *dir    = +1;
    } else if (*cursor >= (int32_t)LIGHTING_NUM_PIXELS - 1) {
        *cursor = (int32_t)LIGHTING_NUM_PIXELS - 1;
        *dir    = -1;
    }
}

static void lighting_thread_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    int32_t cursor = 0;
    int32_t dir    = +1;

    while (true) {
        kitt_step(&cursor, &dir);
        k_msleep(LIGHTING_STEP_MS);
    }
}


/* ── Public Function Implementations ─────────────────────────────────────────────────────────── */

void lighting_module_init(void)
{
    if (!device_is_ready(s_strip)) {
        LOG_ERR("LED strip device %s not ready — lighting module disabled",
                s_strip->name);
        return;
    }

    LOG_INF("LED strip device %s ready (%d pixels)", s_strip->name,
            LIGHTING_NUM_PIXELS);

    k_thread_create(&s_lighting_thread,
                    s_lighting_stack,
                    K_THREAD_STACK_SIZEOF(s_lighting_stack),
                    lighting_thread_fn,
                    NULL, NULL, NULL,
                    LIGHTING_THREAD_PRIORITY, 0, K_NO_WAIT);
    k_thread_name_set(&s_lighting_thread, "lighting");

    LOG_INF("Lighting module initialised");
}
