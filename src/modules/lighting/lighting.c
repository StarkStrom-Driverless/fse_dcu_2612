/**
 * @file
 * @brief       Lighting module — digital LED strip effects
 *
 * @ingroup     dcu_lighting
 *
 * @details     Runs a dedicated thread (priority 7) that drives the digital LED
 *              strip with one animation, from start-up until power-off.  The
 *              strip length comes from the devicetree (`chain-length` on the
 *              `led_strip` alias), so no effect hard-codes an LED count.
 *
 *              ### Two modes, picked by the active screen
 *              On SCREEN_EV_DRIVING the strip becomes an instrument and shows
 *              the three-zone view described below. On every other screen it
 *              runs the gear animation: orange teeth rotating along the strip,
 *              matching the gear on the START screen (whose own rotation is
 *              currently disabled), rendered from a sin^4 lookup table that
 *              needs no floating-point math.
 *
 *              ### The three zones
 *
 *              | Zone | LEDs | Shows |
 *              |------|------|-------|
 *              | Left | ZONE_LEFT_LEN | HV accumulator voltage, emptying as the pack drains |
 *              | Middle | 10 | LED_1…LED_8 as red flags, DCU_RESERVE_LED in amber, DCU_RGB_LED_Themperatur as a red/green/blue code |
 *              | Right | ZONE_RIGHT_LEN | Whichever of the three temperatures is closest to its own critical limit |
 *
 *              Both bars grow from the outer edge of the strip towards the
 *              middle, so they read as mirror images rising together. Their
 *              empty part stays faintly lit in the same color (ZONE_TRACK_DIV),
 *              which is what makes the length of a bar readable at a glance —
 *              a bar at zero would otherwise be indistinguishable from a dark
 *              strip.
 *
 *              Boundaries are derived from the devicetree strip length, so the
 *              layout follows the hardware rather than a hard-coded 28.
 *
 *              The strip is mounted with its data input on the right. Nothing
 *              here has to know: positions are counted from the left and
 *              strip_px() maps them onto the chain, driven by
 *              LIGHTING_DATA_FROM_RIGHT.
 *
 *              ### Where the data comes from
 *              Both the values and the active screen are read from app_state,
 *              the same way the CAN module reads mission and operating mode.
 *              This module subscribes to nothing and talks to no other module:
 *              the App Layer stays the only writer, and the Dirigent pattern
 *              holds.
 *
 *              ### Future extension
 *              A Zbus subscriber for lighting_cmd_chan will be added when the
 *              App Layer begins issuing lighting commands (effects, override
 *              layer for safety faults). The zone and layer model declared in
 *              events.h is still unrelated to the zones implemented here —
 *              those are a fixed layout, not a commandable one.
 *
 * @author      Mario Wegmann <mario.wegmann@web.de>
 * @date        Created: 2026-07-06
 *
 * @copyright   Copyright (c) 2026 Mario Wegmann.
 *              SPDX-License-Identifier: Apache-2.0
 */

/* ── Corresponding Header ────────────────────────────────────────────────────────────────────── */

#include "modules/lighting/lighting.h"

/* ── Project Includes ────────────────────────────────────────────────────────────────────────── */

#include "app/app_state.h"
#include "generated/ui_subjects_gen.h"
#include "services/event_bus/events.h"

/* ── Zephyr Includes ─────────────────────────────────────────────────────────────────────────── */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>


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

/**
 * @brief Thread stack size for the lighting thread.
 *
 * Raised from 512 after the thread analyzer reported 328 of them in use, which
 * left 184 bytes. The strip update calls into the LED strip driver and from
 * there into SPI, and that path is deeper than the animation code above it.
 */
#define LIGHTING_THREAD_STACK_SIZE  2048U

/** @brief Scheduling priority for the lighting thread. */
#define LIGHTING_THREAD_PRIORITY    7

