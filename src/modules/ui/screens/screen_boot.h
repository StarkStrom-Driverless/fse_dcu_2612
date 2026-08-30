/**
 * @file        screen_boot.h
 * @brief       Boot (splash) screen factory
 *
 * @ingroup     dcu_ui_screens
 *
 * @details     Provides a single factory function that builds and returns the
 *              boot screen lv_obj_t.  The screen has no interactive widgets;
 *              the only moving part is the rotating outer gear, animated by
 *              LVGL itself.
 *
 *              It carries no vehicle data either, unless
 *              CONFIG_DCU_BENCHMARK_BOOT_PATCH is enabled — that adds a
 *              CAN-driven measurement patch for latency benchmarking, which is
 *              scaffolding rather than a feature. See screen_boot.c.
 *
 *              ### Navigation context
 *              The boot screen is the carousel's starting position.  Turning
 *              the left encoder counter-clockwise walks into the debug
 *              screens, clockwise towards mission selection and driving.  It
 *              offers no input groups, so the right encoder and both button
 *              pads are detached while it is shown.
 *
 *              ### Screen layout (480 × 320)
 *
 *              ```
 *              ┌──────────────────────────────────────┐
 *              │ START                         ▪▪▪▪▪▪ │ ← shared header
 *              ├──────────────────────────────────────┤
 *              │                                      │
 *              │                ⚙                     │ ← gear logo,
 *              │                                      │   outer ring turning
 *              │               DCU                    │ ← BoldItalic_80
 *              │  ┌────┐     v2612.x.y                │ ← from app_version.h
 *              │  │████│                              │ ← benchmark patch,
 *              └──┴────┴──────────────────────────────┘   only when enabled
 *              ```
 *
 *              ### Lifecycle
 *              Like every screen: built on entry, deleted on leaving.  Nothing
 *              here needs to survive that, so the factory holds no state.
 *
 * @author      Mario Wegmann <mario.wegmann@web.de>
 * @date        Created: 2026-06-02
 *
 * @version     0.1.0
 *
 * @copyright   Copyright (c) 2026 Mario Wegmann.
 *              SPDX-License-Identifier: Apache-2.0
 */

/*
 * ─────────────────────────────────────────────────────────────────────────────────────────────────
 * Revision History
 * Version  Date        Author          Description
 * 0.1.0    2026-06-02  Mario Wegmann   Initial creation
 * ─────────────────────────────────────────────────────────────────────────────────────────────────
 */

#ifndef MODULES_UI_SCREENS_SCREEN_BOOT_H
#define MODULES_UI_SCREENS_SCREEN_BOOT_H

/* ── LVGL Include ────────────────────────────────────────────────────────────────────────────── */

#include <lvgl.h>

/**
 * @defgroup dcu_ui_screens UI screens
 * @ingroup  dcu_ui
 * @brief Screen factories, one per @ref screen_id.
 *
 * Every screen exposes the same shape: a `screen_<name>_create()` factory that
 * ui.c calls on first visit, plus — for screens with interactive widgets —
 * three group accessors that ui.c binds to the right encoder and the two
 * button pads.
 *
 * Screens are deleted on leaving, so a factory must rebuild everything each
 * time. State that has to survive a visit belongs in a file-scope LVGL
 * subject, in app_state or in the settings service.
 * @{
 */


/* ── Public Function Declarations ────────────────────────────────────────────────────────────── */

/**
 * @brief Create the boot screen.
 *
 * Allocates a new top-level LVGL screen object, applies the shared screen
 * background style, and adds the header, the gear logo with its rotation
 * animation, and the product and version labels.
 *
 * Must be called after ui_styles_init() so that the shared style objects
 * are already initialised.
 *
 * The caller (ui.c) loads the returned screen and later deletes it.
 * Ownership is with the LVGL object tree; the pointer must not be freed.
 *
 * @param status_subjects  Device-status subjects for the header widget.
 * @return                 Pointer to the created screen object. Never NULL.
 */
lv_obj_t *screen_boot_create(lv_subject_t *status_subjects);

/** @} */ /* dcu_ui_screens */

#endif /* MODULES_UI_SCREENS_SCREEN_BOOT_H */
