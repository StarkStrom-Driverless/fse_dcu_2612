/*
 * Copyright (c) 2025
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Himax HX8357-D TFT-LCD controller – standalone MIPI-DBI display driver.
 *
 * This is a self-contained driver that does NOT depend on the ILI9xxx
 * framework.  It controls the complete initialisation sequence with timing
 * values taken directly from the HX8357-D datasheet and cross-referenced
 * against the Adafruit HX8357 reference implementation.
 *
 * Key differences from the ILI9xxx framework that made a standalone driver
 * necessary:
 *   - After a hardware reset the HX8357-D requires ≥120 ms before the first
 *     command.  The ILI9xxx framework only waits 5 ms (ILI9XXX_RESET_WAIT_TIME),
 *     so COLMOD and MADCTL would be sent while the controller is still
 *     initialising → black screen.
 *   - SLPOUT requires ≥150 ms before DISPON.  The ILI9xxx framework uses
 *     120 ms (ILI9XXX_SLEEP_OUT_TIME).
 *   - SETEXTC must be followed by a 300 ms delay before any further extended
 *     command.  Sending it via the framework's regs_init_fn (after COLMOD/
 *     MADCTL) would still work, but full ordering control is cleaner here.
 *
 * Initialisation sequence (matches Adafruit HX8357D library):
 *   HW reset (≥10 ms pulse) → 120 ms wait
 *   SWRESET → 10 ms wait
 *   SETEXTC (unlock) → 300 ms wait
 *   SETRGB / SETCOM / SETOSC / SETPANEL / SETPWR1 / SETSTBA / SETCYC / SETGAMMA
 *   COLMOD  (pixel format)
 *   MADCTL  (orientation + BGR)
 *   INVON / INVOFF  (optional)
 *   SLPOUT → 150 ms wait
 *   DISPON → 50 ms wait
 */

#define DT_DRV_COMPAT himax_hx8357

#include "display_hx8357.h"

#include <zephyr/drivers/display.h>
#include <zephyr/drivers/mipi_dbi.h>
#include <zephyr/dt-bindings/display/panel.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(display_hx8357, CONFIG_LOG_DEFAULT_LEVEL);

/* -------------------------------------------------------------------------
 * Driver structs
 * ------------------------------------------------------------------------- */

struct hx8357_config {
	const struct device *mipi_dev;
	struct mipi_dbi_config dbi_config;
	/* Panel geometry */
	uint16_t x_resolution;
	uint16_t y_resolution;
	/* Display options */
	uint8_t  pixel_format;   /* PANEL_PIXEL_FORMAT_* from DT */
	uint16_t rotation;       /* 0 / 90 / 180 / 270 */
	bool     inversion;
	bool     disable_bgr_mode;
	/* HX8357-D init register values (from DT) */
	uint8_t  setextc[3];
	uint8_t  setosc[1];
	uint8_t  setpwr1[6];
	uint8_t  setrgb[4];
	uint8_t  setcyc[7];
	uint8_t  setcom[1];
	uint8_t  setpanel[1];
	uint8_t  setstba[6];
	uint8_t  setgamma[34];
};

struct hx8357_data {
	enum display_pixel_format pixel_format;
	enum display_orientation  orientation;
	uint8_t  bytes_per_pixel;
	uint8_t  madctl; /* cached for runtime set_orientation */
};

/* -------------------------------------------------------------------------
 * Low-level SPI helper
 * ------------------------------------------------------------------------- */

static int hx8357_transmit(const struct device *dev, uint8_t cmd,
			    const void *tx_data, size_t tx_len)
{
	const struct hx8357_config *config = dev->config;

	return mipi_dbi_command_write(config->mipi_dev, &config->dbi_config,
				      cmd, tx_data, tx_len);
}

/* -------------------------------------------------------------------------
 * Display API – blanking
 * ------------------------------------------------------------------------- */

static int hx8357_blanking_on(const struct device *dev)
{
	return hx8357_transmit(dev, HX8357_DISPOFF, NULL, 0);
}

static int hx8357_blanking_off(const struct device *dev)
{
	return hx8357_transmit(dev, HX8357_DISPON, NULL, 0);
}

/* -------------------------------------------------------------------------
 * Display API – write
 * ------------------------------------------------------------------------- */