/**
 * @brief Set when the strip's data line enters at its right-hand end.
 *
 * The strip is mounted with the data input on the right, so pixel 0 of the
 * chain sits where the driver sees the right edge. Everything in this file is
 * written in reading order — index 0 on the left — and strip_px() is the one
 * place that turns that into a chain index.
 *
 * Clear this for a board wired the other way round; nothing else changes.
 */
#define LIGHTING_DATA_FROM_RIGHT    1

/* ── Zone rendering (EV driving screen) ──────────────────────────────────── */

/**
 * @brief Number of LEDs in the middle zone.
 *
 * One per signal it shows: LED_1…LED_8, then DCU_RESERVE_LED and
 * DCU_RGB_LED_Themperatur. Fixed by the signal list, not by taste.
 */
#define ZONE_MID_LEN        10

/**
 * @name Zone boundaries, derived from the strip length
 *
 * The middle zone is fixed; the two bars split what is left. An odd remainder
 * goes to the right zone, so no LED is left dark between the zones.
 * @{
 */
#define ZONE_LEFT_FIRST     0
#define ZONE_LEFT_LEN       ((LIGHTING_NUM_PIXELS - ZONE_MID_LEN) / 2)
#define ZONE_MID_FIRST      (ZONE_LEFT_FIRST + ZONE_LEFT_LEN)
#define ZONE_RIGHT_FIRST    (ZONE_MID_FIRST + ZONE_MID_LEN)
#define ZONE_RIGHT_LEN      (LIGHTING_NUM_PIXELS - ZONE_RIGHT_FIRST)
/** @} */

#if (LIGHTING_NUM_PIXELS < (ZONE_MID_LEN + 2))
#error "LED strip too short for the three-zone layout"
#endif

/**
 * @name Zone colors
 *
 * Chosen to match the display palette in ui_styles.h so that strip and screen
 * agree on what green, amber and red mean. Kept below full brightness: the
 * strip sits in the driver's field of view.
 * @{
 */
#define ZONE_RGB_OFF        { .r =   0, .g =   0, .b =   0 }
#define ZONE_RGB_GREEN      { .r =   0, .g = 255, .b =   0 }
#define ZONE_RGB_AMBER      { .r = 200, .g = 130, .b =   0 }
#define ZONE_RGB_RED        { .r = 255, .g =   0, .b =  20 }
#define ZONE_RGB_BLUE       { .r =   0, .g =  40, .b = 200 }
/** @} */

/**
 * @brief How much dimmer the empty part of a bar is than its fill.
 *
 * Low enough not to be mistaken for a lit segment, high enough to show the
 * bar's extent in daylight. The track follows the bar's own color, so a bar
 * that has gone red is backed by faint red rather than by a second hue the
 * driver would have to interpret.
 */
#define ZONE_TRACK_DIV      10U

/** @brief Severity bands shared by both progress bars. */
enum zone_level {
    ZONE_LEVEL_OK = 0,  /**< Below the warning limit.        */
    ZONE_LEVEL_WARN,    /**< Between warning and critical.   */
    ZONE_LEVEL_CRIT,    /**< At or beyond the critical limit.*/
};

/**
 * @brief How far a reading has come, and how bad it is.
 *
 * @c pct is the fill for the bar, @c level picks its color. Keeping them
 * apart matters: the two do not move together, because the warning limit sits
 * at a different fraction of the range for every signal.
 */
struct zone_reading {
    uint8_t         pct;
    enum zone_level level;
};

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
 * Precomputed so the animation needs no floating-point math at runtime.
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
 * @brief Address one LED by its position as the driver sees it.
 *
 * Index 0 is the left end of the strip, whatever the wiring does. Every write
 * in this file goes through here, so a mirrored strip is a single constant
 * rather than an off-by-one hunt across three zones and an animation.
 *
 * @param pos  Position from the left, 0 … LIGHTING_NUM_PIXELS-1.
 * @return     Pointer to that LED in the chain buffer.
 */
