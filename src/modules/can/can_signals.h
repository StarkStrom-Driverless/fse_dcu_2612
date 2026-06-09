/**
 * @file        can_signals.h
 * @brief       Compile-time CAN signal definitions for frame DCU_2_mABX (0x196)
 *
 * @details     Derived from the vehicle DBC. Provides byte indices, masks, and
 *              shift constants for every signal in the DCU_2_mABX frame, a
 *              pack macro that writes the full frame payload, and a helper that
 *              converts a mission_id to the DV_Drive_Mode_SETTING encoding.
 *
 *              Signal layout (Intel / little-endian byte order, @1+):
 *
 *              Byte  Bit 7        Bit 6        Bit 5        Bit 4        Bit 3-0
 *              ────  ───────────  ───────────  ───────────  ───────────  ───────
 *               0    ←──── Debug_SETTING (3 bits) ────►    (reserved)   ...
 *               1    RTD_Button   ← DV_Drive_Mode_SETTING (3 bits) →    ...
 *               2    (reserved)   (reserved)   (reserved)   (reserved)   ...
 *
 *              Debug_SETTING         :  5|3@1+   start-bit=5,  len=3, Intel LE
 *                                                byte[0], bits [7:5], mask=0xE0
 *              DV_Drive_Mode_SETTING : 12|3@1+   start-bit=12, len=3, Intel LE
 *                                                byte[1], bits [6:4], mask=0x70
 *              RTD_Button            : 15|1@1+   start-bit=15, len=1, Intel LE
 *                                                byte[1], bit  [7],   mask=0x80
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

/* ── Standard Includes ───────────────────────────────────────────────────────────────────────── */

#include <stdint.h>
#include <stdbool.h>

/* ── Project Includes ────────────────────────────────────────────────────────────────────────── */

#include "services/event_bus/events.h"


/* ── Frame Descriptor ────────────────────────────────────────────────────────────────────────── */

/** @brief CAN frame identifier for the DCU_2_mABX message (standard 11-bit ID). */
#define DCU2_MABX_CAN_ID    0x196U

/** @brief Data-length code: three payload bytes. */
#define DCU2_MABX_DLC       3U


/* ── Signal Constants: Debug_SETTING ────────────────────────────────────────────────────────── */

/*
 * DBC definition:  Debug_SETTING : 5|3@1+  (1,0) [0|7] "" Hauptsteuereinheit
 *
 * Intel LE → LSB at bit 5  (byte 0, bit-offset 5)
 *           → 3 bits → occupies byte[0] bits [7:5]
 */

/** @brief Payload byte index of Debug_SETTING. */
#define SIG_DEBUG_BYTE          0U

/** @brief Bitmask for Debug_SETTING within its byte (bits [7:5]). */
#define SIG_DEBUG_MASK          0xE0U

/** @brief Left-shift applied to the raw value before masking to place it in bits [7:5]. */
#define SIG_DEBUG_SHIFT         5U


/* ── Signal Constants: DV_Drive_Mode_SETTING ─────────────────────────────────────────────────── */

/*
 * DBC definition:  DV_Drive_Mode_SETTING : 12|3@1+  (0,6) [0|7] "" mABX
 *
 * Intel LE → LSB at bit 12  (byte 1, bit-offset 4)
 *           → 3 bits → occupies byte[1] bits [6:4]
 */

/** @brief Payload byte index of DV_Drive_Mode_SETTING. */
#define SIG_DRIVE_MODE_BYTE     1U

/** @brief Bitmask for DV_Drive_Mode_SETTING within its byte (bits [6:4]). */
#define SIG_DRIVE_MODE_MASK     0x70U

/** @brief Right-shift applied after masking to isolate the raw signal value. */
#define SIG_DRIVE_MODE_SHIFT    4U


/* ── Signal Constants: RTD_Button ───────────────────────────────────────────────────────────── */

/*
 * DBC definition:  RTD_Button : 15|1@1+  (0,1) [0|1] "" mABX
 *
 * Intel LE → LSB at bit 15  (byte 1, bit-offset 7)
 *           → 1 bit  → occupies byte[1] bit [7]
 */

/** @brief Payload byte index of RTD_Button. */
#define SIG_RTD_BUTTON_BYTE     1U

/** @brief Bitmask for RTD_Button within its byte (bit [7]). */
#define SIG_RTD_BUTTON_MASK     0x80U

/** @brief Right-shift applied after masking to isolate the raw signal value. */
#define SIG_RTD_BUTTON_SHIFT    7U


/* ── Frame Pack Macro ────────────────────────────────────────────────────────────────────────── */

/**
 * @brief Pack a full DCU_2_mABX payload into @p buf.
 *
 * Writes exactly DCU2_MABX_DLC bytes. All bits not belonging to a signal
 * are zeroed.
 *
 * @param buf         Pointer to a uint8_t array of length DCU2_MABX_DLC.
 * @param drive_mode  DV_Drive_Mode_SETTING raw value (0–7, 3-bit unsigned).
 *                    Use mission_to_drive_mode() to convert from enum mission_id.
 * @param rtd         RTD_Button: true = 1 (request Ready-to-Drive), false = 0.
 * @param debug       Debug_SETTING raw value (0–7, 3-bit unsigned).
 *
 * Example:
 * @code
 *   uint8_t payload[DCU2_MABX_DLC];
 *   DCU2_MABX_PACK(payload, mission_to_drive_mode(MISSION_AUTOCROSS), false, 0U);
 * @endcode
 */
#define DCU2_MABX_PACK(buf, drive_mode, rtd, debug)                                 \
    do {                                                                             \
        (buf)[0] = (uint8_t)(((uint8_t)(debug) << SIG_DEBUG_SHIFT)                  \
                             & SIG_DEBUG_MASK);                                      \
        (buf)[1] = (uint8_t)(((uint8_t)(drive_mode) << SIG_DRIVE_MODE_SHIFT)        \
                             & SIG_DRIVE_MODE_MASK)                                  \
                 | (uint8_t)((rtd) ? SIG_RTD_BUTTON_MASK : 0U);                     \
        (buf)[2] = 0U;                                                               \
    } while (0)


/* ── Mission → Drive Mode Mapping ───────────────────────────────────────────────────────────── */

/**
 * @brief Convert a mission_id to the DV_Drive_Mode_SETTING raw value.
 *
 * The numeric values of enum mission_id are defined to match the DBC
 * encoding directly:
 *
 *   MISSION_NONE           → 0  (no discipline selected)
 *   MISSION_ACCELERATION   → 1
 *   MISSION_SKIDPAD        → 2
 *   MISSION_AUTOCROSS      → 3
 *   MISSION_ENDURANCE      → 4
 *   MISSION_INSPECTION     → 5
 *   MISSION_MANUAL_DRIVING → 6
 *
 * @param mission  Mission selected by the driver.
 * @return         3-bit raw signal value (0–6) for DV_Drive_Mode_SETTING.
 */
static inline uint8_t mission_to_drive_mode(enum mission_id mission)
{
    return (uint8_t)mission;
}