static int hx8357_set_mem_area(const struct device *dev,
				uint16_t x, uint16_t y,
				uint16_t w, uint16_t h)
{
	int r;
	uint16_t buf[2];

	buf[0] = sys_cpu_to_be16(x);
	buf[1] = sys_cpu_to_be16(x + w - 1U);
	r = hx8357_transmit(dev, HX8357_CASET, buf, sizeof(buf));
	if (r < 0) {
		return r;
	}

	buf[0] = sys_cpu_to_be16(y);
	buf[1] = sys_cpu_to_be16(y + h - 1U);
	return hx8357_transmit(dev, HX8357_PASET, buf, sizeof(buf));
}

static int hx8357_write(const struct device *dev,
                         const uint16_t x, const uint16_t y,
                         const struct display_buffer_descriptor *desc,
                         const void *buf)
{
    const struct hx8357_config *config = dev->config;
    struct hx8357_data *data = dev->data;
    struct display_buffer_descriptor mipi_desc;
    int r;

    __ASSERT(desc->width <= desc->pitch, "Pitch is smaller than width");
    __ASSERT((desc->pitch * data->bytes_per_pixel * desc->height) <= desc->buf_size,
             "Input buffer too small");

    r = hx8357_set_mem_area(dev, x, y, desc->width, desc->height);
    if (r < 0) return r;

    r = hx8357_transmit(dev, HX8357_RAMWR, NULL, 0);
    if (r < 0) return r;

    mipi_desc.width            = desc->width;
    mipi_desc.height           = desc->height;
    mipi_desc.pitch            = desc->width;
    mipi_desc.buf_size         = desc->width * desc->height * data->bytes_per_pixel;
    mipi_desc.frame_incomplete = desc->frame_incomplete;

    r =  mipi_dbi_write_display(config->mipi_dev, &config->dbi_config,
                                  buf, &mipi_desc, data->pixel_format);

	return r;
}

/* -------------------------------------------------------------------------
 * Display API – pixel format
 * ------------------------------------------------------------------------- */

static int hx8357_set_pixel_format(const struct device *dev,
				    const enum display_pixel_format fmt)
{
	struct hx8357_data *data = dev->data;
	uint8_t colmod;
	uint8_t bpp;

	switch (fmt) {
	case PIXEL_FORMAT_RGB_565:
		colmod = HX8357_COLMOD_RGB565;
		bpp = 2U;
		break;
	case PIXEL_FORMAT_RGB_888:
		colmod = HX8357_COLMOD_RGB888;
		bpp = 3U;
		break;
	default:
		LOG_ERR("Unsupported pixel format %d", fmt);
		return -ENOTSUP;
	}

	int r = hx8357_transmit(dev, HX8357_COLMOD, &colmod, 1U);
	if (r < 0) {
		return r;
	}

	data->pixel_format    = fmt;
	data->bytes_per_pixel = bpp;
	return 0;
}

/* -------------------------------------------------------------------------
 * Display API – orientation
 * ------------------------------------------------------------------------- */

static int hx8357_set_orientation(const struct device *dev,
				   const enum display_orientation orientation)
{
	const struct hx8357_config *config = dev->config;
	struct hx8357_data *data = dev->data;

	/*
	 * Start from the BGR bit (set unless red-blue-swap is requested).
	 * The HX8357-D uses the same MADCTL bit layout as other TFT controllers:
	 *   MY (bit 7), MX (bit 6), MV (bit 5), ML (bit 4), BGR (bit 3).
	 *
	 * The orientation values below are derived from the HX8357-D datasheet
	 * and validated against the Adafruit reference implementation where
	 * MY|MX (= 0xC0) yields a correct upright portrait image.
	 */
	uint8_t madctl = config->disable_bgr_mode ? 0U : HX8357_MADCTL_BGR;

	switch (orientation) {
	case DISPLAY_ORIENTATION_NORMAL:
		madctl |= HX8357_MADCTL_MY | HX8357_MADCTL_MX;
		break;
	case DISPLAY_ORIENTATION_ROTATED_90:
		madctl |= HX8357_MADCTL_MV | HX8357_MADCTL_MX;
		break;
	case DISPLAY_ORIENTATION_ROTATED_180:
		/* no row/column flip bits */
		break;
	case DISPLAY_ORIENTATION_ROTATED_270:
		madctl |= HX8357_MADCTL_MV | HX8357_MADCTL_MY;
		break;
	default:
		return -ENOTSUP;
	}

	int r = hx8357_transmit(dev, HX8357_MADCTL, &madctl, 1U);
	if (r < 0) {
		return r;
	}

	data->madctl      = madctl;
	data->orientation = orientation;
	return 0;
}