static inline struct led_rgb *strip_px(uint8_t pos)
{
#if LIGHTING_DATA_FROM_RIGHT
    return &s_pixels[LIGHTING_NUM_PIXELS - 1U - pos];
#else
    return &s_pixels[pos];
#endif
}

/**
 * @brief Render one gear animation frame and advance the phase.
 *
 * Maps each LED index to a LUT entry via:
 *   idx = (i * TEETH * LUT_SIZE / NUM_LEDS + phase) % LUT_SIZE
 *
 * The first term spreads GEAR_NUM_TEETH copies of the tooth profile evenly
 * across the strip; adding the phase shifts them all by the same amount.
 * Stepping the phase by one each frame therefore rotates the whole gear by
 * 1/LUT_SIZE of a tooth pitch, giving smooth motion in integer arithmetic.
 *
 * The phase counts *down*: positions run left to right while the strip is
 * wired right to left, and counting up would turn the gear against the one on
 * the START screen, should its rotation be switched on.
 *
 * This writes every LED each frame, so no clearing is needed beforehand.
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
        struct led_rgb *px = strip_px((uint8_t)i);

        px->r = (uint8_t)((uint32_t)GEAR_R * v / 255U);
        px->g = (uint8_t)((uint32_t)GEAR_G * v / 255U);
        px->b = 0U;
    }

    int ret = led_strip_update_rgb(s_strip, s_pixels, LIGHTING_NUM_PIXELS);
    if (ret != 0) {
        LOG_ERR("led_strip_update_rgb failed: %d", ret);
    }

    *phase = (uint8_t)((*phase + GEAR_LUT_SIZE - 1U) % GEAR_LUT_SIZE);
}

/**
 * @brief Grade one reading against its warning and critical limits.
 *
 * @c pct is measured from the range minimum to the *critical* limit, not to the
 * range maximum: a full bar then means "at the limit", which is the number the
 * driver needs. Anything beyond clamps to full rather than overflowing.
 *
 * @param value  Current reading.
 * @param min    Range minimum from the generated limits.
 * @param warn   Warning limit.
 * @param crit   Critical limit. Must be greater than @p min.
 * @return       Fill percentage and severity band.
 */
static struct zone_reading grade_rising(int32_t value, int32_t min,
                                        int32_t warn, int32_t crit)
{
    struct zone_reading r;

    if (value <= min) {
        r.pct = 0U;
    } else if (value >= crit) {
        r.pct = 100U;
    } else {
        r.pct = (uint8_t)(((value - min) * 100) / (crit - min));
    }

    r.level = (value >= crit) ? ZONE_LEVEL_CRIT
            : (value >= warn) ? ZONE_LEVEL_WARN
                              : ZONE_LEVEL_OK;
    return r;
}

/**
 * @brief Color for a severity band.
 *
 * @param level  Band from grade_rising() or grade_falling().
 * @return       The strip color standing for it.
 */
static struct led_rgb zone_level_color(enum zone_level level)
{
    static const struct led_rgb green = ZONE_RGB_GREEN;
    static const struct led_rgb amber = ZONE_RGB_AMBER;
    static const struct led_rgb red   = ZONE_RGB_RED;

    switch (level) {
    case ZONE_LEVEL_CRIT: return red;
    case ZONE_LEVEL_WARN: return amber;
    default:              return green;
    }
}

/**
 * @brief Paint a run of LEDs as a bar filled to @p pct.
 *
 * Rounds to the nearest whole LED, so a bar only lights its last LED once the
 * reading is more than half way into it.
 *
 * Both bars grow from the outer edge of the strip towards the middle, which
 * makes them mirror images of each other: the left bar fills from its first
 * LED, the right bar from its last. A driver glancing down reads two bars
 * rising towards the center rather than two running the same way.
 *
 * The empty part is not switched off but dimmed to ZONE_TRACK_DIV of the same
 * color. That way the bar's full length is visible at a glance — an unlit
 * strip and a bar at zero would otherwise look the same.
 *
 * @param first     Index of the zone's first LED, counted from the left.
 * @param len       Number of LEDs in the zone.
 * @param pct       Fill, 0…100.
 * @param color     Color for the lit part.
 * @param from_last True when the zone's outer edge is its last LED (the right
 *                  bar), false when it is its first (the left bar).
 */
