#include "modules/audio/audio.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/logging/log.h>

#include "services/event_bus/event_bus.h"
#include "services/event_bus/events.h"

LOG_MODULE_REGISTER(audio_module, CONFIG_LOG_DEFAULT_LEVEL);

#define AUDIO_THREAD_STACK_SIZE     512U
#define AUDIO_THREAD_PRIORITY       6

#define PIEZO_NODE  DT_ALIAS(piezo)

static const struct gpio_dt_spec s_piezo = GPIO_DT_SPEC_GET(PIEZO_NODE, gpios);

static struct k_thread s_audio_thread;
static K_THREAD_STACK_DEFINE(s_audio_stack, AUDIO_THREAD_STACK_SIZE);

ZBUS_SUBSCRIBER_DEFINE(audio_sub, 4);
ZBUS_CHAN_ADD_OBS(audio_cmd_chan, audio_sub, 0);

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