/* -------------------------------------------------------------------------
 * Display API – capabilities
 * ------------------------------------------------------------------------- */

static void hx8357_get_capabilities(const struct device *dev,
				     struct display_capabilities *caps)
{
	const struct hx8357_config *config = dev->config;
	const struct hx8357_data   *data   = dev->data;

	memset(caps, 0, sizeof(*caps));

	caps->supported_pixel_formats = PIXEL_FORMAT_RGB_565 | PIXEL_FORMAT_RGB_888;
	caps->current_pixel_format    = data->pixel_format;
	caps->current_orientation     = data->orientation;

	/* Swap reported resolution for landscape orientations */
	if (data->orientation == DISPLAY_ORIENTATION_NORMAL ||
	    data->orientation == DISPLAY_ORIENTATION_ROTATED_180) {
		caps->x_resolution = config->x_resolution;
		caps->y_resolution = config->y_resolution;
	} else {
		caps->x_resolution = config->y_resolution;
		caps->y_resolution = config->x_resolution;
	}
}

/* -------------------------------------------------------------------------
 * Display API vtable
 * ------------------------------------------------------------------------- */

static DEVICE_API(display, hx8357_api) = {
	.blanking_on      = hx8357_blanking_on,
	.blanking_off     = hx8357_blanking_off,
	.write            = hx8357_write,
	.get_capabilities = hx8357_get_capabilities,
	.set_pixel_format = hx8357_set_pixel_format,
	.set_orientation  = hx8357_set_orientation,
};

/* -------------------------------------------------------------------------
 * Initialisation
 * ------------------------------------------------------------------------- */

