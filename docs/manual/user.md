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
| Button | Ready to Drive (RTD) | Request RTD — hold, EV CHECKLIST screen only |
| Button | Reserve | Sets `DCU_RESERVE_BUTTON` on the bus while held — any screen |
| Rotary encoder | Left | Page through screens |
| Rotary encoder | Right | Select values within a screen |

The **EV DRIVING** screen has its own operating concept, designed explicitly
for use while driving — see @ref manual-user-ev.

The **reserve button** is the one control that does not depend on the screen.
It has no effect on the display at all: while it is held, the DCU sends
`DCU_RESERVE_BUTTON = 1` to the mABX, and from the first message after the
release it sends 0 again. Nothing is latched, and paging to another screen
while holding it changes nothing. What the mABX does with the bit is decided
there, not here.

### Output elements

- **3.5\" color display** — main indicator
- **Piezo buzzer** — audible feedback
- **LED strip** — in the field of view above the display, divided into three
  zones:

| Zone | Indication |
|------|------------|
| Left | HV battery voltage as a bar |
| Center | Status indicators |
| Right | Whichever of accumulator, inverter or motor temperature is closest to its own limit |

Both bars grow from the outside towards the middle, and their empty part stays
faintly lit so the length of a bar can be read at a glance. Green, gold and red
carry the same meaning as on the display.

Outside the EV DRIVING screen the strip shows a rotating gear instead.

<!-- On a safety-critical fault, a red flashing pattern overrides all zones.
This pattern cannot be displaced by any other indication and only clears once
the cause has been resolved. -->

## Status bar {#manual-user-statusbar}

At the top of the screen, every page shows the screen title on the left and a
row of symbols on the right. Each symbol represents a vehicle component and is
colored independently.

| Symbol | Component | Meaning |
|--------|-----------|---------|
| Camera | Data logger | Recording status |
| Robot | ROS | Autonomy stack ready |
| Monitor | DV PC | Receiving data |
| Ruler | Kistler | Measurement system responding |
| Chip | mABX | Main control unit |
| Power | SDC | Shutdown circuit closed |
| Network | CAN | Bus state |

### Color coding

| Color | State | Meaning |
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

Once power is applied, the DCU boots and shows the **START** screen. From there
you page through the screens with the **left rotary encoder**.

The screens are arranged as a carousel. To the left of the START screen are the
debug screens, to the right the screens for EV and DV operation:

| Turning the encoder | Screens, in order from START |
|---|---|
| Counter-clockwise | SETTINGS, DBG CUSTOM, DBG TS, DBG PRESSURE, DBG HV ACCU, DBG LV ACCU |
| *(start position)* | **START** |
| Clockwise | DV MISSION, SDC, EV CHECKLIST |

The carousel does not wrap around: at either end the display simply stops.

**EV DRIVING** and **DV DRIVING** are not on the carousel. EV DRIVING appears
when the mABX reports that the vehicle is ready to drive, after RTD was
requested on the EV CHECKLIST screen (see @ref manual-user-pre-rtd); DV DRIVING
appears when the autonomous system enters the *AS driving* state. Neither can
be paged away from. Each closes by itself when its state ends: EV DRIVING once
the vehicle leaves the ready-to-drive state, DV DRIVING once the autonomous
system leaves *AS driving*. The display then returns to the screen shown before.


<!-- ## Operating modes

The DCU has four operating modes that determine which navigation is permitted:

| Mode | Meaning |
|------|---------|
| DEBUG | Full navigation, all screens reachable |
| PRE RTD | Guided preparation before driving |
| RTD | Mission active, navigation locked |
| POST RTD | Return to the idle state |

The transition to RTD follows the vehicle: the DCU switches to the
**EV DRIVING** screen once the mABX reports the ready-to-drive state.

> **Note:** The DCU only requests RTD. Whether the vehicle actually enters the
> ready-to-drive state is decided by the mABX. -->

## Debug screens

The debug screens are intended for commissioning and fault finding. They show
raw values from the CAN bus.

### DBG CUSTOM

Sets the `Debug_SETTING` signal of the `DCU_2_mABX` message, which is sent to
the mABX.

**Operation:** Select a value between 0 and 7 with the right rotary encoder and
confirm with the right button. The value currently being transmitted is shown
below the selection roller.

The value is retained across a restart, and the roller starts on the stored
value. This is the only screen the debug bits can be set from.

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

| Text color | Meaning |
|-------------|---------|
| Dark | Component closed, in order |
| Red | Component open — interrupts the shutdown circuit |

Covered are: motor front left and right, motor rear left and right, cockpit,
BSPD, ASCU, HVD, MH, RES, BOTS and inertia.

**For fault finding:** If several components are red at the same time, the
cause is usually the first one in sequence — the shutdown circuit is a series
circuit.

### EV CHECKLIST {#manual-user-pre-rtd}

The last screen before driving off. It shows the values worth a look before the
car is started — brake and air pressure, and the state of both batteries — and
it is the only screen from which ready-to-drive (RTD) can be requested. The RTD
button does nothing on any other screen — page to EV CHECKLIST first.

**Readouts:** six bars, each with its value to the right, in two columns:

