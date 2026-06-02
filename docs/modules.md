# FSE DCU 2612 — Module Specifications

## App Layer (`src/app/`)

### Responsibility
- Single coordinator for all modules (Dirigent pattern)
- Sole writer of `app_state`
- Manages operating modes via Zephyr SMF state machine
- Subscribes to all upward Zbus channels and translates events into downward commands
- Manages mission selection lifecycle (select → confirm → transmit via CAN → lock)

### Components

| File | Role |
|------|------|
| `app.c / app.h` | Module init sequence, Zbus subscriber loop, event-to-command dispatch |
| `app_state.c / app_state.h` | State struct definition; `app_state_get_*()` read-only accessors |
| `state_machine.c / state_machine.h` | Zephyr SMF states: DEBUG, PRE_RTD, RTD, POST_RTD |

### Zbus Subscriptions (inbound events)
- `can_status_chan` — react to connectivity changes and bus errors
- `can_data_chan` — forward decoded values to UI via `ui_cmd_chan`
- `ui_input_chan` — drive state machine transitions, trigger lighting/audio
- `safety_chan` — trigger safety states; also subscribed by Lighting/Audio directly (see event_system.md)
- `settings_chan` — apply settings after load/update
- `feedback_chan` — sequence dependent effects (e.g., play audio after lighting effect)

### Zbus Publications (outbound commands)
- `ui_cmd_chan` — set screen, push telemetry data, update status flags
- `lighting_cmd_chan` — set zone state, play effect, stop effect, clear all
- `audio_cmd_chan` — play effect, stop
- `can_tx_cmd_chan` — send mission frame, send RTD request frame

---

## CAN Module (`src/modules/can/`)

### Responsibility
- Configure and manage CAN RX filters
- Receive frames via ISR → `k_msgq` → decode in `can_rx_thread`
- Decode signals using compile-time definitions in `can_signals.h`
- Detect per-message timeouts via `k_timer` per message ID
- Transmit frames in response to `can_tx_cmd_chan` commands and periodic timer
- Publish decoded data and status events via Zbus
- Keep all raw frame handling internal — no raw CAN frames ever leave this module

### `can_signals.h`

Signal encodings are defined at compile time, generated from the vehicle DBC file via
`cantools` (Python) or equivalent pre-build step. No runtime DBC parsing on the MCU.

```c
// Pattern (generated or hand-coded from DBC):
#define CAN_MSG_MOTOR_STATUS_ID       0x201U
#define CAN_MSG_MOTOR_STATUS_TIMEOUT  200U    // ms — timeout detection threshold

#define CAN_SIG_MOTOR_RPM_START       16      // bit start position
#define CAN_SIG_MOTOR_RPM_LEN         16      // bit length
#define CAN_SIG_MOTOR_RPM_FACTOR      0.1f
#define CAN_SIG_MOTOR_RPM_OFFSET      0.0f
```

**Why semantic TX functions instead of a generic `can_send_frame()`?**
The CAN module owns the DBC knowledge. Exposing `can_send_mission(enum mission_id)` keeps
frame construction (ID, byte packing, DLC) inside the CAN module. The App Layer only
communicates intent — it does not know or care about CAN frame encoding. A generic
`can_send_frame()` would leak DBC details into the App Layer, breaking encapsulation.

### Zbus Subscriptions (inbound commands)
- `can_tx_cmd_chan` — `CAN_TX_CMD_SEND_MISSION`, `CAN_TX_CMD_SEND_RTD_REQUEST`

### Zbus Publications (outbound events)
- `can_status_chan` — `CAN_STATUS_CONNECTED`, `DISCONNECTED`, `TIMEOUT`, `BUS_OFF`
- `can_data_chan` — `struct can_data_snapshot` published at ~100 ms or on significant change
- `safety_chan` — IMD status, AMS status, shutdown circuit state, TS state decoded from CAN

---

## UI Module (`src/modules/ui/`)

### Responsibility
- LVGL theme, styles, and font definitions (`ui_styles.c`)
- Screen creation, destruction, and navigation
- Rendering data received via `ui_cmd_chan`
- Publishing semantic input events to `ui_input_chan` after LVGL processes hardware input

### Input Integration

Hardware input is handled via Zephyr/LVGL devicetree integration — a separate input module
would duplicate this. Encoder rotation and button presses are mapped in the devicetree:

```dts
// Rotary encoder → LVGL encoder group (devicetree)
encoder_input: encoder_input {
    compatible = "zephyr,lvgl-encoder-input";
    rotation-axis = <INPUT_REL_Y>;
    button-key = <INPUT_KEY_ENTER>;
};
```