static int hx8357_init(const struct device *dev)
{
	const struct hx8357_config *config = dev->config;
	int r;

	if (!device_is_ready(config->mipi_dev)) {
		LOG_ERR("MIPI DBI device not ready");
		return -ENODEV;
	}

	/* ------------------------------------------------------------------
	 * Step 1 – Hardware reset
	 *
	 * The HX8357-D datasheet requires the RESX pin to be held low for
	 * at least 10 µs (we use HX8357_RESET_PULSE_MS = 10 ms for margin).
	 * After RESX goes high, the controller needs ≥120 ms before it can
	 * accept any command.  The ILI9xxx framework only waits 5 ms here,
	 * which is the root cause of the blank-screen issue when using the
	 * framework directly.
	 * ------------------------------------------------------------------ */
	r = mipi_dbi_reset(config->mipi_dev, HX8357_RESET_PULSE_MS);
	if (r < 0 && r != -ENOTSUP) {
		LOG_ERR("HW reset failed (%d)", r);
		return r;
	}
	k_msleep(HX8357_RESET_WAIT_MS);

	/* ------------------------------------------------------------------
	 * Step 2 – Software reset
	 * ------------------------------------------------------------------ */
	r = hx8357_transmit(dev, HX8357_SWRESET, NULL, 0);
	if (r < 0) {
		LOG_ERR("SWRESET failed (%d)", r);
		return r;
	}
	k_msleep(HX8357_SWRESET_WAIT_MS);

	/* ------------------------------------------------------------------
	 * Step 3 – Unlock extended command set (SETEXTC, 0xB9)
	 *
	 * The three-byte key {0xFF, 0x83, 0x57} enables all manufacturer-
	 * specific registers.  After sending this command, the HX8357-D
	 * requires a 300 ms delay before any extended register may be written.
	 * ------------------------------------------------------------------ */
	LOG_HEXDUMP_DBG(config->setextc, sizeof(config->setextc), "SETEXTC");
	r = hx8357_transmit(dev, HX8357_SETEXTC, config->setextc,
			    sizeof(config->setextc));
	if (r < 0) {
		LOG_ERR("SETEXTC failed (%d)", r);
		return r;
	}
	k_msleep(HX8357_SETEXTC_WAIT_MS);

	/* ------------------------------------------------------------------
	 * Steps 4-11 – Extended register configuration
	 * ------------------------------------------------------------------ */
	LOG_HEXDUMP_DBG(config->setrgb, sizeof(config->setrgb), "SETRGB");
	r = hx8357_transmit(dev, HX8357_SETRGB, config->setrgb,
			    sizeof(config->setrgb));
	if (r < 0) {
		LOG_ERR("SETRGB failed (%d)", r);
		return r;
	}

	LOG_HEXDUMP_DBG(config->setcom, sizeof(config->setcom), "SETCOM");
	r = hx8357_transmit(dev, HX8357_SETCOM, config->setcom,
			    sizeof(config->setcom));
	if (r < 0) {
		LOG_ERR("SETCOM failed (%d)", r);
		return r;
	}

	LOG_HEXDUMP_DBG(config->setosc, sizeof(config->setosc), "SETOSC");
	r = hx8357_transmit(dev, HX8357_SETOSC, config->setosc,
			    sizeof(config->setosc));
	if (r < 0) {
		LOG_ERR("SETOSC failed (%d)", r);
		return r;
	}

	LOG_HEXDUMP_DBG(config->setpanel, sizeof(config->setpanel), "SETPANEL");
	r = hx8357_transmit(dev, HX8357_SETPANEL, config->setpanel,
			    sizeof(config->setpanel));
	if (r < 0) {
		LOG_ERR("SETPANEL failed (%d)", r);
		return r;
	}

	LOG_HEXDUMP_DBG(config->setpwr1, sizeof(config->setpwr1), "SETPWR1");
	r = hx8357_transmit(dev, HX8357_SETPWR1, config->setpwr1,
			    sizeof(config->setpwr1));
	if (r < 0) {
		LOG_ERR("SETPWR1 failed (%d)", r);
		return r;
	}

	LOG_HEXDUMP_DBG(config->setstba, sizeof(config->setstba), "SETSTBA");
	r = hx8357_transmit(dev, HX8357_SETSTBA, config->setstba,
			    sizeof(config->setstba));
	if (r < 0) {
		LOG_ERR("SETSTBA failed (%d)", r);
		return r;
	}

	LOG_HEXDUMP_DBG(config->setcyc, sizeof(config->setcyc), "SETCYC");
	r = hx8357_transmit(dev, HX8357_SETCYC, config->setcyc,
			    sizeof(config->setcyc));
	if (r < 0) {
		LOG_ERR("SETCYC failed (%d)", r);
		return r;
	}

	LOG_HEXDUMP_DBG(config->setgamma, sizeof(config->setgamma), "SETGAMMA");
	r = hx8357_transmit(dev, HX8357_SETGAMMA, config->setgamma,
			    sizeof(config->setgamma));
	if (r < 0) {
		LOG_ERR("SETGAMMA failed (%d)", r);
		return r;
	}

	/* ------------------------------------------------------------------
	 * Step 12 – Pixel format (COLMOD) and orientation (MADCTL)
	 *
	 * These are sent AFTER all extended commands, matching the reference
	 * init sequence order.
	 * ------------------------------------------------------------------ */
	enum display_pixel_format pixel_format;

	if (config->pixel_format == PANEL_PIXEL_FORMAT_RGB_565) {
		pixel_format = PIXEL_FORMAT_RGB_565;
	} else if (config->pixel_format == PANEL_PIXEL_FORMAT_RGB_888) {
		pixel_format = PIXEL_FORMAT_RGB_888;
	} else {
		LOG_ERR("Unsupported pixel format in DT (%d)", config->pixel_format);
		return -ENOTSUP;
	}

	r = hx8357_set_pixel_format(dev, pixel_format);
	if (r < 0) {
		LOG_ERR("set_pixel_format failed (%d)", r);
		return r;
	}

	enum display_orientation orientation;

	switch (config->rotation) {
	case   0: orientation = DISPLAY_ORIENTATION_NORMAL;      break;
	case  90: orientation = DISPLAY_ORIENTATION_ROTATED_90;  break;
	case 180: orientation = DISPLAY_ORIENTATION_ROTATED_180; break;
	default:  orientation = DISPLAY_ORIENTATION_ROTATED_270; break;
	}

	r = hx8357_set_orientation(dev, orientation);
	if (r < 0) {
		LOG_ERR("set_orientation failed (%d)", r);
		return r;
	}

	/* ------------------------------------------------------------------
	 * Step 13 – Display inversion (optional)
	 * ------------------------------------------------------------------ */
	if (config->inversion) {
		r = hx8357_transmit(dev, HX8357_INVON, NULL, 0);
	} else {
		r = hx8357_transmit(dev, HX8357_INVOFF, NULL, 0);
	}
	if (r < 0) {
		LOG_ERR("Inversion command failed (%d)", r);
		return r;
	}

	/* ------------------------------------------------------------------
	 * Step 14 – Exit sleep mode
	 *
	 * The HX8357-D requires ≥150 ms after SLPOUT before DISPON.
	 * The ILI9xxx framework only waits 120 ms (ILI9XXX_SLEEP_OUT_TIME),
	 * which can leave the display in a transitional state.
	 * ------------------------------------------------------------------ */
	r = hx8357_transmit(dev, HX8357_SLPOUT, NULL, 0);
	if (r < 0) {
		LOG_ERR("SLPOUT failed (%d)", r);
		return r;
	}
	k_msleep(HX8357_SLPOUT_WAIT_MS);

	/* ------------------------------------------------------------------
	 * Step 15 – Normal display mode on
	 *
	 * Exits partial mode (if active) and ensures the controller drives
	 * all lines.  Present in the manufacturer's reference init sequence.
	 * ------------------------------------------------------------------ */
	r = hx8357_transmit(dev, HX8357_NORON, NULL, 0);
	if (r < 0) {
		LOG_ERR("NORON failed (%d)", r);
		return r;
	}

	/* ------------------------------------------------------------------
	 * Step 16 – Display on
	 * ------------------------------------------------------------------ */
	r = hx8357_transmit(dev, HX8357_DISPON, NULL, 0);
	if (r < 0) {
		LOG_ERR("DISPON failed (%d)", r);
		return r;
	}
	k_msleep(HX8357_DISPON_WAIT_MS);

	LOG_DBG("HX8357-D initialised successfully (%ux%u, rot=%u)",
		config->x_resolution, config->y_resolution, config->rotation);
	return 0;
}

