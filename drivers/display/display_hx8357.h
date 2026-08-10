/*
 * Copyright (c) 2025
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZEPHYR_DRIVERS_DISPLAY_DISPLAY_HX8357_H_
#define ZEPHYR_DRIVERS_DISPLAY_DISPLAY_HX8357_H_

/* -------------------------------------------------------------------------
 * Standard MIPI DCS commands (work without SETEXTC unlock)
 * ------------------------------------------------------------------------- */
#define HX8357_SWRESET   0x01  /* Software reset */
#define HX8357_SLPOUT    0x11  /* Sleep out */
#define HX8357_NORON     0x13  /* Normal display mode on */
#define HX8357_INVOFF    0x20  /* Display inversion off */
#define HX8357_INVON     0x21  /* Display inversion on */
#define HX8357_DISPOFF   0x28  /* Display off */
#define HX8357_DISPON    0x29  /* Display on */
#define HX8357_CASET     0x2A  /* Column address set */
#define HX8357_PASET     0x2B  /* Page (row) address set */
#define HX8357_RAMWR     0x2C  /* Memory write */
#define HX8357_MADCTL    0x36  /* Memory data access control */
#define HX8357_COLMOD    0x3A  /* Interface pixel format */

/* -------------------------------------------------------------------------
 * HX8357-D extended commands (require SETEXTC unlock first)
 * ------------------------------------------------------------------------- */
/** Enable extension command set – key: {0xFF, 0x83, 0x57} */
#define HX8357_SETEXTC   0xB9
/** Internal oscillator / frame-rate control */
#define HX8357_SETOSC    0xB0
/** Power control */
#define HX8357_SETPWR1   0xB1
/** RGB / SPI interface control */
#define HX8357_SETRGB    0xB3
/** Display timing / cycle control */
#define HX8357_SETCYC    0xB4
/** VCOM voltage control */
#define HX8357_SETCOM    0xB6
/** Panel characteristics */
#define HX8357_SETPANEL  0xCC
/** Source timing / balance control */
#define HX8357_SETSTBA   0xC0
/** Gamma correction */
#define HX8357_SETGAMMA  0xE0

/* -------------------------------------------------------------------------
 * MADCTL register bits
 * ------------------------------------------------------------------------- */
#define HX8357_MADCTL_MY   BIT(7)  /* Row address order */
#define HX8357_MADCTL_MX   BIT(6)  /* Column address order */
#define HX8357_MADCTL_MV   BIT(5)  /* Row/column exchange */
#define HX8357_MADCTL_ML   BIT(4)  /* Vertical refresh order */
#define HX8357_MADCTL_BGR  BIT(3)  /* BGR filter panel order */

/* -------------------------------------------------------------------------
 * COLMOD pixel format values
 * ------------------------------------------------------------------------- */
#define HX8357_COLMOD_RGB565  0x55  /* 16-bit per pixel */
#define HX8357_COLMOD_RGB888  0x77  /* 24-bit per pixel */

/* -------------------------------------------------------------------------
 * Timing constants (HX8357-D datasheet requirements)
 * ------------------------------------------------------------------------- */
/** Hardware reset pulse width (ms) */
#define HX8357_RESET_PULSE_MS     10U
/** Wait after HW reset before first command (ms) – datasheet: 120 ms min */
#define HX8357_RESET_WAIT_MS     120U
/** Wait after SWRESET (ms) */
#define HX8357_SWRESET_WAIT_MS    10U
/** Wait after SETEXTC before next extended command (ms) – datasheet: 300 ms */
#define HX8357_SETEXTC_WAIT_MS   300U
/** Wait after SLPOUT before DISPON (ms) – datasheet: 150 ms min */
#define HX8357_SLPOUT_WAIT_MS    150U
/** Wait after DISPON (ms) */
#define HX8357_DISPON_WAIT_MS     50U

/* Default resolution */
#define HX8357_X_RES 320U
#define HX8357_Y_RES 480U

#endif /* ZEPHYR_DRIVERS_DISPLAY_DISPLAY_HX8357_H_ */
