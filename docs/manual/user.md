# User Manual {#manual-user}

This part explains how to operate the DCU, for drivers and engineers.

## Operating concept {#manual-user-controls}

The DCU is mounted directly in the steering wheel. Four buttons and two rotary
encoders are available for input.

### Input elements

| Element | Name | Basic function |
|---------|------|----------------|
| Button | Left | Context-dependent, varies by screen |
| Button | Right | Confirm selection |
| Button | Ready to Drive (RTD) | Request RTD |
| Button | Timestamp (TS) | Place a time marker in the log |
| Rotary encoder | Left | Page through screens |
| Rotary encoder | Right | Select values within a screen |

The **EV DRIVING** screen has its own operating concept, designed explicitly
for use while driving — see @ref manual-user-ev.

### Output elements

- **3.5\" colour display** — main indicator
- **Piezo buzzer** — audible feedback
- **LED strip** — in the field of view above the display, divided into three
  zones:

| Zone | Indication |
|------|------------|
| Left | HV battery temperature as a bar |
| Centre | Status indicators |
| Right | HV battery state of charge (SoC) as a bar |

<!-- On a safety-critical fault, a red flashing pattern overrides all zones.
This pattern cannot be displaced by any other indication and only clears once
the cause has been resolved. -->

## Status bar {#manual-user-statusbar}

At the top of the screen, every page shows the screen title on the left and a
row of symbols on the right. Each symbol represents a vehicle component and is
coloured independently.

| Symbol | Component | Meaning |
|--------|-----------|---------|
| Camera | Data logger | Recording status |
| Robot | ROS | Autonomy stack ready |
| Monitor | DV PC | Receiving data |
| Ruler | Kistler | Measurement system responding |
| Chip | mABX | Main control unit |
| Power | SDC | Shutdown circuit closed |
| Network | CAN | Bus state |

### Colour coding

| Colour | State | Meaning |
|--------|-------|---------|
| Green | OK | Component operating normally |
| Gold | WARN | Degraded but functional |
| Red, flashing | FAULT | Component reports a fault or is not responding |
| Red, flashing | ACTIVE | Special state, see below |


### What triggers each state

- **Data logger** — Green when the logger is ready. Flashing red while
  recording is in progress (ACTIVE state).
- **ROS** — Green as soon as the autonomy stack reports readiness.
- **DV PC** — Green while valid data is being received.
- **Kistler** — Red as soon as the measurement system runs into a timeout.
- **SDC** — Green only if **all** components of the shutdown circuit are
  closed. The SDC screen shows which component is open.
- **CAN** — Green in normal operation, gold on elevated error counters, red on
  error-passive, flashing red on bus-off or a stopped controller. A gold CAN
  symbol is an early warning sign for wiring or termination problems.
- **mABX** — not yet linked to a signal.

## Navigation {#manual-user-navigation}

Once power is applied, the DCU boots and shows the **boot screen**. From there
you page through the screens with the **left rotary encoder**.

The screens are arranged as a carousel. To the left of the boot screen are the
debug screens, to the right the screens for EV and DV operation:

| Turning the encoder | Screens, in order from BOOT |
|---|---|
| Counter-clockwise | DV SETTINGS, DBG TX, DBG TS, DBG PRESSURE, DBG HV ACCU, DBG LV ACCU |
| *(start position)* | **BOOT** |
| Clockwise | DV MISSION, SDC, PRE RTD |

The carousel does not wrap around: at either end the display simply stops.

**EV DRIVING** and **DV DRIVING** are not on the carousel. EV DRIVING appears
when RTD is activated from the PRE RTD screen; DV DRIVING appears when the
autonomous system enters the *AS driving* state. Neither can be left again
except by powering the DCU off.


<!-- ## Operating modes

The DCU has four operating modes that determine which navigation is permitted:

| Mode | Meaning |
|------|---------|
| DEBUG | Full navigation, all screens reachable |
| PRE RTD | Guided preparation before driving |
| RTD | Mission active, navigation locked |
| POST RTD | Return to the idle state |

The transition to RTD is triggered by the RTD button. It locks the selected
mission, sends the RTD signal over CAN and switches automatically to the
**EV DRIVING** screen.

> **Note:** The DCU only requests RTD. Whether the vehicle actually enters the
> ready-to-drive state is decided by the mABX. -->

## Debug screens

The debug screens are intended for commissioning and fault finding. They show
raw values from the CAN bus.

### DBG TX

Sets the `Debug_SETTING` signal of the `DCU_2_mABX` message, which is sent to
the mABX.

