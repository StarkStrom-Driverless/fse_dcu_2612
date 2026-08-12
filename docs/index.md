# DCU 2612 {#mainpage}

Firmware for the **Driver Control Unit** of the StarkStrom Augsburg Formula
Student car — the display and control unit built into the steering wheel, from
vehicle generation 2612 onwards. It shows the driver the relevant vehicle data,
takes input from four buttons and two rotary encoders, and talks to the main
control unit (mABX) over CAN.

## Platform

Everything in this reference is written against the same two dependencies, so
they are stated once here rather than in each file:

| | |
|---|---|
| Target RTOS | [Zephyr RTOS](https://zephyrproject.org) |
| UI library | [LVGL](https://lvgl.io) |
| Controller | STM32F405RG, 168 MHz, 1 MB flash, 128 KB RAM |

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
exchange goes through a Zbus channel.

| Group | Contents |
|---|---|
| @ref dcu_app | Dirigent coordinator and the global application state |
| @ref dcu_modules | CAN, UI, lighting, audio — one peripheral and one thread each |
| @ref dcu_services | Event bus and persistent settings |

Follow a value through the system:

- @ref dcu_can decodes received frames into a `can_data_snapshot` and publishes
  it on `can_data_chan`.
- @ref dcu_app stores the snapshot and forwards it to the UI on `ui_cmd_chan`.
- @ref dcu_ui pushes it into the generated LVGL subjects; bound widgets on the
  active screen update themselves.

Transmission runs the other way and without commands: the CAN module reads the
selected mission and the operating mode straight out of the application state
on each of its cycles.

### Generated code

The CAN layer is not written by hand. Signals are declared once in
`dbc/dcu_app.yaml` against the vehicle DBC, and `tools/codegen/gen_can.py`
emits the pack/unpack functions, the RX dispatch with its hardware filters, the
snapshot struct, the LVGL subjects and the settings schema into `src/generated/`.

Those files are not checked in. If the generator has not been run, they are
missing from this reference — see @ref manual-developer for how to produce them.

### Where to look

Group pages list the public interface of a subsystem. The **Files** section
carries the rest: each module's implementation notes live in the file
documentation of its `.c` file, including the private helpers, the thread
functions and the reasoning behind them.
