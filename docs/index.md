# DCU 2612 {#mainpage}

Firmware for the **Driver Control Unit** of the StarkStrom Augsburg Formula
Student car — the display and control unit built into the steering wheel, from
vehicle generation 2612 onwards. It shows the driver the relevant vehicle data,
takes input from four buttons and two rotary encoders, and talks to the
**Vehicle Control Unit (VCU, "mABX")** over CAN. The VCU is the car's central
controller: it decides, among other things, whether the vehicle may enter
Ready-to-Drive; the DCU only requests it and displays the outcome.

## Platform

Everything in this reference is written against the same two dependencies, so
they are stated once here rather than in each file:

| | |
|---|---|
| Target RTOS | [Zephyr RTOS](https://zephyrproject.org) 4.4 |
| UI library | [LVGL](https://lvgl.io) 9.5 |
| Controller | STM32F405RG, 168 MHz, 1 MB flash, 128 KB RAM (+64 KB CCM) |
| Display | 3.5" HX8357 panel, 480 × 320, landscape |

All threads, channels and devicetree references below are Zephyr concepts; all
screens, widgets, subjects and styles are LVGL ones.

## Manual

Start here if you operate the car or are setting up a development environment.

- @subpage manual-intro
  - @ref manual-user — operation for drivers and engineers
  - @ref manual-developer — toolchain, build, extending

## API reference

The firmware follows the **Dirigent pattern**: an App Layer coordinates all
modules, and modules never communicate with each other directly — every
exchange goes through a Zbus channel. The App Layer is the only writer of the
application state, and it runs the operating-mode state machines that decide
which screen the driver is on when the vehicle changes state.

| Group | Contents |
|---|---|
| @ref dcu_app | Dirigent coordinator, the global application state and the state machines |
| @ref dcu_modules | CAN, UI, lighting, audio — one peripheral and one thread each |
| @ref dcu_services | Event bus and persistent settings |

The UI module has its own sub-groups:

| Group | Contents |
|---|---|
| @ref dcu_ui | LVGL thread, screen lifecycle, input routing and the screen carousel |
| @ref dcu_ui_screens | One factory per screen |
| @ref dcu_ui_widgets | Header, hint bar and the value widget shared by the screens |
| @ref dcu_ui_layout | The screen frame and how a screen divides what is left — no widget has a pixel coordinate |
| @ref dcu_ui_styles | Colors, fonts and the shared styles |

Follow a value through the system:

- @ref dcu_can decodes received frames into a `can_data_snapshot` and publishes
  it on `can_data_chan`.
- @ref dcu_app stores the snapshot and forwards it to the UI on `ui_cmd_chan`.
- @ref dcu_ui pushes it into the generated LVGL subjects; bound widgets on the
  active screen update themselves.

Transmission runs the other way and without commands: on each of its cycles
the CAN module reads the selected mission, the Ready-to-Drive request and the
reserve button out of the application state and the persistent settings out of
the settings service, and packs them into the `DCU_2_mABX` frame. Nothing is
stored twice, so the frame on the bus cannot lag behind the application.

### Generated code

The CAN layer is not written by hand. Signals are declared once in
`dbc/dcu_app.yaml` against the vehicle DBC, and `tools/codegen/gen_can.py`
emits the pack/unpack functions, the RX dispatch with its hardware filters, the
snapshot struct, the LVGL subjects, the settings schema and one **signal
descriptor** per displayed value into `src/generated/`.

The descriptor is what keeps the screens free of hard-coded numbers: caption,
unit, decimals, range and warning/critical limits of a value come from the
`ui:`, `range:` and `limits:` entries of the YAML, and the widgets read them.
Changing a limit is a change to the YAML, not to a screen.

Those files are not checked in, and neither is the vehicle DBC. If the
generator has not been run, they are missing from this reference — see
@ref manual-developer for how to produce them.

### Where to look

Group pages list the public interface of a subsystem. The **Files** section
carries the rest: each module's implementation notes live in the file
documentation of its `.c` file, including the private helpers, the thread
functions and the reasoning behind them.

### Without the car

The firmware runs in the QEMU emulator without any hardware: the real screens,
the CAN thread against a loopback driver, and an optional demo mode that tours
through every screen on a fixed set of fake CAN values (@ref dcu_demo). The
emulator shows the UI but cannot be operated; its build and the demo mode are
described in @ref manual-developer.
