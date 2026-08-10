# Developer Manual {#manual-developer}

This part is aimed at developers who build, extend or port the DCU firmware.

## Overview

### Hardware

| | |
|---|---|
| Controller | STM32F405RG, 168 MHz, 1 MB flash, 128 KB RAM (+64 KB DTCM/CCM) |
| Board | `fse_pb` (processor board) |
| Shield | `fse_dcu_2612` (display board) |
| Display | 3.5" colour display via SPI2 |
| LED strip | APA102 via SPI3 |
| CAN | CAN3, 1 Mbit/s, TJA1048 transceiver |
| Inputs | 2 GPIO quadrature encoders, 4 GPIO buttons |
| Piezo | GPIO |

The PCB design lives in the Altium project (link under
@ref manual-developer-links).

### Software architecture

The firmware follows the **Dirigent pattern**: an App Layer coordinates all
modules, which never communicate with each other directly. All exchange goes
through Zbus channels.

\dot
digraph dirigent {
    bgcolor  = "transparent";
    rankdir  = LR;
    nodesep  = 0.25;
    node [shape = box, style = rounded, fontname = "Helvetica", fontsize = 10, height = 0.35];
    edge [fontname = "Helvetica", fontsize = 9, color = "#666666"];

    subgraph cluster_up {
        label     = "publish events";
        fontname  = "Helvetica";
        fontsize  = 9;
        fontcolor = "#666666";
        style     = dashed;
        color     = "#aaaaaa";
        up_can [label = "CAN"];
        up_ui  [label = "UI"];
        up_set [label = "Settings"];
    }

    app [label = "App Layer\n(Dirigent)", style = "rounded,filled", fillcolor = "#eeeeee"];

    subgraph cluster_down {
        label     = "receive commands";
        fontname  = "Helvetica";
        fontsize  = 9;
        fontcolor = "#666666";
        style     = dashed;
        color     = "#aaaaaa";
        dn_ui    [label = "UI"];
        dn_can   [label = "CAN"];
        dn_light [label = "Lighting"];
        dn_audio [label = "Audio"];
    }

    up_can -> app;
    up_ui  -> app;
    up_set -> app;

    app -> dn_ui;
    app -> dn_can;
    app -> dn_light;
    app -> dn_audio;
}
\enddot

The App Layer is the sole writer of `app_state` and hosts the operating-mode
state machine. CAN and UI appear on both sides: they publish events *and*
receive commands.

| Directory | Content |
|---|---|
| `src/app/` | App Layer, `app_state` |
| `src/modules/can/` | CAN driver, TX/RX, bus status |
| `src/modules/ui/` | LVGL, screens, widgets, fonts |
| `src/modules/lighting/` | APA102 control, zones and effects |
| `src/modules/audio/` | Piezo control |
| `src/services/event_bus/` | Zbus channels and payload types |
| `src/services/settings/` | Persistent settings (NVS) |
| `src/generated/` | Generated code — **do not edit by hand** |

<!-- Further reading:

| Document | Content |
|---|---|
| `docs/architecture.md` | Overall architecture, design decisions |
| `docs/modules.md` | Module specifications |
| `docs/event_system.md` | Zbus channels and subscriber model |
| `docs/thread_model.md` | Threads, priorities, stack sizes |
| `docs/ui_data_flow.md` | Data flow CAN → UI |
| `docs/settings_module.md` | Persistence, flash layout, schema generation | -->

### Threads

| Thread | Priority | Task |
|---|---|---|
| CAN | 3 | Decode RX, send TX, bus status |
| App | 5 | Process events, issue commands |
| UI (LVGL) | 8 | `lv_timer_handler()`, input, rendering |

All LVGL calls happen exclusively in the UI thread. Values needed by other
threads live in `app_state` or in the settings service — both are mutex
protected.

## Hardware assembly