static void zone_fill_bar(uint8_t first, uint8_t len, uint8_t pct,
                          struct led_rgb color, bool from_last)
{
    const struct led_rgb track = {
        .r = (uint8_t)(color.r / ZONE_TRACK_DIV),
        .g = (uint8_t)(color.g / ZONE_TRACK_DIV),
        .b = (uint8_t)(color.b / ZONE_TRACK_DIV),
    };
    const uint8_t lit = (uint8_t)(((uint32_t)pct * len + 50U) / 100U);

    for (uint8_t i = 0U; i < len; i++) {
        /* Distance from the outer edge, which is where the fill starts. */
        const uint8_t from_edge = from_last ? (uint8_t)(len - 1U - i) : i;

        *strip_px((uint8_t)(first + i)) = (from_edge < lit) ? color : track;
    }
}

/**
 * @brief Right zone — whichever of the three temperatures is closest to its limit.
 *
 * The three have different ranges and different limits, so their raw values
 * cannot be compared. Grading each against its own critical limit puts them on
 * one scale, and the highest grade wins: the bar always shows the temperature
 * that is in most trouble, whichever that currently is.
 *
 * The color comes from the winner's own band, so a bar at 80 % reads as amber
 * or green depending on where that signal's warning limit sits.
 *
 * @param snap  Latest CAN snapshot.
 */
static void zone_render_temperatures(const struct can_data_snapshot *snap)
{
    const struct zone_reading readings[] = {
        grade_rising((int32_t)snap->temperature_accu_hv,
                     (int32_t)UI_TEMPERATURE_ACCU_HV_RANGE_MIN,
                     (int32_t)UI_TEMPERATURE_ACCU_HV_WARN_HIGH,
                     (int32_t)UI_TEMPERATURE_ACCU_HV_CRIT_HIGH),
        grade_rising((int32_t)snap->temperature_inverter,
                     (int32_t)UI_TEMPERATURE_INVERTER_RANGE_MIN,
                     (int32_t)UI_TEMPERATURE_INVERTER_WARN_HIGH,
                     (int32_t)UI_TEMPERATURE_INVERTER_CRIT_HIGH),
        grade_rising((int32_t)snap->temperature_motor,
                     (int32_t)UI_TEMPERATURE_MOTOR_RANGE_MIN,
                     (int32_t)UI_TEMPERATURE_MOTOR_WARN_HIGH,
                     (int32_t)UI_TEMPERATURE_MOTOR_CRIT_HIGH),
    };

    struct zone_reading worst = readings[0];

    for (size_t i = 1U; i < ARRAY_SIZE(readings); i++) {
        if (readings[i].pct > worst.pct) {
            worst = readings[i];
        }
    }

    zone_fill_bar(ZONE_RIGHT_FIRST, ZONE_RIGHT_LEN, worst.pct,
                  zone_level_color(worst.level), true);
}

/**
 * @brief Middle zone — ten status signals, one LED each.
 *
 * LED_1…LED_8 are plain flags: lit red when set, dark when clear. The last two
 * carry their own color coding and are handled separately.
 *
 * @param snap  Latest CAN snapshot.
 */
