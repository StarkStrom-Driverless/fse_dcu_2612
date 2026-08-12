/**
 * @file        lighting.c
 * @brief       Lighting module — digital LED strip effects
 *
 * @ingroup     dcu_lighting
 *
 * @details     Runs a dedicated thread (priority 7) that drives the digital LED
 *              strip with one animation, from start-up until power-off.  The
 *              strip length comes from the devicetree (`chain-length` on the
 *              `led_strip` alias), so no effect hard-codes an LED count.
 *
 *              ### Which animation runs
 *              The gear animation: orange teeth rotating along the strip, to
 *              match the turning gear on the boot screen.  It is rendered from
 *              a sin^4 lookup table, which gives narrow bright teeth with dark
 *              valleys between them and needs no floating-point maths.
 *
 *              kitt_step() implements an alternative — a bouncing red
 *              cursor with a fading tail. Nothing calls it yet. swapping the call in
 *              lighting_thread_fn() switches the strip over.
 *
 *              ### Future extension
 *              A Zbus subscriber for lighting_cmd_chan will be added when the
 *              App Layer begins issuing lighting commands (zone states, effects,
 *              override layer for safety faults).  Until then the zone and
 *              layer model declared in events.h has no counterpart here.
 *
 * @author      Mario Wegmann <mario.wegmann@web.de>
 * @date        Created: 2026-07-06
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

/** @brief Devicetree node of the LED strip, resolved through the `led_strip` alias. */
#define STRIP_NODE              DT_ALIAS(led_strip)

/**
 * @brief Number of LEDs on the strip, taken from the devicetree.
 *
 * Every effect derives its geometry from this, so a strip of a different
 * length needs a shield change and no code change.  The @c \#error makes a
 * missing property a build failure with a readable message instead of a
 * confusing one about an undefined macro further down.
 */
#if DT_NODE_HAS_PROP(STRIP_NODE, chain_length)
#define LIGHTING_NUM_PIXELS     DT_PROP(STRIP_NODE, chain_length)
#else
#error "LED strip alias led_strip has no chain-length property"
#endif

/** @brief Thread stack size for the lighting thread. */
#define LIGHTING_THREAD_STACK_SIZE  2048U

/** @brief Scheduling priority for the lighting thread. */
#define LIGHTING_THREAD_PRIORITY    7

/** @brief Time between animation steps in milliseconds */
#define LIGHTING_STEP_MS            50U

/**
 * @brief KITT scanner tail brightness table.
 *
 * Index 0 = cursor (brightest), index 1..N = trailing LEDs in the direction
 * the scanner came from, each dimmer than the previous.  The array length
 * defines the tail length; no separate constant to keep in sync.
 */
static const uint8_t k_kitt_trail[] = {255, 100, 35, 10};

/* ── Gear animation ──────────────────────────────────────────────────────── */

/** @brief Number of gear teeth distributed across the strip. */
#define GEAR_NUM_TEETH      6U

/** @brief Time per animation step in ms. One full rotation = LUT_SIZE steps. */
#define GEAR_STEP_MS        40U

/** @brief LUT size; must be a power of 2 for the modulo to stay cheap. */
#define GEAR_LUT_SIZE       64U

/** @name Gear color — orange 0xfa6e00, matching the boot-screen logo.
 *  @{ */
#define GEAR_R  255U /**< Red channel at full tooth brightness.   */
#define GEAR_G   60U /**< Green channel at full tooth brightness. */
#define GEAR_B    0U /**< Blue channel; unused, kept for clarity. */
/** @} */

/**
 * @brief Brightness profile of one gear tooth.
 *
 * sin^4(2π·i/64) · 255  for i = 0..31, then 0 for i = 32..63.
 *
 * sin^4 gives narrower, sharper peaks than sin^2, which better
 * resembles distinct gear teeth.  The negative half of the sine wave
 * is clamped to 0, creating a dark valley between each tooth.
 *
 * Precomputed so the animation needs no floating-point maths at runtime.
 */
