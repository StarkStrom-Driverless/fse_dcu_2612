/**
 * @file        screen_boot.h
 * @brief       Boot (splash) screen factory
 *
 * @details     Provides a single factory function that builds and returns the
 *              boot screen lv_obj_t.  The screen is purely static — it contains
 *              no interactive widgets and requires no periodic updates.
 *
 *              Navigation context
 *              ──────────────────
 *              In the screen carousel managed by ui.c, the boot screen is the
 *              starting position.  Rotating the left encoder counter-clockwise
 *              transitions to the Mission Selection screen.
 *
 *              Screen layout (480 × 320)
 *              ─────────────────────────
 *
 *                ┌──────────────────────────────────────┐
 *                │                                      │
 *                │                                      │
 *                │                                      │
 *                │               DCU                    │ ← centered label
 *                │                                      │   BoldItalic_100
 *                │                                      │
 *                │                                      │
 *                └──────────────────────────────────────┘
 *
 *              Lifecycle
 *              ─────────
 *              Created once in ui_module_init(), kept alive for the entire
 *              firmware session.  ui.c loads or unloads it via
 *              lv_screen_load_anim(); the screen is never explicitly deleted.
 *
 * @author      Mario Wegmann <mario.wegmann@web.de>
 * @date        Created: 2026-06-02
 *
 * @version     0.1.0
 *
 * @copyright   Copyright (c) 2026 Mario Wegmann
 *              SPDX-License-Identifier: Apache-2.0
 *
 * @note        Target RTOS : Zephyr RTOS (https://zephyrproject.org)
 *              UI Library  : LVGL (https://lvgl.io)
 *
 * ─────────────────────────────────────────────────────────────────────────────────────────────────
 * Revision History
 * ─────────────────────────────────────────────────────────────────────────────────────────────────
 * Version  Date        Author          Description
 * 0.1.0    2026-06-02  Mario Wegmann   Initial creation
 * ─────────────────────────────────────────────────────────────────────────────────────────────────
 */

#pragma once

/* ── LVGL Include ────────────────────────────────────────────────────────────────────────────── */

#include <lvgl.h>


/* ── Public Function Declarations ────────────────────────────────────────────────────────────── */

/**
 * @brief Create the boot screen.
 *
 * Allocates a new top-level LVGL screen object, applies the shared screen
 * background style, and adds the centred product-name label.
 *
 * Must be called after ui_styles_init() so that the shared style objects
 * are already initialised.
 *
 * The caller (ui.c) is responsible for loading the returned screen via
 * lv_screen_load() or lv_screen_load_anim().  Ownership is retained by the
 * LVGL object tree; the pointer must not be freed explicitly.
 *
 * @return  Pointer to the created screen object.  Never NULL.
 */
lv_obj_t *screen_boot_create(void);
