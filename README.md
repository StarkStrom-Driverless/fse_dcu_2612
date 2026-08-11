# DCU 2612

This Repository contains the Software for the Driver Control Unit (DCU) Version 2612. This Software uses Zephyr RTOS and LVGL UI Library. 

## Features

## Getting Started

### Prepare
First it is needed to load the West Python venv. The platform specific instructions can be found in the Zephyr Documentation.

Clone with submodules — the Doxygen theme (`docs/doxygen-awesome-css`) is one:

``` sh
git clone --recurse-submodules https://github.com/StarkStrom-Driverless/fse_dcu_2612
# already cloned without the flag:
git submodule update --init --recursive
```


### Build and Flash to Prozessorboard
``` sh
west build -p always -o=-j4 -b fse_pb --shield fse_dcu_2612
west flash --runner openocd
```

### Build and Run on QEMU Cortex A53 Emulator
It is also possible to run the firmware on the local computer by using the QEMU Cortex A53 Target. 
``` sh
west build -p always -b qemu_cortex_a53
west build -t run
```

### Fast Recompile
It is possible to only compile changed files if the build directory is clean by ommiting the `-p always` parameter. 

## CAN Signal generation

Switch to the zephyr folder and run: 
`python fse_dcu_2612/tools/codegen/gen_can.py`

## Documentation
Powered by Doxygen.

The manual under `docs/manual/` covers operation (drivers, engineers) and
development (toolchain, build, extending). Architecture details live in the
other documents under `docs/`.

### Build Docs locally

`docker run --volume ./docs:/etc/doxygen/docs ./src:/etc/doxygen/src -i --name dcu_doxygen ubuntu:latest`

Inside the container

``` bash
apt-get update
# Enough for only HTML generation
apt-get install doxygen graphviz 
# Additional for PDF generation
apt-get install texlive-latex-base texlive-latex-recommended texlive-latex-extra texlive-fonts-recommended texlive-font-utils texlive-lang-german ghostscript make

# Build website
doxygen Doxyfile.website

# Build Manual PDF
doxygen Doxyfile.manual
make -C _build_manual/latex
```

The website output can be find under `docs/_build`. 
The PDF Manual can be find under `docs/_build_manual/refman.pdf`. 


## Resources

- [📚 Wiki](https://confluence.starkstrom-augsburg.de/spaces/SW/pages/362447144/DCU+2612)
- [📖 Documentation]()
- [⚙️ PCB Design](https://emea-starkstrom-augsburg-ev-hochschule-augsburg-univer.365.altium.com/designs/B701FB28-C5AD-4F84-A34B-F90F3A0C0913)

## Contributors

- Mario Wegmann <mario.wegmann@web.de>

## License