1. Populate the components per the BOM
2. Connect the processor board and the display board
3. Fit the assembly into the steering wheel
4. Connect CAN and the power supply

## Setting up the working environment

### Installing Zephyr

Follow the [Zephyr installation guide](https://docs.zephyrproject.org/latest/develop/getting_started/index.html)
for your platform. In addition you need:

- **OpenOCD** — for flashing via the `openocd` runner
- **cantools** and **PyYAML** — for the CAN code generator

```sh
# Inside the activated West venv
pip install cantools pyyaml
```

### Cloning the repository

The repository is placed as an application directory inside the Zephyr
workspace:

```sh
cd zephyrproject/zephyr
git clone --recurse-submodules https://github.com/StarkStrom-Driverless/fse_dcu_2612 fse_dcu_2612
```

`--recurse-submodules` is required: the Doxygen theme
(`docs/doxygen-awesome-css`) is a submodule. Without it the directory stays
empty and the generated website falls back to the default Doxygen styling.

If the repository was cloned without the flag, fetch the submodule afterwards:

```sh
git submodule update --init --recursive
```

### Providing the DBC and the YAML

The code generator needs two inputs:

| File | Role |
|---|---|
| `dbc/dcu_can.dbc` | Bit layout of all CAN messages — single source of truth |
| `dbc/dcu_app.yaml` | Application intent: which signals, named how, with which limits |

The DBC comes from the [vehicle repository](https://github.com/StarkStrom-Driverless/datenbasen_2511/blob/main/CAN3_UASA2310_Vehicle_mitnode.dbc).
The YAML belongs to this repository and is checked in — it holds project
knowledge that cannot be derived from the DBC.

## Building and flashing

### Generating the CAN code

Before the first build and after every change to the DBC or the YAML:

```sh
python3 tools/codegen/gen_can.py
```

The generator validates the YAML against the DBC and aborts with a specific
error message if something does not match. It produces the following under
`src/generated/`:

| File | Content |
|---|---|
| `dcu_can_gen.{c,h}` | Pack/unpack functions (via cantools) |
| `can_data_gen.h` | `struct can_data_snapshot` |
| `can_rx_gen.{c,h}` | RX dispatch and filter list |
| `can_tx_gen.h` | TX periods |
| `ui_subjects_gen.{c,h}` | LVGL subjects for RX signals |
| `ui_tx_subjects_gen.{c,h}` | LVGL subjects for TX signals |
| `settings_schema_gen.{c,h}` | Schema of the persistent settings |

### Building the firmware

```sh
west build -p always -o=-j4 -b fse_pb --shield fse_dcu_2612
```

The shield lives in this repository under `boards/shields/`. Zephyr only
searches `BOARD_ROOT` plus `ZEPHYR_BASE`, so `CMakeLists.txt` appends the
application directory to `BOARD_ROOT` before `find_package(Zephyr)` — no
command-line flag is needed.

Omitting `-p always` recompiles only changed files, provided the build
directory is consistent.

### Flashing

```sh
west flash --runner openocd
```

### Running in the emulator

For UI work without hardware:

```sh
west build -p always -b qemu_cortex_a53
west build -t run
```

<!-- ### Resource budget

Flash is tight — keep an eye on it while building:

```
FLASH:  759184 B / 768 KB   (96.5 %)
RAM:    123252 B / 128 KB   (94.0 %)
DTCM:      64 KB / 64 KB    (100 %)
```

The application size is capped at 768 KB via `zephyr,code-partition`; the
remaining 256 KB belong to the settings partition. Exceeding it produces the
linker error `region 'FLASH' overflowed` — this is intentional and prevents the
firmware from growing into the settings area.

The largest individual contributors are the fonts (particularly
`BarlowCondensed_BoldItalic_100` and `_80`) and the top-down graphics. If space
gets tight, that is where the leverage is. -->

## Adding a new CAN signal

1. **Add the signal to `dbc/dcu_app.yaml`** — the message must exist in the
   DBC; the YAML only references it:

   ```yaml
     mABX_2_DCU_1:
       direction: rx
       timeout_ms: 500
       signals:
         Neues_Signal:
           app_name: neues_signal
           ui: { label: "Neues Signal", precision: 1 }
           limits: { critical_low: 5, warning_low: 15, warning_high: 60, critical_high: 75 }
           range: { min: 0, max: 100 }
   ```

   | Key | Effect |
   |---|---|
   | `app_name` | Field name in the snapshot and name of the LVGL subject |
   | `ui.label` | Caption for the UI |
   | `ui.precision` | Number of decimal places |
   | `limits` | Generates threshold defines for warning and critical bounds |
   | `range` | Generates `UI_<NAME>_RANGE_MIN` / `_MAX`, e.g. for `lv_bar_set_range()` |
   | `persist` | TX only: makes the signal persistent (see below) |

2. **Run the generator** — `python3 tools/codegen/gen_can.py`

3. **Adapt the UI** — bind the `ui_subj_neues_signal` subject to a widget:

   ```c
   lv_bar_bind_value(bar_new_signal, &ui_subj_new_signal);
   ```

4. **Build and flash**

A new snapshot field, an RX filter and the decoding are created automatically —
nothing has to change in `can.c`.

### Persistent TX signal

If a TX signal is to survive a restart, a `persist` block is all it takes:

```yaml
      Torque_Vectoring_SETTING:
        app_name: torquevect_setting
        persist: { default: 0 }
```

The value range follows from the bit width in the DBC. The generator creates
the entry in `settings_schema_gen.h`; the settings service handles loading,
saving and range checking without further action.

Details in `docs/settings_module.md`.

## Adding a new feature

### Creating a new screen

1. Create `src/modules/ui/screens/screen_<name>.{c,h}` following the pattern of
   an existing screen
2. Extend `enum screen_id` in `src/services/event_bus/events.h`
3. Register the factory in `k_screen_factories[]` in `ui.c` and, if needed, add
   the screen to `k_carousel[]`
4. Create the header via `ui_header_create(scr, "TITLE", status_subjects)`

Screens are created lazily and released when left. State that has to outlive
that does not belong in the screen, but in a module-wide subject, in
`app_state` or in the settings service.

### Creating a new module

1. Create a directory under `src/modules/<name>/`
2. Add the Zbus channel and payload to `events.h` and `event_bus.{c,h}`
3. Provide `<name>_module_init()` and call it from `main.c` at the right point
   in the initialisation order
4. Describe the module in `docs/modules.md`

`CMakeLists.txt` does not need touching — `FILE(GLOB_RECURSE app_sources
src/*.c)` picks up new files automatically.

**Rules that uphold the Dirigent pattern:**

- Only the App Layer writes to `app_state`
- Modules never communicate with each other directly
- Raw CAN frames never leave the CAN module
- LVGL calls exclusively in the UI thread

### Contributing code

- Branch off `main`
- Follow the existing file-header style and comment density
- Update the affected documents under `docs/` as well
- Build before pushing

## Building the documentation

The documentation is generated with Doxygen from two configurations that share
`Doxyfile.common`:

| Configuration | Output | Input |
|---|---|---|
| `Doxyfile.website` | HTML | `index.md`, `manual/`, `../src` |
| `Doxyfile.pdfmanual` | LaTeX → PDF | `manual/` only — no API reference |

### Theme

The HTML output uses [doxygen-awesome-css](https://github.com/jothepro/doxygen-awesome-css),
pulled in as the git submodule `docs/doxygen-awesome-css` and applied via
`HTML_EXTRA_STYLESHEET` in `Doxyfile.common`. `GENERATE_TREEVIEW = YES` is set
because the theme is designed for the sidebar layout.

The theme affects **HTML only**. The PDF manual is typeset by LaTeX and is
unaffected by it, which is why the two outputs look different by design.

To update the theme, bump the submodule:

```sh
git submodule update --remote docs/doxygen-awesome-css
```


### Building in a container

Without a local installation, use a container:

```sh
docker run --volume ./fse_dcu_2612/docs:/etc/doxygen/docs --volume ./fse_dcu_2612/src:/etc/doxygen/src \
           -it --name dcu_doxygen ubuntu:latest
```

Inside the container:

```sh
apt-get update
# HTML only
apt-get install doxygen graphviz
# Additionally for PDF
apt-get install texlive-latex-base texlive-latex-recommended texlive-latex-extra \
                texlive-fonts-recommended texlive-font-utils texlive-lang-german \
                ghostscript make

cd /etc/doxygen/docs

doxygen Doxyfile.website          # Website
doxygen Doxyfile.pdfmanual        # Manual
make -C _build_manual/latex
```

Results: website under `docs/_build`, PDF under
`docs/_build_manual/refman.pdf`.

## Versioning {#manual-developer-versioning}

The public version is **`2612.<minor>.<patch>`**. The leading number identifies
the vehicle generation — year 26, twelfth car built — and is what appears on
the boot screen, in the git tag and on the generated documentation.

Internally that number is split across two files, because it does not fit where
the toolchain stores it.

### Why the number is split

Zephyr validates every field of the `VERSION` file against a range of 0 to 255.
This applies to the application version, not only to Zephyr's own: both are
processed by the same loop in `cmake/modules/version.cmake` (the loop appends
`APP` alongside `KERNEL` and then checks `${type}_VERSION_MAJOR`). The MCUboot
image header has the same limit — `iv_major` is a `uint8_t`.

`VERSION_MAJOR = 2612` would therefore abort the build at CMake configure time
with:

```
VERSION_MAJOR must be present and in the range of 0-255 (Current: 2612)
```

So the vehicle index and the year are stored as separate fields and the public
number is derived from them. All of them live in `VERSION`:

| Value | Field in `VERSION` | Example |
|---|---|---|
| Vehicle index | `VERSION_MAJOR` | `12` |
| Year | `DCU_VEHICLE_YEAR` | `26` |
| Minor / patch | `VERSION_MINOR`, `PATCHLEVEL` | `1` / `0` |
| Public number | derived: `year * 100 + index` | `2612` |

`DCU_VEHICLE_YEAR` is not a Zephyr key. Zephyr's `version.cmake` extracts only
the keys it knows by regular expression and ignores every other line, so the
field can live in the same file without interfering — which keeps all version
numbers in one place. `CMakeLists.txt` reads it and computes `DCU_VEHICLE_ID`.

```
VERSION file   12.1.0        Zephyr, MCUboot image header
Boot screen    v2612.1.0     via app_version.h + DCU_VEHICLE_ID
Git tag        v2612.1.0
```

The index appears exactly once, so the two representations cannot drift. The
boot screen reads `APP_VERSION_MINOR` and `APP_PATCHLEVEL` from the generated
`app_version.h` rather than a hard-coded string, so what the display shows is
always what was built.

### What the fields mean

| Field | Bump when |
|---|---|
| `PATCHLEVEL` | Bug fixes, no behavioural change for the driver |
| `VERSION_MINOR` | New features — a new screen, a new signal, a new setting |
| `VERSION_MAJOR` | Only with a new vehicle; `DCU_VEHICLE_YEAR` changes with it |

Note that the major field does not carry the usual semantic-versioning meaning
of "breaking change". The firmware has no downstream consumers that could
break; its real compatibility contract is the DBC and the hardware, and both
are tied to the vehicle generation. Encoding the vehicle there is therefore the
more honest reading — but it does mean a breaking change within a season is
expressed as a minor bump.

### Cutting a release

1. Bump `VERSION_MINOR` or `PATCHLEVEL` in `VERSION`
2. Commit the change
3. Tag and push:

```sh
git tag v2612.1.1
git push origin v2612.1.1
```

The release workflow verifies that the tag matches the derived version before
it builds anything, and aborts with the expected value if they disagree. That
check runs first, so a typo fails within seconds instead of after the LaTeX
installation.

A tag with a suffix (`v2612.2.0-rc1`) is published as a pre-release; the suffix
is ignored when comparing against `VERSION`.

### Moving to the next vehicle

In `VERSION`, set `VERSION_MAJOR` to the new index and `DCU_VEHICLE_YEAR` to
the new year, and reset `VERSION_MINOR` and `PATCHLEVEL` to zero. No other file
needs touching. Whether that happens in this repository or a new one is a
separate decision — the repository, the board and the shield are all named
after the vehicle as well.

## Continuous integration

Two workflows live under `.github/workflows/`:

| Workflow | Trigger | Does |
|---|---|---|
| `docs-check.yml` | Pull request, push to `main` | Builds website and PDF, publishes nothing |
| `release.yml` | Push of a `v*` tag | Builds, publishes Pages, creates the release |

Both run the same Doxygen and LaTeX steps — keep them in sync when changing
one.

### Documentation check

`docs-check.yml` runs whenever a pull request touches `docs/` or `src/`. It
builds both outputs and uploads them as artifacts, so a reviewer can download
the PDF and look at the rendered result instead of reading raw Markdown.

Its purpose is to surface a broken documentation build on the pull request
rather than when a release tag is pushed. In particular it fails on Unicode
characters that LaTeX cannot typeset, which are easy to introduce and produce
no warning locally until the PDF build runs.

## Releasing

Releases are produced by `.github/workflows/release.yml`, triggered by pushing
a version tag:

```sh
git tag v1.2.0
git push origin v1.2.0
```

The tag is the single source of truth for the version. There is no automatic
increment — you decide the number when you tag. A tag containing a hyphen
(`v1.2.0-rc1`) is published as a pre-release.

### What the workflow does

1. Checks out the repository **including submodules**, so the Doxygen theme is
   present
2. Derives `PROJECT_VERSION` from the tag, which Doxygen picks up as
   `PROJECT_NUMBER` — the version on the generated pages matches the release
3. Builds the HTML website and the PDF manual
4. Aborts if LaTeX rejected any Unicode characters — box-drawing glyphs
   (U+2500 to U+257F) and geometric shapes (U+25A0 to U+25FF) have no glyph in
   the default LaTeX fonts and would otherwise produce a broken PDF
5. Publishes the HTML to GitHub Pages
6. Creates the GitHub release with the PDF attached, named
   `dcu-manual-v1.2.0.pdf`

Source archives (zip and tar.gz) are attached by GitHub automatically. They do
**not** contain submodules — the theme is a build-time dependency of the
documentation only, so nothing needed to build the firmware is missing.

### One-time setup

GitHub Pages must be switched to workflow-based publishing once, otherwise the
deploy step fails with *Pages site not found*:

> Settings → Pages → Build and deployment → Source: **GitHub Actions**

### Notes

Installing the LaTeX packages takes a few minutes on every run. That is
acceptable for a workflow that only runs on a tag; if it becomes a nuisance,
the step is the obvious candidate for caching or a prebuilt container image.

The workflow does not build the firmware — it produces documentation and the
release only. Adding a firmware build would require the Zephyr SDK and a West
workspace in CI.

## Resources {#manual-developer-links}

- [Wiki](https://confluence.starkstrom-augsburg.de/spaces/SW/pages/362447144/DCU+2612)
- [PCB Design](https://emea-starkstrom-augsburg-ev-hochschule-augsburg-univer.365.altium.com/designs/B701FB28-C5AD-4F84-A34B-F90F3A0C0913)
- [Zephyr Project](https://zephyrproject.org)
- [LVGL](https://lvgl.io)
