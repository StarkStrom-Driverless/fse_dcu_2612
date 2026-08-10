# Manual {#manual-intro}

This manual describes the operation, use and further development of the Driver
Control Unit (DCU) from version v2612 onwards.

## What the DCU is

The DCU is the central display and control unit in the vehicle's steering
wheel. It presents the relevant vehicle data to the driver, accepts input via
buttons and rotary encoders, and communicates over CAN with the main control
unit (mABX).

The firmware is based on **Zephyr RTOS** and the **LVGL** UI library. The
controller is an STM32F405RG mounted on processor board V2.1.

## Who this manual is for

The manual is split into two parts addressing different audiences:

| Part | Audience | Content |
|------|----------|---------|
| @ref manual-user | Drivers, engineers | Operation, screens, operating modes, status indicators |
| @ref manual-developer | Developers | Toolchain, build, CAN signals, architecture |

**Drivers** mainly need the sections on the operating concept, navigation and
the driving screens.

**Engineers** additionally need the debug screens, the meaning of the status
bar and the diagnostic notes.

**Developers** start with the developer manual; the architecture documents
under `docs/` supplement it with the details of the individual modules.

## Structure

- @subpage manual-user
- @subpage manual-developer
