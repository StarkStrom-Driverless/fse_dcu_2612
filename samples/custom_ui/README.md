# Custom UI Sample

This sample project creates a custom UI design optimized for readability in fast and bright situations. 

## Features

## Getting Started

### Prepare
First it is needed to load the West Python venv. The platform specific instructions can be found in the Zephyr Documentation. 


### Build and Flash to Prozessorboard
``` sh
west build -o=-j4 -p always -b fse_pb --shield fse_display_3_5 ./FSE_DCU_2612/samples/custom_ui
west flash --runner openocd
```

### Build and Run on QEMU Cortex A53 Emulator
It is also possible to run the firmware on the local computer by using the QEMU Cortex A53 Target. 
``` sh
west build -o=-j4 -p always -b qemu_cortex_a53 ./FSE_DCU_2612/samples/custom_ui
west build -t run
```

### Fast Recompile
It is possible to only compile changed files if the build directory is clean by ommiting the `-p always` parameter. 


### Circuit