**Operation:** Select a value between 0 and 7 with the right rotary encoder and
confirm with the right button. The value currently being transmitted is shown
below the selection roller.

The value is retained across a restart.

### DBG TS

Shows the measured values of the tractive system:

- APPS position left and right (accelerator pedal position sensors)
- BPPS position (brake pressure sensor)
- Tractive system voltage
- Inverter temperature
- Motor temperature


### DBG PRESSURE

Shows the pressure sensors:

- Air pressure front and rear
- Brake pressure front and rear

### DBG HV ACCU

Shows the values of the high-voltage battery:

- HV battery voltage
- HV battery temperature
- Air pressure front and rear

### DBG LV ACCU

Shows the values of the low-voltage battery:

- LV battery voltage
- HV battery temperature
- Air pressure front and rear

## Driving screens

### DV MISSION

Selection of the mission transmitted to the vehicle controller.

**Operation:** Select the mission in the roller with the right rotary encoder
and confirm with the right button. The currently confirmed mission is shown
below the roller — it is only transmitted after confirmation.

Available options: None, Acceleration, Skidpad, Trackdrive, Braketest,
Inspection, Autocross and Manual Driving.

<!-- After switching to RTD mode the mission is locked and cannot be changed -->
<!-- until POST RTD has completed. -->

### SDC

Shows the state of all components of the shutdown circuit.

The display consists of two parts: a top-down view of the vehicle with the
components at their physical positions, and a table next to it on the right.

In the table:

| Text colour | Meaning |
|-------------|---------|
| Dark | Component closed, in order |
| Red | Component open — interrupts the shutdown circuit |

Covered are: motor front left and right, motor rear left and right, cockpit,
BSPD, ASCU, HVD, MH, RES, BOTS and inertia.

**For fault finding:** If several components are red at the same time, the
cause is usually the first one in sequence — the shutdown circuit is a series
circuit.

<!-- ### PRE RTD

Guided checklist before driving off. The screen lists the preconditions that
must be met before RTD can be requested. -->

### EV DRIVING {#manual-user-ev}

Shown automatically as soon as RTD has been activated — hold the **middle
button** (RTD) on the PRE RTD screen.

**Paging to other screens is locked on this screen.** This is intentional and
prevents accidental input while driving. The only way back is to power-cycle
the DCU.

The controls have a fixed assignment here:

| Element | Function |
|---------|----------|
| Left rotary encoder | Torque vectoring front |
| Right rotary encoder | Torque vectoring rear |
| Left button | Power limit |
| Right button | Torque vectoring on/off |

In addition to the two torque vectoring values (TQG F, TQG R), the screen shows
the HV battery state of charge and the temperatures of the HV battery, inverter
and motor.

The configured values for power limit and torque vectoring are retained across
a restart.

### DV DRIVING

Shown automatically once the vehicle reports the autonomous system in the
**AS driving** state. For now it displays only the DV mission selected on the
DV MISSION screen.

Like EV DRIVING it is not on the carousel and cannot be left — the only way
back is to power-cycle the DCU. It stays up even after the autonomous system
moves on to *AS finished* or *AS emergency*.

## Other screens

### Boot

First screen after power-up. Shows the current DCU software version and serves
as the starting point of the carousel.

The version reads `v2612.<minor>.<patch>`, for example `v2612.1.0`. The leading
number identifies the vehicle generation, the two behind it the firmware
release.

When reporting a fault, always include the version shown here — it identifies
the exact firmware build.

### Settings

**DV SETTINGS** is the central editor for every persistent setting: debug bits,
ASR, recuperation, torque vectoring, power limit and display brightness. Each
setting is one row; the **right encoder** moves the highlight between rows, the
**left button** lowers the highlighted value and the **right button** raises it.

Changes take effect immediately — there is no separate confirm step. The same
values can still be reached from the functional screens (DBG CUSTOM,
EV DRIVING); all paths write to the same store, so the screens always agree.

## Diagnostics

Common observations and their likely cause:

| Observation | Possible cause |
|-------------|----------------|
| All measured values frozen, CAN symbol red | CAN connection interrupted |
| CAN symbol gold | Elevated error counters — check wiring and termination |
| SDC symbol red, vehicle does not move off | Open the SDC screen, identify the open component |
| Kistler symbol red | Measurement system not responding, check wiring |
| RTD button has no effect | Preconditions per PRE RTD not met; the mABX grants the release |
| Display stays dark | Check power supply; booting only takes a few seconds |

Values that still look plausible despite a red status bar are the last ones
received. The DCU does not hide stale values — when in doubt, the status bar
is authoritative.
