# FSE DCU 2612 — Thread Model

## Thread Definitions

In Zephyr, lower priority number = higher urgency for preemptive threads (range 0–`CONFIG_NUM_PREEMPT_PRIORITIES-1`).

| Thread | Symbol | Priority | Stack | Wake Condition |
|--------|--------|----------|-------|----------------|
| App / State Machine | `app_thread` | 5 | 4096 B | `zbus_sub_wait()` — blocks until any subscribed channel publishes |
| CAN RX Processing | `can_rx_thread` | 2 | 2048 B | `k_msgq_get()` — fed by CAN ISR |
| CAN TX Periodic | `can_tx_work` (work queue) | 3 | 1024 B | `k_timer` callback at 10 ms |
| LVGL Render | `lvgl_thread` | 8 | 8192 B | `k_timer` at 20 ms (50 fps) |
| Lighting Animation | `lighting_thread` | 7 | 2048 B | `k_timer` at 20 ms |
| Audio | `audio_thread` | 6 | 1024 B | `k_sem` — posted by App when effect triggered |

---

## Thread Responsibilities

### `app_thread` (Priority 5)

The central event loop. Blocks on `zbus_sub_wait()` and wakes only when a module publishes to a subscribed channel. On each wakeup:
1. Identifies the channel and reads the message.
2. Calls the corresponding state machine transition or dispatch function.
3. Updates `app_state` as needed.
4. Calls module APIs to issue commands.

This thread must never perform blocking I/O or delay. All its work must complete quickly before the next event arrives.

### `can_rx_thread` (Priority 2)

Highest-priority application thread. The CAN ISR deposits raw frames into a `k_msgq`. This thread drains the queue, decodes each frame using `can_signals.h` macros, and publishes to `can_status_chan`, `can_data_chan`, or `safety_chan` as appropriate. Timeout detection is handled via per-message `k_timer` instances that are reset on each received frame; on expiry they post a `CAN_STATUS_TIMEOUT` event.

### `can_tx_work` (Priority 3)

A `k_work_delayable` item or `k_timer`-driven callback at 10 ms. Transmits periodic CAN frames (e.g., heartbeat, mission frame). One-shot TX requests from the App Layer (e.g., RTD request) are queued and sent from this context to avoid concurrent CAN device access.

### `lvgl_thread` (Priority 8)

Calls `lv_timer_handler()` every 20 ms. LVGL is not thread-safe; all LVGL object creation, modification, and deletion must happen from this thread. The UI module API functions (`ui_set_screen`, `ui_update_telemetry`) write to a shared buffer protected by a mutex; `lvgl_thread` reads from this buffer on each tick and applies updates to LVGL objects.

### `lighting_thread` (Priority 7)

Drives animation frame updates at 20 ms. Runs a Zbus subscriber loop on `lighting_cmd_chan`
and `safety_chan` (non-blocking, `K_NO_WAIT`) at the start of each tick to apply any
pending commands, then computes and outputs the next animation frame via PWM and APA102 SPI.
Long SPI transfers must use DMA to avoid blocking this thread for the full transfer duration.

### `audio_thread` (Priority 6)

Runs a Zbus subscriber loop on `audio_cmd_chan` and `safety_chan`. Blocks on
`zbus_sub_wait()` until a command arrives, then plays the effect sequence via PWM register
writes and `k_sleep` between tones. On completion publishes `FEEDBACK_AUDIO_DONE` to
`feedback_chan`.

---

## Stack Size Rationale

| Thread | Rationale |
|--------|-----------|
| `app_thread` (4096 B) | State machine nesting, dispatch function call depth, local event structs |
| `can_rx_thread` (2048 B) | Decode loops are flat; no deep nesting |
| `can_tx_work` (1024 B) | Minimal — only CAN API calls |
| `lvgl_thread` (8192 B) | LVGL allocates heavily on the stack during widget creation and rendering; 8 KB is a safe minimum |
| `lighting_thread` (2048 B) | APA102 frame buffer (~144 bytes for 48 LEDs × 4 bytes) + SPI call overhead |
| `audio_thread` (1024 B) | Minimal — only PWM register writes and `k_sleep` calls |

---

## Shared Resource Access

| Resource | Owner Thread | Access by Others | Protection |
|----------|-------------|-----------------|------------|
| `app_state` | `app_thread` (write) | Read via `app_state_get_*()` from any thread | `k_mutex` in accessor functions |
| LVGL objects | `lvgl_thread` (read/write) | UI module API writes to intermediate buffer | `k_mutex` between `ui_update_*` and `lvgl_thread` |
| CAN device | `can_rx_thread` (RX), `can_tx_work` (TX) | No sharing; Zephyr CAN driver serializes internally | Driver-internal |
| SPI (APA102) | `lighting_thread` | Exclusive; no other module uses this SPI bus | None needed (exclusive ownership) |
| PWM (LEDs) | `lighting_thread` | Exclusive | None needed |
| PWM (Piezo) | `audio_thread` | Exclusive | None needed |

---

## Inter-Thread Communication Summary

All inter-thread communication goes through Zbus channels. No direct API calls cross
module boundaries at runtime.

```
CAN ISR
  └─[k_msgq]──► can_rx_thread ──[safety_chan]──────────────────────► lighting_thread
                              ──[safety_chan]──────────────────────► audio_thread
                              ──[can_status_chan / can_data_chan]──► app_thread

app_thread ──[ui_cmd_chan]──────────────────────────────────────► lvgl_thread
           ──[lighting_cmd_chan]──────────────────────────────────► lighting_thread
           ──[audio_cmd_chan]────────────────────────────────────► audio_thread
           ──[can_tx_cmd_chan]──────────────────────────────────► can_tx_work

k_timer (10ms) ──► can_tx_work
k_timer (20ms) ──► lvgl_thread (lv_timer_handler tick)
               ──► lighting_thread (animation tick)
```

---

## Kconfig Requirements

```kconfig
CONFIG_ZBUS=y
CONFIG_ZBUS_MSG_SUBSCRIBER=y

# Stack/thread counts
CONFIG_MAIN_STACK_SIZE=4096
CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE=2048

# Zephyr SMF
CONFIG_SMF=y
CONFIG_SMF_ANCESTOR_SUPPORT=y
```