static const uint8_t k_gear_lut[GEAR_LUT_SIZE] = {
      0,   0,   0,   2,   5,  13,  24,  41,
     64,  91, 122, 154, 186, 214, 236, 250,
    255, 250, 236, 214, 186, 154, 122,  91,
     64,  41,  24,  13,   5,   2,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,
};


/* ── Private Variables ───────────────────────────────────────────────────────────────────────── */

/** @brief digital strip device handle, resolved at compile time. */
static const struct device *const s_strip = DEVICE_DT_GET(STRIP_NODE);

/**
 * @brief Frame buffer handed to the driver on every step.
 *
 * Only the lighting thread touches it, so it needs no lock.
 */
static struct led_rgb s_pixels[LIGHTING_NUM_PIXELS];

/** @brief Thread control block for the lighting thread. */
static struct k_thread s_lighting_thread;

/** @brief Stack storage for the lighting thread. */
static K_THREAD_STACK_DEFINE(s_lighting_stack, LIGHTING_THREAD_STACK_SIZE);


/* ── Private Function Implementations ───────────────────────────────────────────────────────── */

/**
 * @brief Render one KITT scanner frame and advance the cursor.
 *
 * Paints the cursor LED at full brightness and the trailing LEDs with
 * decreasing brightness in the direction the cursor came from, then
 * advances the cursor and flips direction at strip boundaries.
 *
 * Both parameters are in/out: the caller owns the animation state and this
 * function moves it one step on.
 *
 * @note Not called at present — lighting_thread_fn() runs gear_step(). Kept
 *       as an alternative effect; see the file header.
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

/**
 * @brief Render one gear animation frame and advance the phase.
 *
 * Maps each LED index to a LUT entry via:
 *   idx = (i * TEETH * LUT_SIZE / NUM_LEDS + phase) % LUT_SIZE
 *
 * The first term spreads GEAR_NUM_TEETH copies of the tooth profile evenly
 * across the strip; adding the phase shifts them all by the same amount.
 * Incrementing phase by 1 each step therefore rotates the whole gear by
 * 1/LUT_SIZE of a tooth pitch, giving smooth motion in integer arithmetic.
 *
 * Unlike kitt_step() this writes every LED each frame, so no clearing is
 * needed beforehand.
 *
 * @param phase  In/out. Current animation phase (0 … GEAR_LUT_SIZE-1);
 *               advanced by one on return.
 */
static void gear_step(uint8_t *phase)
{
    for (int32_t i = 0; i < (int32_t)LIGHTING_NUM_PIXELS; i++) {
        uint8_t idx = (uint8_t)(
            ((uint32_t)i * (GEAR_LUT_SIZE * GEAR_NUM_TEETH) / LIGHTING_NUM_PIXELS
             + *phase)
            % GEAR_LUT_SIZE
        );
        uint8_t v = k_gear_lut[idx];
        s_pixels[i].r = (uint8_t)((uint32_t)GEAR_R * v / 255U);
        s_pixels[i].g = (uint8_t)((uint32_t)GEAR_G * v / 255U);
        s_pixels[i].b = 0U;
    }

    int ret = led_strip_update_rgb(s_strip, s_pixels, LIGHTING_NUM_PIXELS);
    if (ret != 0) {
        LOG_ERR("led_strip_update_rgb failed: %d", ret);
    }

    *phase = (*phase + 1U) % GEAR_LUT_SIZE;
}

/**
 * @brief Lighting thread entry point.
 *
 * Renders the gear animation forever, one frame per GEAR_STEP_MS.  The thread
 * never blocks on anything but the sleep, so a stalled SPI transfer would show
 * up as a frozen strip rather than as a blocked system.
 *
 * @param p1  Unused.
 * @param p2  Unused.
 * @param p3  Unused.
 */
static void lighting_thread_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    uint8_t phase = 0U;

    while (true) {
        gear_step(&phase);
        k_msleep(GEAR_STEP_MS);
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
