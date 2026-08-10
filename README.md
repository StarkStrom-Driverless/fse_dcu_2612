# DCU 2612

This Repository contains the Software for the Driver Control Unit (DCU) Version 2612. This Software uses Zephyr RTOS and LVGL UI Library. 

## Features

## Getting Started

### Prepare
First it is needed to load the West Python venv. The platform specific instructions can be found in the Zephyr Documentation. 


### Build and Flash to Prozessorboard
``` sh
west build -o=-j4 -p always -b fse_pb --shield fse_display_3_5 ./FSE_DCU_2612/samples/inputs_and_outputs
west flash --runner openocd
```

### Build and Run on QEMU Cortex A53 Emulator
It is also possible to run the firmware on the local computer by using the QEMU Cortex A53 Target. 
``` sh
west build -o=-j4 -p always -b qemu_cortex_a53 ./FSE_DCU_2612/samples/inputs_and_outputs
west build -t run
```

### Fast Recompile
It is possible to only compile changed files if the build directory is clean by ommiting the `-p always` parameter. 

## Documentation
Powered by Doxygen. 

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