static void zone_render_status(const struct can_data_snapshot *snap)
{
    static const struct led_rgb off   = ZONE_RGB_OFF;
    static const struct led_rgb red   = ZONE_RGB_RED;
    static const struct led_rgb green = ZONE_RGB_GREEN;
    static const struct led_rgb blue  = ZONE_RGB_BLUE;
    static const struct led_rgb amber = ZONE_RGB_AMBER;

    /* LED_1 … LED_8, in the order the DBC numbers them. */
    const bool flags[8] = {
        snap->dv_receiving,          /* LED_1 */
        snap->dv_ready,              /* LED_2 */
        snap->sdc_open,              /* LED_3 */
        snap->datalogger_ready,      /* LED_4 */
        snap->datalogger_recording,  /* LED_5 */
        snap->rtd_possible,          /* LED_6 */
        snap->startup_finish,        /* LED_7 */
        snap->kistler_timeout,       /* LED_8 */
    };

    for (uint8_t i = 0U; i < ARRAY_SIZE(flags); i++) {
        *strip_px((uint8_t)(ZONE_MID_FIRST + i)) = flags[i] ? red : off;
    }

    /* DCU_RESERVE_LED — a single amber flag. */
    *strip_px(ZONE_MID_FIRST + 8U) = snap->led_reserved_0 ? amber : off;

    /*
     * DCU_RGB_LED_Themperatur — a three-state code rather than a flag.
     * Anything outside 1…3 is dark, so an unexpected value reads as "no
     * statement" instead of silently borrowing another state's color.
     */
    struct led_rgb *rgb_led = strip_px(ZONE_MID_FIRST + 9U);

    switch (snap->temperature_generic) {
    case 1:  *rgb_led = red;   break;
    case 2:  *rgb_led = green; break;
    case 3:  *rgb_led = blue;  break;
    default: *rgb_led = off;   break;
    }
}

/**
 * @brief Left zone — HV accumulator voltage.
 *
 * Voltage is graded the other way round from a temperature: the limits are
 * lower bounds, so the bar empties as the pack drains and the color worsens
 * on the way down. The fill therefore spans the full declared range rather
 * than stopping at the critical limit — a driver watching this wants to see
 * the pack empty, not the bar sit at zero from the limit onwards.
 *
 * @param snap  Latest CAN snapshot.
 */
static void zone_render_voltage(const struct can_data_snapshot *snap)
{
    const int32_t v    = (int32_t)snap->voltage_accu_hv;
    const int32_t min  = (int32_t)UI_VOLTAGE_ACCU_HV_RANGE_MIN;
    const int32_t max  = (int32_t)UI_VOLTAGE_ACCU_HV_RANGE_MAX;
    const int32_t warn = (int32_t)UI_VOLTAGE_ACCU_HV_WARN_LOW;
    const int32_t crit = (int32_t)UI_VOLTAGE_ACCU_HV_CRIT_LOW;

    uint8_t pct;
    if (v <= min) {
        pct = 0U;
    } else if (v >= max) {
        pct = 100U;
    } else {
        pct = (uint8_t)(((v - min) * 100) / (max - min));
    }

    const enum zone_level level = (v <= crit) ? ZONE_LEVEL_CRIT
                                : (v <= warn) ? ZONE_LEVEL_WARN
                                              : ZONE_LEVEL_OK;

    zone_fill_bar(ZONE_LEFT_FIRST, ZONE_LEFT_LEN, pct,
                  zone_level_color(level), false);
}

/**
 * @brief Render one full frame of the three-zone view.
 *
 * Takes one copy of the snapshot and works from it, so the three zones cannot
 * end up showing values from different moments. Every LED is written, so no
 * clearing is needed beforehand.
 */
static void zone_step(void)
{
    struct can_data_snapshot snap;

    app_state_get_can_data(&snap);

    zone_render_temperatures(&snap);
    zone_render_status(&snap);
    zone_render_voltage(&snap);

    int ret = led_strip_update_rgb(s_strip, s_pixels, LIGHTING_NUM_PIXELS);
    if (ret != 0) {
        LOG_ERR("led_strip_update_rgb failed: %d", ret);
    }
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
        /*
         * The strip follows the display: on the EV driving screen it turns
         * into an instrument, everywhere else it is decoration. The active
         * screen comes from app_state, which the App Layer maintains from the
         * UI's screen-change events — this module never talks to the UI.
         */
        if (app_state_get_active_screen() == SCREEN_EV_DRIVING) {
            zone_step();
        } else {
            gear_step(&phase);
        }

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

    LOG_INF("Lighting module initialized");
}
