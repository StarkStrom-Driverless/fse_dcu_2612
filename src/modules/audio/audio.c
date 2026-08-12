/**
 * @file        audio.c
 * @brief       Audio module — piezo buzzer control
 *
 * @ingroup     dcu_audio
 *
 * @details     Owns the piezo GPIO and a thread (priority 6) that blocks on
 *              audio_cmd_chan and switches the buzzer accordingly.
 *
 *              ### Effects are not implemented
 *              The piezo hangs on a plain GPIO, not on a PWM channel, so the
 *              module can only turn it on or off — it cannot produce a pitch,
 *              a pattern or a duration.  enum audio_effect_id is therefore
 *              ignored: every AUDIO_CMD_PLAY_EFFECT drives the pin high and
 *              AUDIO_CMD_STOP drives it low, and the buzzer keeps sounding
 *              until a stop command arrives.
 *
 *              In practice the App Layer supplies exactly that pairing: it
 *              mirrors the vehicle's rtd_sound CAN signal, so the vehicle,
 *              not this module, decides how long the sound lasts.
 *
 *              A thread rather than a Zbus listener: gpio_pin_set_dt() may
 *              block on some drivers, which a listener callback running in the
 *              publisher's context must not do.
 *
 * @author      Mario Wegmann <mario.wegmann@web.de>
 * @date        Created: 2026-07-08
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
 * 0.1.0    2026-07-08  Mario Wegmann   Initial creation
 * ─────────────────────────────────────────────────────────────────────────────────────────────────
 */

/* ── Corresponding Header ────────────────────────────────────────────────────────────────────── */

#include "modules/audio/audio.h"

/* ── Zephyr Includes ─────────────────────────────────────────────────────────────────────────── */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/logging/log.h>

/* ── Project Includes ────────────────────────────────────────────────────────────────────────── */

#include "services/event_bus/event_bus.h"
#include "services/event_bus/events.h"

/* ── Zephyr Logging ──────────────────────────────────────────────────────────────────────────── */

LOG_MODULE_REGISTER(audio_module, CONFIG_LOG_DEFAULT_LEVEL);


/* ── Private Macros & Constants ──────────────────────────────────────────────────────────────── */

/** @brief Stack size for the audio thread. Small — the thread only toggles a pin. */
#define AUDIO_THREAD_STACK_SIZE     512U

/** @brief Scheduling priority for the audio thread. */
#define AUDIO_THREAD_PRIORITY       6

/** @brief Devicetree node of the piezo, resolved through the `piezo` alias. */
#define PIEZO_NODE  DT_ALIAS(piezo)


/* ── Private Variables ───────────────────────────────────────────────────────────────────────── */

/** @brief GPIO the piezo hangs on; active level comes from the devicetree flags. */
static const struct gpio_dt_spec s_piezo = GPIO_DT_SPEC_GET(PIEZO_NODE, gpios);

/** @brief Thread control block for the audio thread. */
static struct k_thread s_audio_thread;

/** @brief Stack storage for the audio thread. */
static K_THREAD_STACK_DEFINE(s_audio_stack, AUDIO_THREAD_STACK_SIZE);


/* ── Zbus Subscriber ─────────────────────────────────────────────────────────────────────────── */

/**
 * @brief Subscriber for audio_cmd_chan (App Layer → Audio module).
 *
 * Queue depth 4 is ample: commands arrive on rtd_sound edges only.
 */
ZBUS_SUBSCRIBER_DEFINE(audio_sub, 4);
ZBUS_CHAN_ADD_OBS(audio_cmd_chan, audio_sub, 0);


/* ── Private Function Implementations ───────────────────────────────────────────────────────── */

/**
 * @brief Audio thread entry point.
 *
 * Blocks on the subscriber queue and switches the piezo GPIO per command.
 *
 * The K_MSEC(10) on the read is deliberate: with K_NO_WAIT the read can fail
 * with -EAGAIN when the publisher still holds the channel mutex, which happens
 * whenever a lower-priority thread publishes and this thread preempts it. The
 * same reasoning as in app.c applies here.
 *
 * A failed read is skipped silently — the next command re-establishes the
 * intended state, so a lost one cannot leave the buzzer stuck.
 *
 * @param p1  Unused.
 * @param p2  Unused.
 * @param p3  Unused.
 */
static void audio_thread_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    const struct zbus_channel *chan;

    while (true) {
        int rc = zbus_sub_wait(&audio_sub, &chan, K_FOREVER);
        if (rc != 0) {
            LOG_ERR("zbus_sub_wait error: %d", rc);
            continue;
        }

        struct audio_cmd cmd;
        if (zbus_chan_read(chan, &cmd, K_MSEC(10)) != 0) {
            continue;
        }

        switch (cmd.type) {
        case AUDIO_CMD_PLAY_EFFECT:
            /* cmd.effect is ignored — see the file header. */
            gpio_pin_set_dt(&s_piezo, 1);
            LOG_DBG("Buzzer ON");
            break;

        case AUDIO_CMD_STOP:
            gpio_pin_set_dt(&s_piezo, 0);
            LOG_DBG("Buzzer OFF");
            break;

        default:
            LOG_WRN("Unknown audio_cmd type: %d", (int)cmd.type);
            break;
        }
    }
}

/* ── Public Function Implementations ─────────────────────────────────────────────────────────── */

void audio_module_init(void)
{
    if (!gpio_is_ready_dt(&s_piezo)) {
        LOG_ERR("Piezo GPIO not ready");
        return;
    }

    int ret = gpio_pin_configure_dt(&s_piezo, GPIO_OUTPUT_INACTIVE);
    if (ret != 0) {
        LOG_ERR("Piezo GPIO configure failed: %d", ret);
        return;
    }

    k_thread_create(&s_audio_thread, s_audio_stack,
                    K_THREAD_STACK_SIZEOF(s_audio_stack),
                    audio_thread_fn, NULL, NULL, NULL,
                    AUDIO_THREAD_PRIORITY, 0, K_NO_WAIT);
    k_thread_name_set(&s_audio_thread, "audio");

    LOG_INF("Audio module initialised");
}