/* -------------------------------------------------------------------------
 * Device instantiation
 * ------------------------------------------------------------------------- */

#define HX8357_DEFINE(n)                                                       \
	static const struct hx8357_config hx8357_config_##n = {               \
		.mipi_dev = DEVICE_DT_GET(DT_INST_PARENT(n)),                 \
		.dbi_config = {                                                \
			.mode = DT_INST_STRING_UPPER_TOKEN_OR(                 \
					n, mipi_mode,                          \
					MIPI_DBI_MODE_SPI_4WIRE),              \
			.config = MIPI_DBI_SPI_CONFIG_DT_INST(                 \
					n,                                     \
					SPI_OP_MODE_MASTER | SPI_WORD_SET(8),  \
					0),                                    \
		},                                                             \
		.x_resolution    = DT_INST_PROP_OR(n, width,  HX8357_X_RES), \
		.y_resolution    = DT_INST_PROP_OR(n, height, HX8357_Y_RES), \
		.pixel_format    = DT_INST_PROP(n, pixel_format),             \
		.rotation        = DT_INST_PROP(n, rotation),                 \
		.inversion       = DT_INST_PROP(n, display_inversion),        \
		.disable_bgr_mode = DT_INST_PROP(n, red_blue_swap),           \
		.setextc  = DT_INST_PROP(n, setextc),                         \
		.setosc   = DT_INST_PROP(n, setosc),                          \
		.setpwr1  = DT_INST_PROP(n, setpwr1),                         \
		.setrgb   = DT_INST_PROP(n, setrgb),                          \
		.setcyc   = DT_INST_PROP(n, setcyc),                          \
		.setcom   = DT_INST_PROP(n, setcom),                          \
		.setpanel = DT_INST_PROP(n, setpanel),                        \
		.setstba  = DT_INST_PROP(n, setstba),                         \
		.setgamma = DT_INST_PROP(n, setgamma),                        \
	};                                                                     \
                                                                               \
	static struct hx8357_data hx8357_data_##n;                             \
                                                                               \
	DEVICE_DT_INST_DEFINE(n, hx8357_init, NULL,                            \
			      &hx8357_data_##n, &hx8357_config_##n,            \
			      POST_KERNEL, CONFIG_DISPLAY_INIT_PRIORITY,        \
			      &hx8357_api)

DT_INST_FOREACH_STATUS_OKAY(HX8357_DEFINE)
