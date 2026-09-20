/**
 * @file
 * @brief       Public interface for the demo module (emulator only)
 *
 * @ingroup     dcu_demo
 *
 * @details     Lets the emulator run unattended while screenshots are taken:
 *              it invents the vehicle's CAN data and walks through every screen
 *              on a timer. The module exists only in a build with
 *              CONFIG_DCU_DEMO_MODE, which Kconfig allows on a QEMU target and
 *              nowhere else — see the reasoning there and in demo.c.
 *
 *              Channels used:
 *                can_data_chan — published: the invented snapshot
 *                ui_nav_chan   — published: the next screen of the tour
 *                ui_input_chan — published once: the mission for DV DRIVING
 *
 * @author      Mario Wegmann <mario.wegmann@web.de>
 * @date        Created: 2026-09-20
 *
 * @copyright   Copyright (c) 2026 Mario Wegmann.
 *              SPDX-License-Identifier: Apache-2.0
 */

#ifndef MODULES_DEMO_DEMO_H
#define MODULES_DEMO_DEMO_H

/**
 * @defgroup dcu_demo Demo module
 * @ingroup  dcu_modules
 * @brief Fake CAN data and an automatic screen tour for the emulator.
 * @{
 */

/**
 * @brief Start the demo thread.
 *
 * Call it last, after ui_module_init(): the tour starts from the boot screen
 * the UI has just loaded, and the first snapshot is published while the UI is
 * already listening.
 *
 * Only exists when CONFIG_DCU_DEMO_MODE is set.
 */
void demo_module_init(void);

/** @} */ /* dcu_demo */

#endif /* MODULES_DEMO_DEMO_H */
