# FSE DCU 2612 — Event System

## Overview

The event system is built on **Zephyr Zbus** (`CONFIG_ZBUS=y`). All inter-module
communication — both upward events and downward commands — uses Zbus channels.
This gives a symmetric, fully decoupled architecture: no module needs to include
another module's header to communicate with it.

All channel declarations live in `src/services/event_bus/event_bus.h`.  
All event/command type enums and payload structs live in `src/services/event_bus/events.h`.  
Every module that publishes or subscribes includes both headers.

---

## Communication Model

```
  ┌─────────────────────────────────────────────────────────┐
  │                      App Layer                          │
  │   Subscribes: can_status, can_data, ui_input,           │
  │               safety, settings, feedback                │
  │   Publishes:  ui_cmd, lighting_cmd, audio_cmd, can_tx   │
  └─────────────────────────────────────────────────────────┘
       ▲ upward events           ▼ downward commands
  ┌────┴────┐  ┌───────┐  ┌──────────┐  ┌───────┐  ┌──────────┐
  │   CAN   │  │  UI   │  │ Lighting │  │ Audio │  │ Settings │
  └─────────┘  └───────┘  └──────────┘  └───────┘  └──────────┘
       ▲                       ▲              ▲
       └───────── safety_chan direct subscriptions (fast-path) ──┘
```

**Upward channels** (Module → App): carry events — things that happened.  
**Downward channels** (App → Module): carry commands — things to do.  
**Safety fast-path**: Lighting and Audio subscribe directly to `safety_chan` in addition
to App, enabling immediate visual/audio override without waiting for App to process the
event first (see rationale below).

---

## Channel Overview

### Upward: Module → App

| Channel | Publisher | Subscribers | Purpose |
|---------|-----------|-------------|---------|
| `can_status_chan` | CAN | App, (logging) | CAN connectivity and bus error events |
| `can_data_chan` | CAN | App, (logging) | Periodic decoded signal snapshot |
| `ui_input_chan` | UI | App | Driver input events |
| `safety_chan` | CAN | App, Lighting, Audio | Safety-critical state changes |
| `settings_chan` | Settings | App | Settings lifecycle events |
| `feedback_chan` | Lighting, Audio | App | Effect completion signals |

### Downward: App → Module

| Channel | Publisher | Subscriber | Purpose |
|---------|-----------|------------|---------|
| `ui_cmd_chan` | App | UI | Screen navigation and data updates |
| `lighting_cmd_chan` | App | Lighting | Zone state and effect commands |
| `audio_cmd_chan` | App | Audio | Sound effect commands |
| `can_tx_cmd_chan` | App | CAN | Transmit mission/RTD frames |

---

## Upward Channel Definitions

### `can_status_chan`

```c
enum can_status_type {
    CAN_STATUS_CONNECTED,
    CAN_STATUS_DISCONNECTED,
    CAN_STATUS_TIMEOUT,   // per-message timeout; msg_id identifies which message
    CAN_STATUS_BUS_OFF,
};

struct can_status_event {
    enum can_status_type type;
    uint32_t msg_id;      // non-zero only for CAN_STATUS_TIMEOUT
};
```

### `can_data_chan`

Published at ~100 ms or on significant value change. Struct grows as the DBC is finalized.

```c
struct can_data_snapshot {
    /* Drivetrain */
    float motor_rpm;
    float motor_temp_c;

    /* HV Accumulator */
    float hv_soc_pct;        // 0.0 – 100.0
    float hv_voltage_v;
    float hv_temp_max_c;

    /* LV Accumulator */
    float lv_voltage_v;

    /* Staleness detection */
    int64_t timestamp_ms;    // k_uptime_get() at time of decode
};
```

### `ui_input_chan`

