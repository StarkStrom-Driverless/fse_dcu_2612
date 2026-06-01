# FSE DCU 2612 — Module Specifications

## App Layer (`src/app/`)

### Responsibility
- Single coordinator for all modules (Dirigent pattern)
- Sole writer of `app_state`
- Manages operating modes via Zephyr SMF state machine
- Subscribes to all Zbus channels and translates events into module API calls
- Manages mission selection lifecycle (select → confirm → transmit via CAN → lock)

### Components

| File | Role |
|------|------|
| `app.c / app.h` | Module init sequence, Zbus subscriber loop, event-to-action dispatch |
| `app_state.c / app_state.h` | State struct definition; `app_state_get_*()` accessors for read-only access |
| `state_machine.c / state_machine.h` | Zephyr SMF states: DEBUG, PRE_RTD, RTD, POST_RTD |

### Zbus Subscriptions
- `can_status_chan` — react to connectivity changes and bus errors
- `can_data_chan` — forward decoded values to UI
- `ui_input_chan` — drive state machine transitions and trigger module effects
- `safety_chan` — trigger error states on IMD/AMS/shutdown events
- `settings_chan` — apply settings after load/update
- `feedback_chan` — sequence lighting and audio effects

### Module API Calls (outbound commands)
- `ui_set_screen(screen_id)` / `ui_update_telemetry(data)` / `ui_set_status(flags)`
- `lighting_set_effect(effect_id, params)` / `lighting_set_state(state_id)`
- `audio_play_effect(effect_id)`
- `can_send_mission(mission_id)` / `can_send_rtd_request()`

---

## CAN Module (`src/modules/can/`)

### Responsibility
- Configure and manage CAN RX filters
- Receive frames via ISR → `k_msgq` → decode in `can_rx_thread`
- Decode signals using compile-time definitions in `can_signals.h`
- Detect per-message timeouts
- Transmit periodic and event-triggered frames
- Publish decoded data and status events via Zbus
- Keep all raw frame handling internal — no raw frame events on the bus

### `can_signals.h`

Signal encodings are defined at compile time, generated from the vehicle DBC file via `cantools` (Python) or equivalent. No runtime DBC parsing on the MCU.

```c
// Pattern (generated or hand-coded):
#define CAN_MSG_MOTOR_STATUS_ID       0x201U
#define CAN_MSG_MOTOR_STATUS_TIMEOUT  200U   // ms

#define CAN_SIG_MOTOR_RPM_START       16
#define CAN_SIG_MOTOR_RPM_LEN         16
#define CAN_SIG_MOTOR_RPM_FACTOR      0.1f
#define CAN_SIG_MOTOR_RPM_OFFSET      0.0f
```

### Zbus Publications
- `can_status_chan` — `CAN_STATUS_CONNECTED`, `DISCONNECTED`, `TIMEOUT`, `BUS_OFF`
- `can_data_chan` — `struct can_data_snapshot` published at ~100 ms or on significant change
- `safety_chan` — safety signals decoded from CAN (IMD status, AMS status, shutdown circuit, TS state)

### App API (inbound commands)
- `can_send_mission(enum mission_id)` — send mission selection frame
- `can_send_rtd_request(void)` — send RTD request frame

---

## UI Module (`src/modules/ui/`)

### Responsibility
- LVGL theme, styles, and font definitions (`ui_styles.c`)
- Screen creation, destruction, and navigation
- Rendering data pushed by the App Layer
- Publishing high-level input events via Zbus after LVGL processes raw hardware input

### Input Integration

Hardware input is handled entirely by Zephyr/LVGL devicetree integration. A separate input module is not created since it would duplicate framework functionality.

```dts
// Devicetree example — rotary encoder mapped to LVGL encoder group
encoder_input: encoder_input {
    compatible = "zephyr,lvgl-encoder-input";
    rotation-axis = <INPUT_REL_Y>;
    button-key = <INPUT_KEY_ENTER>;
};
```

LVGL consumes encoder and button events natively. After LVGL processes them, the UI module publishes semantic events (`UI_INPUT_CONFIRM`, `UI_INPUT_MISSION_SELECTED`, etc.) via Zbus for the App Layer.

