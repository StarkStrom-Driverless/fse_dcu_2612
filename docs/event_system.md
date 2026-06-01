# FSE DCU 2612 — Event System

## Overview

The event system is built on **Zephyr Zbus** (`CONFIG_ZBUS=y`). Events are grouped into named **channels**, each carrying a single typed message struct. This replaces discrete event flags (`k_event`) with a structured, payload-capable, scalable mechanism.

All channel declarations live in `src/services/event_bus/event_bus.h`.  
All event types and payload structs live in `src/services/event_bus/events.h`.  
Every module that publishes or subscribes includes both headers.

---

## Channel Overview

| Channel | Published by | Primary Subscriber | Purpose |
|---------|-------------|-------------------|---------|
| `can_status_chan` | CAN module | App | CAN connectivity and bus error events |
| `can_data_chan` | CAN module | App | Periodic decoded signal snapshot |
| `ui_input_chan` | UI module | App | Driver input events |
| `safety_chan` | CAN module | App | IMD, AMS, shutdown circuit, TS state |
| `settings_chan` | Settings module | App | Settings lifecycle events |
| `feedback_chan` | Lighting, Audio | App | Effect completion signals |

**Direction rule:** Zbus events flow from modules **up** to the App Layer only.  
The App Layer issues commands to modules via **direct API calls**, not via Zbus.

---

## Channel Definitions

### `can_status_chan`

```c
enum can_status_type {
    CAN_STATUS_CONNECTED,
    CAN_STATUS_DISCONNECTED,
    CAN_STATUS_TIMEOUT,     // per-message timeout exceeded
    CAN_STATUS_BUS_OFF,
};

struct can_status_event {
    enum can_status_type type;
    uint32_t msg_id;        // relevant for TIMEOUT; 0 otherwise
};
```

### `can_data_chan`

Published at ~100 ms or on significant value change. Fields are extended as the vehicle DBC is finalized.

```c
struct can_data_snapshot {
    /* Drivetrain */
    float motor_rpm;
    float motor_temp_c;

    /* HV Accumulator */
    float hv_soc_pct;
    float hv_voltage_v;
    float hv_temp_max_c;

    /* LV Accumulator */
    float lv_voltage_v;

    /* Timestamp (Zephyr uptime ms) — lets App detect stale data */
    int64_t timestamp_ms;
};
```

### `ui_input_chan`

```c
enum ui_input_type {
    UI_INPUT_CONFIRM,           // OK / encoder button short press
    UI_INPUT_BACK,              // Abort / back button
    UI_INPUT_ENCODER_UP,        // Encoder rotated clockwise
    UI_INPUT_ENCODER_DOWN,      // Encoder rotated counter-clockwise
    UI_INPUT_ENCODER_CLICK,     // Encoder button (same as CONFIRM in most contexts)
    UI_INPUT_MISSION_SELECTED,  // Driver confirmed a mission in the selection screen
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
    SETTINGS_LOADED,         // Initial load from NVM complete (on boot)
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

## Usage Pattern

### Publishing (module side)

```c
#include "services/event_bus/event_bus.h"
#include "services/event_bus/events.h"

// Example: CAN module detects timeout
struct can_status_event evt = {
    .type   = CAN_STATUS_TIMEOUT,
    .msg_id = CAN_MSG_MOTOR_STATUS_ID,
};
zbus_chan_pub(&can_status_chan, &evt, K_NO_WAIT);
```

### Subscribing (App thread)

```c
ZBUS_SUBSCRIBER_DEFINE(app_subscriber, 8);  // message queue depth 8

// Register channels this subscriber listens to:
ZBUS_CHAN_ADD_OBS(can_status_chan,  &app_subscriber, 0);
ZBUS_CHAN_ADD_OBS(can_data_chan,    &app_subscriber, 0);
ZBUS_CHAN_ADD_OBS(ui_input_chan,    &app_subscriber, 0);
ZBUS_CHAN_ADD_OBS(safety_chan,      &app_subscriber, 0);
ZBUS_CHAN_ADD_OBS(settings_chan,    &app_subscriber, 0);
ZBUS_CHAN_ADD_OBS(feedback_chan,    &app_subscriber, 0);

// App thread event loop:
const struct zbus_channel *chan;
while (!zbus_sub_wait(&app_subscriber, &chan, K_FOREVER)) {
    if (chan == &can_status_chan) {
        struct can_status_event evt;
        zbus_chan_read(&can_status_chan, &evt, K_NO_WAIT);
        app_handle_can_status(&evt);
    } else if (chan == &ui_input_chan) {
        struct ui_input_event evt;
        zbus_chan_read(&ui_input_chan, &evt, K_NO_WAIT);
        app_handle_ui_input(&evt);
    }
    // ... other channels
}
```

---

## Design Rationale

**Why not `k_event`?**  
`k_event` is a 32-bit bitmask — hard limit of 32 flags, no payload. With ~6 meaningful event categories and structured payloads needed (e.g., which message timed out, which mission was selected), Zbus is the correct tool.

**Why not `k_msgq` per module?**  
`k_msgq` is point-to-point. Zbus supports multiple subscribers on the same channel with minimal boilerplate, which is needed as the system grows (e.g., a future logging subscriber on `can_data_chan`).

**Why only one subscriber (App)?**  
The Dirigent pattern deliberately funnels all decisions through the App Layer. Modules do not subscribe to each other's channels. If a secondary subscriber is needed in the future (e.g., diagnostics logging), it can be added to any channel without changing publishers.
