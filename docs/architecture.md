# FSE DCU 2612 — Software Architecture

## System Overview

The DCU (Driver Control Unit) is the in-car driver display for a Formula Student Electric vehicle. It reads CAN bus data, presents mission-critical information to the driver, manages mission selection, controls indicator LEDs and audio, and handles all driver input.

**Hardware interfaces:**

| Interface | Purpose |
|-----------|---------|
| SPI Display | LVGL UI output |
| CAN Bus | Bidirectional vehicle communication |
| Rotary Encoders + Buttons | Driver input |
| PWM LEDs + Piezo | Status indicators and audio feedback |
| APA102 SPI LEDs | Addressable LED strip |

**Target platform:** Custom board `fse_pb`, Zephyr RTOS, LVGL.

---

## Architecture Pattern: App as Dirigent

The App Layer is the sole coordinator and the only writer of `app_state`. Modules are passive: they expose synchronous APIs and publish events upward via Zbus. The App Layer subscribes to module events, decides what to do, updates `app_state`, and calls module APIs.

```
                      ┌───────────────────┐
                      │     App Layer     │
                      │  (Dirigent +      │
                      │  State Machine)   │
                      └────────┬──────────┘
          ┌──────────────────  │  ──────────────────────┐
          │           ┌────────┴────────┐               │
          ▼           ▼                 ▼               ▼
     ┌─────────┐ ┌────────┐      ┌──────────┐   ┌──────────┐
     │   CAN   │ │   UI   │      │ Lighting │   │  Audio   │
     │ Module  │ │ Module │      │  Module  │   │  Module  │
     └────┬────┘ └───┬────┘      └────┬─────┘   └─────┬────┘
          │          │                │               │
          └──────────┴────────────────┴───────────────┘
                         Zbus channels (events up)
                    App calls module APIs directly (commands down)
```

**Design rules:**

1. Only `src/app/` writes to `app_state`. All other modules receive data via function arguments in API calls.
2. Modules do not read `app_state`. They receive commands from the App Layer.
3. Modules publish events upward via Zbus. The App thread subscribes and reacts.
4. Module APIs are synchronous command functions. Zbus events are asynchronous signals.

---

## Directory Structure

```
fse_dcu_2612/
│
├── src/
│   ├── main.c
│   │
│   ├── app/
│   │   ├── app.c / app.h               # Coordinator, init sequence, event dispatch
│   │   ├── app_state.c / app_state.h   # State struct + read-only accessor functions
│   │   └── state_machine.c / state_machine.h  # Zephyr SMF states
│   │
│   ├── modules/
│   │   ├── ui/
│   │   │   ├── ui.c / ui.h
│   │   │   ├── screens/                # One .c/.h pair per screen
│   │   │   │   ├── screen_debug.c / .h
│   │   │   │   ├── screen_pre_rtd.c / .h
│   │   │   │   ├── screen_rtd.c / .h
│   │   │   │   ├── screen_post_rtd.c / .h
│   │   │   │   └── screen_error.c / .h
│   │   │   └── ui_styles.c / ui_styles.h
│   │   │
│   │   ├── can/
│   │   │   ├── can.c / can.h
│   │   │   └── can_signals.h           # Compile-time signal definitions (from DBC)
│   │   │
│   │   ├── lighting/
│   │   │   ├── lighting.c / lighting.h
│   │   │   └── lighting_effects.c / lighting_effects.h
│   │   │
│   │   ├── audio/
│   │   │   ├── audio.c / audio.h
│   │   │   └── audio_effects.c / audio_effects.h
│   │   │
│   │   └── settings/
│   │       ├── settings.c / settings.h
│   │       └── settings_schema.h       # Key names, defaults, validation
│   │
│   └── services/
│       └── event_bus/
│           ├── event_bus.h             # ZBUS_CHAN_DECLARE for all channels
│           └── events.h               # Event type enums and payload structs
│
├── samples/
│   ├── can_loopback/
│   ├── lvgl_custom_style/
│   ├── encoder_navigation/
│   └── apa102_demo/
│
├── boards/
│   └── custom_board/
│
├── docs/
│   ├── architecture.md     ← this file
│   ├── modules.md
│   ├── event_system.md
│   └── thread_model.md
│
├── prj.conf
├── Kconfig
└── CMakeLists.txt
```

**Structural rules:**

- No top-level `include/` directory. Headers live next to their `.c` files.
- `services/event_bus/` is the single source of truth for all Zbus channel definitions. Every module includes `events.h` and `event_bus.h`.
- `services/storage/` is omitted. NVM access is handled exclusively via the Zephyr settings subsystem inside `modules/settings/`.

---

## Key Technology Decisions

| Concern | Decision | Rationale |
|---------|----------|-----------|
| Event system | Zbus (`CONFIG_ZBUS=y`) | Typed payloads, no 32-event limit of `k_event`, structured subscriber model |
| State machine | Zephyr SMF (`smf.h`) | Native Zephyr, hierarchical states, no external dependency |
| Input handling | Zephyr/LVGL DT integration | Encoder mapped via devicetree to `zephyr,lvgl-encoder-input`; reimplementing would work against the framework |
| CAN signal definitions | Compile-time header (`can_signals.h`) | No runtime DBC parsing on MCU; generated from DBC via `cantools` or hand-coded |
| NVM/Persistent storage | Zephyr settings subsystem | Abstracted backend (NVS/FCB), versioning, factory reset support |

---

## Operating Modes

The state machine in `app/state_machine.c` manages four operating modes. Mission selection is orthogonal to the operating mode (see below).

| Mode | Display Behavior |
|------|-----------------|
| `DEBUG` | Full navigation: CAN signal monitor, all screens accessible via encoder |
| `PRE_RTD` | Guided pre-drive checklist; navigation restricted to checklist items |
| `RTD` | Mission active; live telemetry display; mode transitions locked |
| `POST_RTD` | Confirm return to idle; brief summary; then back to `DEBUG` |

## Mission vs. Operating Mode

These are two independent dimensions:

- **Mission** (`app_state.mission.selected_mission`): The Formula Student discipline the driver selects (Acceleration, Skidpad, Autocross, Endurance, Inspection, Manual Driving). The DCU sends the selected mission over CAN to the vehicle. The display does not otherwise change behavior per mission — no separate module is needed.
- **Operating Mode** (`app_state.system.operating_mode`): Governs how the display behaves. The vehicle does not need to know the operating mode.

---

## Initialization Order

```
main()
 ├── settings_module_init()     // Load NVM settings first (other modules may read defaults)
 ├── can_module_init()          // Register CAN RX filters
 ├── lighting_module_init()     // Set LEDs to default/off state
 ├── audio_module_init()
 ├── ui_module_init()           // Create initial screen (before threads start)
 └── [start threads]
      ├── app_thread
      ├── can_rx_thread
      ├── lvgl_thread
      ├── lighting_thread
      └── audio_thread
```