### Screens

| Screen | Active Mode | Purpose |
|--------|-------------|---------|
| `screen_debug` | DEBUG | CAN signal monitor, full navigation |
| `screen_pre_rtd` | PRE_RTD | Pre-drive checklist |
| `screen_rtd` | RTD | Live telemetry display |
| `screen_post_rtd` | POST_RTD | Confirm return to idle |
| `screen_error` | any | Fatal error / safety fault display |

Mission selection is a sub-screen within `screen_pre_rtd` or `screen_debug`.

### Zbus Publications
- `ui_input_chan` — `UI_INPUT_CONFIRM`, `BACK`, `ENCODER_UP/DOWN`, `ENCODER_CLICK`, `MISSION_SELECTED`

### App API (inbound commands)
- `ui_set_screen(screen_id)` — navigate to named screen
- `ui_update_telemetry(const struct can_data_snapshot *)` — push new values to active screen
- `ui_set_status(struct ui_status_flags)` — update connection/safety indicators

---

## Lighting Module (`src/modules/lighting/`)

### Responsibility
- Control PWM LEDs and APA102/SPI LED strip
- Execute named lighting effects (transient: flash, blink; persistent: solid color, breathing)
- Handle animation frame timing via `lighting_thread`
- Translate App commands into physical PWM duty cycles and APA102 frames

### App API (inbound commands)
- `lighting_set_effect(enum lighting_effect, struct lighting_params)` — play a named effect
- `lighting_set_state(enum lighting_state)` — set a persistent visual state

### Zbus Publications
- `feedback_chan` — `FEEDBACK_LIGHTING_DONE` when a transient effect finishes

### Lighting State Examples

| State ID | Visual Meaning |
|----------|---------------|
| `LIGHTING_IDLE` | Off / standby |
| `LIGHTING_PRE_RTD` | Slow amber pulse |
| `LIGHTING_RTD` | Solid green |
| `LIGHTING_ERROR` | Solid red |
| `LIGHTING_EFFECT_CONFIRM` | Short green flash (transient) |
| `LIGHTING_EFFECT_ABORT` | Short red flash (transient) |

---

## Audio Module (`src/modules/audio/`)

### Responsibility
- Control piezo buzzer via PWM
- Play named sound effects (transient: beep, tone sequence; persistent: alarm)
- Manage timing and sequences in `audio_thread`

### App API (inbound commands)
- `audio_play_effect(enum audio_effect)` — play a named effect

### Zbus Publications
- `feedback_chan` — `FEEDBACK_AUDIO_DONE` when a transient effect finishes

### Audio Effect Examples

| Effect ID | Sound |
|-----------|-------|
| `AUDIO_EFFECT_CONFIRM` | Single short beep |
| `AUDIO_EFFECT_ABORT` | Double short beep |
| `AUDIO_EFFECT_RTD_READY` | Three ascending tones |
| `AUDIO_EFFECT_ERROR` | Continuous alarm (persistent until cleared) |

---

## Settings Module (`src/modules/settings/`)

### Responsibility
- Register keys with Zephyr settings subsystem on init
- Provide typed get/set API with validation
- Supply default values when no persisted value exists
- Support factory reset (clear all keys to defaults)
- Handle schema versioning for future migrations

### Key Definitions (`settings_schema.h`)

All setting keys, types, defaults, and valid ranges are defined in `settings_schema.h`. This file is the single source of truth for what is persisted.

```c
// Pattern:
#define SETTINGS_KEY_BRIGHTNESS   "dcu/brightness"
#define SETTINGS_DEFAULT_BRIGHTNESS  80U
#define SETTINGS_MIN_BRIGHTNESS       0U
#define SETTINGS_MAX_BRIGHTNESS     100U
```

### App API (inbound commands)
- `settings_get_brightness(void)` — typed getter
- `settings_set_brightness(uint8_t val)` — typed setter with validation + persist
- `settings_factory_reset(void)` — clear all keys

### Zbus Publications
- `settings_chan` — `SETTINGS_LOADED` after initial load on boot; `SETTINGS_UPDATED` on any change; `SETTINGS_FACTORY_RESET` on reset
