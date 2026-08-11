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
defaults and a change-detecting hash.
*Storage is currently disabled* — see [`docs/settings_module.md`](docs/settings_module.md).

**Documentation pipeline.** Doxygen builds a website and a PDF manual; GitHub
Actions checks both on every documentation pull request and publishes them on
a release tag.

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

| Document | Contents |
|---|---|
| [Manual](docs/manual/) | Operation for drivers and engineers, development guide |
| [architecture.md](docs/architecture.md) | Overall architecture and design decisions |
| [modules.md](docs/modules.md) | Module specifications |
| [event_system.md](docs/event_system.md) | Zbus channels and subscriber model |
| [thread_model.md](docs/thread_model.md) | Threads, priorities, stack sizes |
| [ui_data_flow.md](docs/ui_data_flow.md) | Data flow from CAN signal to widget |
| [settings_module.md](docs/settings_module.md) | Persistence, flash layout, schema generation |

Building the documentation locally is described in the
[developer manual](docs/manual/developer.md); the release workflow publishes it
to GitHub Pages.

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

## License

Apache License 2.0 — see [LICENSE](LICENSE).
