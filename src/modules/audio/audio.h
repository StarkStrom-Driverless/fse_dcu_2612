/**
 * @file        audio.h
 * @brief       Public interface for the Audio module
 *
 * @ingroup     dcu_audio
 *
 * @details     The Audio module owns the piezo buzzer on the DCU board and is
 *              the only place allowed to drive it.
 *
 *              Downward channel consumed (App → Audio):
 *                audio_cmd_chan  — play / stop commands
 *
 *              The module produces nothing: it does not report effect
 *              completion, so feedback_chan stays unused.
 *
 * @author      Mario Wegmann <mario.wegmann@web.de>
 * @date        Created: 2026-07-08
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
 * 0.1.0    2026-07-08  Mario Wegmann   Initial creation
 * ─────────────────────────────────────────────────────────────────────────────────────────────────
 */

#ifndef MODULES_AUDIO_AUDIO_H
#define MODULES_AUDIO_AUDIO_H

/**
 * @defgroup dcu_audio Audio module
 * @ingroup  dcu_modules
 * @brief Piezo buzzer control, driven by commands on audio_cmd_chan.
 * @{
 */


/* ── Public Function Declarations ────────────────────────────────────────────────────────────── */

/**
 * @brief Initialise the Audio module.
 *
 * Configures the piezo GPIO as an inactive output and starts the audio thread
 * (priority 6), which then blocks on audio_cmd_chan.
 *
 * Call from main() before ui_module_init() so audio feedback is available from
 * the first screen interaction.
 *
 * @note If the piezo GPIO is not ready or cannot be configured, an error is
 *       logged and no thread is started.  The firmware continues silently —
 *       the Zbus subscriber is registered statically, so commands published to
 *       audio_cmd_chan are then simply never consumed.
 */
void audio_module_init(void);

/** @} */ /* dcu_audio */

#endif /* MODULES_AUDIO_AUDIO_H */