```c
enum ui_input_type {
    UI_INPUT_CONFIRM,           // OK / encoder button press
    UI_INPUT_BACK,              // Abort / back button
    UI_INPUT_ENCODER_UP,        // Encoder rotated clockwise
    UI_INPUT_ENCODER_DOWN,      // Encoder rotated counter-clockwise
    UI_INPUT_ENCODER_CLICK,     // Encoder button (= CONFIRM in most contexts)
    UI_INPUT_MISSION_SELECTED,  // Driver completed mission selection
};

struct ui_input_event {
    enum ui_input_type type;
    union {
        enum mission_id mission;   // valid when type == UI_INPUT_MISSION_SELECTED
    } data;
};
```

### `safety_chan`

```c
enum safety_event_type {
    SAFETY_IMD_FAULT,
    SAFETY_AMS_FAULT,
    SAFETY_SHUTDOWN_OPEN,
    SAFETY_SHUTDOWN_CLOSED,
    SAFETY_TS_OFF,
    SAFETY_TS_ACTIVE,
};

struct safety_event {
    enum safety_event_type type;
};
```

### `settings_chan`

```c
enum settings_event_type {
    SETTINGS_LOADED,         // Initial NVM load complete (on boot)
    SETTINGS_UPDATED,        // A setting was changed and persisted
    SETTINGS_FACTORY_RESET,  // All settings cleared to defaults
};

struct settings_event {
    enum settings_event_type type;
};
```

### `feedback_chan`

```c
enum feedback_type {
    FEEDBACK_LIGHTING_DONE,  // Transient lighting effect finished
    FEEDBACK_AUDIO_DONE,     // Transient audio effect finished
};

struct feedback_event {
    enum feedback_type type;
};
```

---

## Downward Channel Definitions

### `ui_cmd_chan`

```c
enum ui_cmd_type {
    UI_CMD_SET_SCREEN,        // Navigate to a named screen
    UI_CMD_UPDATE_TELEMETRY,  // Push new CAN values for rendering
    UI_CMD_SET_STATUS,        // Update connection/safety indicator flags
};

struct ui_status_flags {
    bool can_connected;
    bool safety_fault;
    bool ts_active;
};

struct ui_cmd {
    enum ui_cmd_type type;
    union {
        enum screen_id screen;                  // UI_CMD_SET_SCREEN
        struct can_data_snapshot telemetry;     // UI_CMD_UPDATE_TELEMETRY
        struct ui_status_flags status;          // UI_CMD_SET_STATUS
    } data;
};
```

### `lighting_cmd_chan`

```c
enum lighting_cmd_type {
    LIGHTING_CMD_SET_STATE,     // Set persistent BASE state for a zone
    LIGHTING_CMD_PLAY_EFFECT,   // Play transient EFFECT on a zone
    LIGHTING_CMD_STOP_EFFECT,   // Stop active effect, revert to BASE
    LIGHTING_CMD_CLEAR_OVERRIDE,// Release OVERRIDE layer, return to EFFECT/BASE
    LIGHTING_CMD_CLEAR_ALL,     // Reset all zones to idle/off
};

struct lighting_cmd {
    enum lighting_cmd_type type;
    enum lighting_zone_id zone;  // target zone, or LIGHTING_ZONE_ALL
    enum lighting_layer layer;   // LIGHTING_LAYER_BASE / EFFECT / OVERRIDE
    union {
        struct {
            enum lighting_state_id state;
            uint8_t value_pct;   // for progress-bar zones (0–100)
        } state;
        struct {
            enum lighting_effect_id effect;
            uint32_t duration_ms;  // 0 = loop continuously until stopped
        } effect;
    } data;
};
```

### `audio_cmd_chan`

```c
enum audio_cmd_type {
    AUDIO_CMD_PLAY_EFFECT,  // Play a named sound effect
    AUDIO_CMD_STOP,         // Stop any active effect immediately
};

struct audio_cmd {
    enum audio_cmd_type type;
    enum audio_effect_id effect;  // only used for AUDIO_CMD_PLAY_EFFECT
};
```

### `can_tx_cmd_chan`