LVGL processes these natively. The UI module then translates LVGL interaction results into
semantic Zbus events (`UI_INPUT_CONFIRM`, `UI_INPUT_MISSION_SELECTED`, etc.).

### Screens

| Screen | Active Mode | Purpose |
|--------|-------------|---------|
| `screen_debug` | DEBUG | CAN signal monitor, full encoder navigation |
| `screen_pre_rtd` | PRE_RTD | Guided pre-drive checklist + mission selection |
| `screen_rtd` | RTD | Live telemetry display |
| `screen_post_rtd` | POST_RTD | Return-to-idle confirmation |
| `screen_error` | any | Fatal error / safety fault display |

### Zbus Subscriptions (inbound commands)
- `ui_cmd_chan` — `UI_CMD_SET_SCREEN`, `UI_CMD_UPDATE_TELEMETRY`, `UI_CMD_SET_STATUS`

### Zbus Publications (outbound events)
- `ui_input_chan` — `UI_INPUT_CONFIRM`, `BACK`, `ENCODER_UP/DOWN`, `ENCODER_CLICK`,
  `MISSION_SELECTED`

---

## Lighting Module (`src/modules/lighting/`)

### Responsibility
- Maintain per-zone rendering state and active effects
- Execute animations via `lighting_thread` at 20 ms tick rate
- Output PWM duty cycles (status LEDs) and APA102 SPI frames (LED strip)
- Resolve conflicts between simultaneous commands using a layer priority model
- React directly to `safety_chan` for immediate override effects (safety fast-path)

---

### Effect vs. State

| Concept | Layer | Lifetime | Example |
|---------|-------|----------|---------|
| **State** | `BASE` | Persistent — stays until explicitly replaced | HV SoC progress bar at 72 % |
| **Effect** | `EFFECT` | Transient — runs for a duration, then zone reverts to BASE | Green flash on mission confirm |
| **Override** | `OVERRIDE` | Safety-critical — cannot be interrupted by BASE or EFFECT commands | All red on IMD fault |

**Rendering priority:** `OVERRIDE` > `EFFECT` > `BASE`

Each zone independently tracks all three layers. When an EFFECT finishes, the zone
automatically returns to its current BASE state. An OVERRIDE can only be cleared explicitly
(e.g., by a `LIGHTING_CMD_CLEAR_OVERRIDE` command from App or a direct `safety_chan`
subscription callback).

---

### Zone Layout (`lighting_zones.h`)

Physical LED layout and semantic zone assignment are defined in `lighting_zones.h` inside
the lighting module. The App Layer works exclusively with zone IDs — it never addresses
individual LED indices.

```
APA102 Strip Layout:
┌────────────────────────────────────────────────────────────────┐
│  [0..9]          [10]  [11]  [12]         [13..22]             │
│  ◀━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━▶  │
│  ZONE_LEFT        TS   AMS   IMD          ZONE_RIGHT           │
│  HV Temp %        Off              HV SoC %                    │
│                   ╰──── ZONE_CENTER ────╯                      │
└────────────────────────────────────────────────────────────────┘
```

```c
// lighting_zones.h
enum lighting_zone_id {
    LIGHTING_ZONE_LEFT,    // LEDs  0 ..  9 — HV Temperature progress bar
    LIGHTING_ZONE_CENTER,  // LEDs 10 .. 12 — TS Off / AMS / IMD status indicators
    LIGHTING_ZONE_RIGHT,   // LEDs 13 .. 22 — HV SoC progress bar
    LIGHTING_ZONE_ALL,     // All LEDs      — cross-zone broadcast
};

enum lighting_layer {
    LIGHTING_LAYER_BASE,      // Normal operational state (lowest priority)
    LIGHTING_LAYER_EFFECT,    // Transient animation (mid priority)
    LIGHTING_LAYER_OVERRIDE,  // Safety-critical (highest priority, not interruptible)
};
```

---

### Conflict Resolution

When multiple commands target the same zone simultaneously:

1. **Layer priority** (OVERRIDE > EFFECT > BASE) determines what is rendered.
2. A `BASE` command on a zone that has an active EFFECT still updates the BASE state —
   it takes effect as soon as the EFFECT finishes.
3. While an OVERRIDE is active, `BASE` and `EFFECT` commands are accepted and queued
   but not rendered until the OVERRIDE is cleared.
4. `LIGHTING_ZONE_ALL` is syntactic sugar — it broadcasts the command to all zones at the
   same layer. Each zone resolves its own priority independently.

**Cross-zone effect example:**  
Mission confirmed → `LIGHTING_CMD_PLAY_EFFECT` on `ZONE_ALL` / `LAYER_EFFECT` →
all zones flash green → each zone returns to its own BASE state on completion.

