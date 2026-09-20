# DCU 2612

Firmware for the **Driver Control Unit** of the StarkStrom Augsburg Formula
Student car 2612 — the display and control unit built into the steering wheel.

It shows the driver the relevant vehicle data, takes input from four buttons
and two rotary encoders, and talks to the main control unit (mABX) over CAN.
Built on [Zephyr RTOS](https://zephyrproject.org) and
[LVGL](https://lvgl.io), running on an STM32F405RG.

## Features

**Generated CAN layer.** Signals are declared once in `dbc/dcu_app.yaml`
against the vehicle DBC; a code generator emits the pack/unpack functions, the
RX dispatch with hardware filters, the data snapshot struct and the LVGL data
bindings. Adding a signal means editing one YAML entry and rerunning the
generator — no hand-written bit manipulation anywhere in the tree.

**Reactive UI.** Screens bind widgets to LVGL subjects that the CAN layer
feeds. Values propagate without polling or manual refresh calls. Screens are
created on first visit and released on leaving, which keeps a large screen set
affordable on 128 KB of RAM.

**Strict module separation.** An App Layer coordinates all modules following
the Dirigent pattern; modules never talk to each other directly, only through
Zbus channels. Raw CAN frames never leave the CAN module, and all LVGL calls
stay in the UI thread.

**Vehicle status at a glance.** A header bar on every screen shows the health
of seven vehicle components — data logger, ROS, DV PC, Kistler, mABX, shutdown
circuit and the CAN bus itself — derived from live CAN signals and the CAN
controller state.

**Composable hardware description.** The DCU board and its two pluggable
displays (3.5" HX8357, 2.8" ST7789V) are separate Zephyr shields, so a display
swap is a build flag rather than an edit. The HX8357 driver ships as an
out-of-tree Zephyr module in this repository.

**Persistent settings.** Values that must survive a power cycle are declared
in the same YAML as the CAN signals and generated into a schema with bounds,
defaults and a change-detecting hash. They are stored in NVS on the board's
SPI NOR flash.

**Documentation pipeline.** Doxygen builds a website and a PDF manual; GitHub
Actions checks both on every documentation pull request and publishes them on
a release tag.

## Hardware

The firmware is written for one specific car and expects that hardware to be
present. It is not a general-purpose steering-wheel display: every device below
is resolved from the devicetree at compile time, so a missing one fails the
build rather than the boot.

| Part | What it is | Devicetree |
|---|---|---|
| Processor board | `fse_pb` — STM32F405RG, 168 MHz, 192 KB RAM, 1 MB flash, CAN transceiver | [fse_pb_bootloader](https://github.com/StarkStrom-Driverless/fse_pb_bootloader) |
| DCU board | Steering-wheel PCB: 2 rotary encoders, 4 buttons, piezo, display backlight | `boards/shields/fse_dcu_2612/` |
| Display | 3.5" HX8357 (480x320) or 2.8" ST7789V, over SPI2 | `boards/shields/fse_display_*/` |
| LED strip | APA102, 28 LEDs, over SPI3 | `fse_dcu_2612.overlay`, node `apa102` |
| Settings storage | AT25DF081A SPI NOR flash (8 Mbit), over SPI1 | `fse_dcu_2612.overlay`, node `nor_flash` |
| Vehicle bus | CAN at 1 Mbit/s carrying the messages `dbc/dcu_app.yaml` selects | board DTS, `zephyr,canbus` |
| Debug probe | ST-Link over SWD — flashing, and the shell over RTT | `west flash --runner openocd` |

### Running it on your own hardware

Everything board-specific sits in the devicetree, so adapting it is mostly a
matter of overlays rather than C. In rough order of effort:

1. **A different display.** Cheapest case: the two panels are separate shields,
   so swapping them is a build flag. A third panel needs its own shield
   directory — copy one of the existing two.
2. **A different carrier board.** Point the shields at your pins: the encoders,
   buttons, piezo, backlight timer, LED strip and the flash are all plain
   devicetree nodes in `boards/shields/fse_dcu_2612/fse_dcu_2612.overlay`. The
   aliases the application resolves (`qdec-input-left`, `keypad-rtd`, `piezo`,
   `led-strip`, …) are listed at the top of that file and in
   `boards/qemu_cortex_a53.overlay`.
3. **A different MCU.** The application code is portable Zephyr, but `prj.conf`
   and the shield's `Kconfig.defconfig` carry STM32-specific choices (SPI DMA
   streams, the LVGL draw-buffer sizing for 192 KB of RAM). Expect to revisit
   those.
4. **No external flash.** Set `CONFIG_DCU_SETTINGS_PERSIST=n`; the settings
   service keeps its full API and starts from the schema defaults every boot,
   which is exactly what the emulator build does.
5. **No LED strip.** Not a matter of configuration: `lighting.c` resolves the
   `led-strip` alias with `DEVICE_DT_GET` at file scope, and `CMakeLists.txt`
   globs every `.c` under `src/`, so the alias has to exist for the build to
   link at all. Either keep a stand-in node — the QEMU overlay bit-bangs a
   TLC59731 on an emulated GPIO for this reason — or drop `src/modules/lighting/`
   and its `lighting_module_init()` call in `src/main.c`.
6. **A different vehicle.** The CAN layer is generated from `dbc/dcu_can.dbc`
   plus `dbc/dcu_app.yaml`; signal names, limits and the screens that show them
   follow from there. Screens themselves are hand-written and assume this car's
   signal set.

### Without any hardware

The UI can be built and run in QEMU, which needs no board at all:

```sh
west build -p always -b qemu_cortex_a53 fse_dcu_2612
west build -t run
```

The emulator shows the real screens and runs the CAN thread against a loopback
driver. The inputs exist as emulated GPIOs that nothing drives, so the UI can be
looked at but not operated — see `boards/qemu_cortex_a53.overlay`.

## Getting Started

Full instructions — host dependencies, toolchain, DBC handling — are in the
[developer manual](docs/manual/developer.md). The short version:

```sh
mkdir fse-workspace && cd fse-workspace
python3.12 -m venv .venv && source .venv/bin/activate

pip install west
west init -m https://github.com/StarkStrom-Driverless/fse_dcu_2612 .
west update && west zephyr-export
pip install -r zephyr/scripts/requirements.txt cantools pyyaml
west sdk install

# The Doxygen theme is a git submodule; west does not fetch it.
git -C fse_dcu_2612 submodule update --init --recursive
```

This repository is the west manifest repository, so `west init` brings the
whole workspace: Zephyr pinned to a release tag, the `fse_pb` board definition
and this application.

### Add the vehicle DBC

The DBC is **not** part of this repository — it belongs to the vehicle
database and is maintained there. Copy it in as `dbc/dcu_can.dbc`:

```sh
# from https://github.com/StarkStrom-Driverless/datenbasen_2511
cp CAN3_UASA2310_Vehicle_mitnode.dbc fse_dcu_2612/dbc/dcu_can.dbc
```

Its counterpart `dbc/dcu_app.yaml` *is* in this repository: it selects which
messages and signals the firmware uses and how they are named, which is
project knowledge that cannot be derived from the DBC.

### Generate the CAN layer

**Required before the first build** — `src/generated/` is not checked in:

```sh
cd fse_dcu_2612
python3 tools/codegen/gen_can.py
```

Rerun it after every change to the DBC or the YAML. The generator validates
one against the other and aborts with a specific message when they disagree.

### Build and flash

Two shields — the DCU board plus one of the pluggable displays:

```sh
west build -p always -o=-j4 -b fse_pb --shield "fse_dcu_2612;fse_display_3_5" fse_dcu_2612
west flash --runner openocd
```

Use `fse_display_2_8` for the 2.8" panel. Omitting `-p always` recompiles only
what changed.

## Repository layout

| Path | Contents |
|---|---|
| `src/app/` | App Layer (Dirigent), application state |
| `src/modules/` | CAN, UI, lighting, audio |
| `src/services/` | Zbus event bus, settings |
| `src/generated/` | Generated code — do not edit by hand |
| `boards/shields/` | DCU board and display shields |
| `drivers/` | Out-of-tree Zephyr module: HX8357 display driver |
| `dbc/` | Vehicle DBC and the application intent YAML |
| `tools/codegen/` | CAN code generator |
| `docs/` | Manual and architecture documents |

## Documentation

Everything is built with Doxygen from three sources, so there is no second
place that can fall out of date:

| Source | Contents |
|---|---|
| [`docs/index.md`](docs/index.md) | Landing page: architecture in brief, where to start reading |
| [`docs/manual/`](docs/manual/) | User manual (operation) and developer manual (toolchain, build, extending) |
| `src/**` | The modules themselves — every header carries its module's design rationale |

Doxygen produces a website and a PDF manual. Building them locally is described
in the [developer manual](docs/manual/developer.md); the release workflow
publishes both to GitHub Pages.

## Versioning

The public version is `2612.<minor>.<patch>`, where 2612 identifies the vehicle
generation. It appears on the boot screen and as the git tag. The split between
`VERSION` and the displayed number is explained in the
[developer manual](docs/manual/developer.md).

## Resources

- [Wiki](https://confluence.starkstrom-augsburg.de/spaces/SW/pages/362447144/DCU+2612)
- [PCB design](https://emea-starkstrom-augsburg-ev-hochschule-augsburg-univer.365.altium.com/designs/B701FB28-C5AD-4F84-A34B-F90F3A0C0913)
- [fse_pb board and bootloader](https://github.com/StarkStrom-Driverless/fse_pb_bootloader)

## Contributors

- Mario Wegmann <mario.wegmann@web.de>

## AI assistance

Large parts of this repository were written with the help of an AI coding
assistant, [Claude](https://www.anthropic.com/claude) by Anthropic, used
through Claude Code. That covers firmware source, documentation and the helper scripts under `tools/`.

The requirements and the architecture come from the author, who reviewed the
results and built, flashed and tested the firmware on the target hardware. 


## License

Apache License 2.0 — see [LICENSE](LICENSE).