```c
enum can_tx_cmd_type {
    CAN_TX_CMD_SEND_MISSION,      // Transmit mission selection frame
    CAN_TX_CMD_SEND_RTD_REQUEST,  // Transmit RTD request frame
};

struct can_tx_cmd {
    enum can_tx_cmd_type type;
    union {
        enum mission_id mission;  // CAN_TX_CMD_SEND_MISSION
    } data;
};
```

---

## Usage Patterns

### Publishing a command (App → Module)

```c
// App triggers a lighting effect on all zones:
struct lighting_cmd cmd = {
    .type  = LIGHTING_CMD_PLAY_EFFECT,
    .zone  = LIGHTING_ZONE_ALL,
    .layer = LIGHTING_LAYER_EFFECT,
    .data.effect = {
        .effect      = LIGHTING_EFFECT_CONFIRM,
        .duration_ms = 200,
    },
};
zbus_chan_pub(&lighting_cmd_chan, &cmd, K_NO_WAIT);
```

### Subscribing to a command channel (Module side)

```c
// In lighting module:
ZBUS_SUBSCRIBER_DEFINE(lighting_subscriber, 4);
ZBUS_CHAN_ADD_OBS(lighting_cmd_chan, &lighting_subscriber, 0);
ZBUS_CHAN_ADD_OBS(safety_chan,       &lighting_subscriber, 0); // safety fast-path

static void lighting_thread_fn(void *a, void *b, void *c)
{
    const struct zbus_channel *chan;
    while (!zbus_sub_wait(&lighting_subscriber, &chan, K_NO_WAIT)) {
        if (chan == &lighting_cmd_chan) {
            struct lighting_cmd cmd;
            zbus_chan_read(&lighting_cmd_chan, &cmd, K_NO_WAIT);
            lighting_handle_cmd(&cmd);
        } else if (chan == &safety_chan) {
            struct safety_event evt;
            zbus_chan_read(&safety_chan, &evt, K_NO_WAIT);
            lighting_handle_safety_override(&evt);
        }
    }
    // ... animation tick (k_timer driven)
}
```

### Publishing an upward event (Module → App)

```c
// CAN module publishes a timeout:
struct can_status_event evt = {
    .type   = CAN_STATUS_TIMEOUT,
    .msg_id = CAN_MSG_MOTOR_STATUS_ID,
};
zbus_chan_pub(&can_status_chan, &evt, K_NO_WAIT);
```

---

## Design Rationale

### Why Zbus for both directions (not direct API calls for commands)?

Direct function calls from App to modules would require App to `#include` each module's
header, creating compile-time coupling. With Zbus command channels:
- App only includes `events.h` — no dependency on module headers
- Modules are fully interchangeable as long as they subscribe to their command channel
- Additional subscribers can be added to any channel (e.g., a diagnostics logger on
  `can_data_chan`) without touching publishers

### Why does Lighting subscribe directly to `safety_chan` instead of waiting for App?

The command path for a safety event via App is:
```
CAN ISR → can_rx_thread → safety_chan → app_thread → lighting_cmd_chan → lighting_thread
```
This is two thread context switches. In practice this is sub-millisecond and acceptable.
The direct subscription is nonetheless kept as an explicit design choice for the safety
fast-path, ensuring the lighting override is activated even if the App thread is briefly
busy. This is a documented exception to the Dirigent pattern: Lighting and Audio may react
to `safety_chan` directly, but must not modify `app_state` (read-only reaction only).

### Why is Settings hybrid (sync API reads, Zbus writes)?

Read values are needed synchronously — a caller asking `settings_get_brightness()` needs
the value immediately to construct a command. A Zbus roundtrip for reads would be
unnecessarily complex. Write operations trigger a `settings_chan` event so App can react
(e.g., apply new brightness to UI and lighting), which is naturally asynchronous.

### Why not `k_event` or `k_msgq`?

`k_event` has a hard 32-flag limit and carries no payload.  
`k_msgq` is point-to-point — adding a second subscriber (e.g., logging) requires a second
queue and a dispatcher.  
Zbus handles multiple subscribers natively and carries typed payloads.