**Safety override example:**  
IMD fault → Lighting subscribes directly to `safety_chan` →
`LIGHTING_CMD_SET_STATE` on `ZONE_ALL` / `LAYER_OVERRIDE` with red solid →
no other command can change the display until the override is explicitly cleared.

---

### Zbus Subscriptions (inbound)
- `lighting_cmd_chan` — `SET_STATE`, `PLAY_EFFECT`, `STOP_EFFECT`, `CLEAR_OVERRIDE`,
  `CLEAR_ALL` (published by App)
- `safety_chan` — direct subscription for immediate OVERRIDE activation (bypasses App)

### Zbus Publications (outbound)
- `feedback_chan` — `FEEDBACK_LIGHTING_DONE` when a transient EFFECT finishes

---

### Lighting State Examples

| State ID | Zone | Visual |
|----------|------|--------|
| `LIGHTING_STATE_IDLE` | any | Off |
| `LIGHTING_STATE_PROGRESS` | LEFT / RIGHT | Color gradient 0–100 %, value from command payload |
| `LIGHTING_STATE_STATUS_OK` | CENTER | Green solid |
| `LIGHTING_STATE_STATUS_WARN` | CENTER | Amber solid |
| `LIGHTING_STATE_STATUS_FAULT` | CENTER | Red solid |
| `LIGHTING_STATE_RTD` | ALL | Solid green |

### Lighting Effect Examples

| Effect ID | Zone | Layer | Visual |
|-----------|------|-------|--------|
| `LIGHTING_EFFECT_CONFIRM` | ALL | EFFECT | Single green flash (200 ms) |
| `LIGHTING_EFFECT_ABORT` | ALL | EFFECT | Double red flash |
| `LIGHTING_EFFECT_STARTUP` | ALL | EFFECT | Sequential sweep |
| `LIGHTING_EFFECT_SAFETY_FAULT` | ALL | OVERRIDE | Fast red strobe (continuous) |

---

## Audio Module (`src/modules/audio/`)

### Responsibility
- Control piezo buzzer via PWM
- Play named sound effects (transient sequences and persistent alarms)
- Manage timing and tone sequences in `audio_thread`
- React directly to `safety_chan` for immediate alarm activation (safety fast-path)

### Zbus Subscriptions (inbound)
- `audio_cmd_chan` — `AUDIO_CMD_PLAY_EFFECT`, `AUDIO_CMD_STOP` (published by App)
- `safety_chan` — direct subscription for immediate alarm on safety fault

### Zbus Publications (outbound)
- `feedback_chan` — `FEEDBACK_AUDIO_DONE` when a transient effect finishes

### Audio Effect Examples

| Effect ID | Sound | Duration |
|-----------|-------|----------|
| `AUDIO_EFFECT_CONFIRM` | Single short beep | Transient |
| `AUDIO_EFFECT_ABORT` | Double short beep | Transient |
| `AUDIO_EFFECT_RTD_READY` | Three ascending tones | Transient |
| `AUDIO_EFFECT_SAFETY_FAULT` | Continuous alarm | Persistent (until stopped) |

---

## Settings Module (`src/modules/settings/`)

### Responsibility
- Register keys with Zephyr settings subsystem on init
- Provide typed get/set API with validation
- Supply default values when no persisted value exists
- Support factory reset (clear all keys to defaults)
- Handle schema versioning for future migrations

**Note:** Settings uses a hybrid model — **reads are synchronous** (direct function calls,
since callers need the value immediately) and **writes trigger a Zbus event** to notify
the App Layer. There is no `settings_cmd_chan` — the Settings module API is the write
interface.

### Key Definitions (`settings_schema.h`)

```c
// Pattern — single source of truth for all persisted keys:
#define SETTINGS_KEY_BRIGHTNESS      "dcu/brightness"
#define SETTINGS_DEFAULT_BRIGHTNESS  80U
#define SETTINGS_MIN_BRIGHTNESS       0U
#define SETTINGS_MAX_BRIGHTNESS     100U
```

### Direct API (reads + writes — synchronous)
- `settings_get_brightness(void)` → `uint8_t` — read current value
- `settings_set_brightness(uint8_t val)` — validate, persist, publish `SETTINGS_UPDATED`
- `settings_factory_reset(void)` — clear all keys, publish `SETTINGS_FACTORY_RESET`

### Zbus Publications (outbound events)
- `settings_chan` — `SETTINGS_LOADED` after boot; `SETTINGS_UPDATED` on any change;
  `SETTINGS_FACTORY_RESET` on reset