| | Left column | Right column |
|---|---|---|
| Top | Brake pressure front | Brake pressure rear |
| Middle | Air pressure front | Air pressure rear |
| Bottom | HV battery voltage | LV battery voltage |

A value turns gold once it passes its warning limit and red past its critical
one — for the pressures when they get too high, for the voltages when they get
too low. The same values, and more, are on DBG PRESSURE, DBG HV ACCU and
DBG LV ACCU.

The screen only *shows* the values. It does not check them or hold the RTD
button back; whether the vehicle enters ready-to-drive is decided by the mABX.

**Operation:** Press the brake pedal, then press and **hold** the RTD button
until the button on the display turns green.

| Button color | Meaning |
|--------------|---------|
| White | Not pressed |
| Gold | Held, request not yet sent |
| Green | Request is being sent to the mABX |

The request is sent only while the button is held, and only after it has been
held for 0.5 s — a short tap sends nothing. Letting go ends the request with
the next CAN message, a fraction of a second later. The DCU never keeps it on
by itself.

The DCU only *requests* RTD. Whether the vehicle enters the ready-to-drive
state is decided by the mABX; the brake must be applied at the same time. Once
the vehicle reports ready-to-drive, the RTD sound plays and the display
switches to **EV DRIVING** on its own — the button can then be released. When
the vehicle leaves the ready-to-drive state, the display comes back here.

### EV DRIVING {#manual-user-ev}

Shown automatically as soon as the mABX reports that the vehicle is ready to
drive — request it by holding the RTD button on the EV CHECKLIST screen, see
@ref manual-user-pre-rtd.

**Paging to other screens is locked on this screen.** This is intentional and
prevents accidental input while driving.

The screen closes on its own as soon as the vehicle leaves the ready-to-drive
state — when the shutdown circuit opens, for instance. The display returns to
the screen shown before, usually EV CHECKLIST, from where RTD can be requested
again.

The controls have a fixed assignment here:

| Element | Function |
|---------|----------|
| Left rotary encoder | Torque vectoring front |
| Right rotary encoder | Torque vectoring rear |
| Left button | Power limit on/off |
| Right button | Torque vectoring on/off |

Both buttons work the same way: they show their name and ON or OFF underneath
and turn green while on. Both only switch between on and off — if a level was
chosen for one of them on SETTINGS, pressing the button here replaces it
with plain on or off.

In addition to the two torque vectoring values (TQG F, TQG R), the screen shows
the HV battery state of charge and the temperatures of the HV battery, inverter
and motor.

The configured values for power limit and torque vectoring are retained across
a restart.

### DV DRIVING

Shown automatically once the vehicle reports the autonomous system in the
**AS driving** state. For now it displays only the DV mission selected on the
DV MISSION screen.

Like EV DRIVING it is not on the carousel and cannot be paged away from. It
closes on its own as soon as the autonomous system leaves *AS driving* — for
*AS finished* and *AS emergency* just as for any other state — and the display
returns to the screen shown before.

## Other screens

### START

First screen after power-up. Shows the current DCU software version and serves
as the starting point of the carousel. The gear logo stands still on purpose:
a turning gear made users think the DCU was still starting up and not yet
ready. The code for the rotation is in the firmware but disabled.

The version reads `v2612.<minor>.<patch>`, for example `v2612.1.0`. The leading
number identifies the vehicle generation, the two behind it the firmware
release.

When reporting a fault, always include the version shown here — it identifies
the exact firmware build.

### Settings

**SETTINGS** is the central editor for the persistent settings: ASR,
recuperation, torque vectoring, power limit and display brightness. Each setting
is one row with a bar showing where the value sits between its limits. The
**right encoder** moves the highlight between rows — the highlighted bar turns
gold — the **left button** lowers the highlighted value and the **right button**
raises it.

Changes take effect immediately — there is no separate confirm step. The same
values can still be reached from EV DRIVING; both paths write to the same store,
so the screens always agree.

The debug bits are **not** on this screen. They are set on DBG CUSTOM, which also
documents what each bit means.

Writing to the flash is deferred by two seconds so that turning a value up and
down again does not wear the memory out. Once the write has gone through,
**Settings saved** appears briefly at the bottom of the screen — only then does
the value survive a power cycle.

## Diagnostics

Common observations and their likely cause:

| Observation | Possible cause |
|-------------|----------------|
| All measured values frozen, CAN symbol red | CAN connection interrupted |
| CAN symbol gold | Elevated error counters — check wiring and termination |
| SDC symbol red, vehicle does not move off | Open the SDC screen, identify the open component |
| Kistler symbol red | Measurement system not responding, check wiring |
| RTD button has no effect, display button stays white | Not on the EV CHECKLIST screen — the button only works there |
| Display button stays gold while held | Held for less than 0.5 s, or the request cannot be sent — check the CAN symbol |
| Display button green, but no RTD | The mABX refuses: brake not pressed, tractive system not active or a precondition not met |
| Display stays dark | Check power supply; booting only takes a few seconds |

Values that still look plausible despite a red status bar are the last ones
received. The DCU does not hide stale values — when in doubt, the status bar
is authoritative.
