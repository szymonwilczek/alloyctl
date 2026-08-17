// SPDX-License-Identifier: GPL-2.0-only
/*
 * SteelSeries Rival 600, USB IDs 1038:1724, 1038:172E.
 *
 * Protocol notes live in Documentation/protocol/steelseries-rival600.rst.
 *
 * Configuration uses HID reports on USB interface 0.
 * Commands send fire-and-forget.
 */
#include <stdio.h>
#include <string.h>

#include "default_art.h"
#include "driver.h"

#define R600_CMD_SENSITIVITY 0x03
#define R600_CMD_POLLING 0x04
#define R600_CMD_COLOR 0x05
#define R600_CMD_SAVE 0x09
#define R600_CMD_BUTTONS 0x31

#define R600_DPI_MIN 100
#define R600_DPI_MAX 12000
#define R600_DPI_STEP 100

static uint8_t r600_dpi_to_wire(uint16_t dpi)
{
	uint16_t clamped;

	clamped = ALLOY_CLAMP(dpi, R600_DPI_MIN, R600_DPI_MAX);
	clamped = (uint16_t)(((clamped - R600_DPI_MIN + (R600_DPI_STEP / 2)) /
			      R600_DPI_STEP) *
				     R600_DPI_STEP +
			     R600_DPI_MIN);
	clamped = ALLOY_CLAMP(clamped, R600_DPI_MIN, R600_DPI_MAX);
	return (uint8_t)((clamped - R600_DPI_MIN) / R600_DPI_STEP);
}

static uint8_t r600_action_first_byte(const struct alloy_action *act)
{
	switch (act->type) {
	case ALLOY_ACT_MOUSE:
		return (uint8_t)act->value;
	case ALLOY_ACT_DPI_CYCLE:
		return 0x30;
	case ALLOY_ACT_SCROLL_UP:
		return 0x31;
	case ALLOY_ACT_SCROLL_DOWN:
		return 0x32;
	case ALLOY_ACT_KEYBOARD:
		return 0x51;
	case ALLOY_ACT_MEDIA:
		return 0x61;
	case ALLOY_ACT_DISABLED:
	default:
		return 0x00;
	}
}

/* Pure packet builders for unit testing; returns byte count written */
size_t r600_build_dpi(uint8_t preset_idx, uint16_t dpi, uint8_t *buf);
size_t r600_build_polling(uint16_t polling_hz, uint8_t *buf);
size_t r600_build_zone_color(uint8_t led_id, const struct alloy_rgb *color,
			     uint8_t *buf);
size_t r600_build_buttons(const struct alloy_config *cfg, uint8_t *buf);
size_t r600_build_save(uint8_t *buf);

size_t r600_build_dpi(uint8_t preset_idx, uint16_t dpi, uint8_t *buf)
{
	buf[0] = R600_CMD_SENSITIVITY;
	buf[1] = 0x00;
	buf[2] = (uint8_t)(preset_idx == 0 ? 0x01 : 0x02);
	buf[3] = r600_dpi_to_wire(dpi);
	buf[4] = 0x00;
	buf[5] = 0x42;
	return 6;
}

size_t r600_build_polling(uint16_t polling_hz, uint8_t *buf)
{
	uint8_t wire;

	switch (polling_hz) {
	case 125:
		wire = 0x04;
		break;
	case 250:
		wire = 0x03;
		break;
	case 500:
		wire = 0x02;
		break;
	case 1000:
	default:
		wire = 0x01;
		break;
	}

	buf[0] = R600_CMD_POLLING;
	buf[1] = 0x00;
	buf[2] = wire;
	return 3;
}

size_t r600_build_zone_color(uint8_t led_id, const struct alloy_rgb *color,
			     uint8_t *buf)
{
	buf[0] = R600_CMD_COLOR;
	buf[1] = 0x00;
	memset(buf + 2, 0, 28);
	buf[2] = led_id; /* offset 0 in header */
	buf[7] = led_id; /* offset 5 in header */
	buf[8] = 0xE8; /* duration 1000ms low byte (offset 6 in header) */
	buf[9] = 0x03; /* duration 1000ms high byte (offset 7 in header) */
	buf[24] = 0x01; /* repeat flag (offset 22 in header) */
	buf[29] = 0x01; /* color count (offset 27 in header) */
	buf[30] = color->r;
	buf[31] = color->g;
	buf[32] = color->b;
	return 33;
}

size_t r600_build_buttons(const struct alloy_config *cfg, uint8_t *buf)
{
	size_t i;

	buf[0] = R600_CMD_BUTTONS;
	buf[1] = 0x00;
	memset(buf + 2, 0, 7 * 5);

	for (i = 0; i < 7; i++) {
		const struct alloy_action *act = &cfg->buttons[i];
		uint8_t *field = buf + 2 + i * 5;

		field[0] = r600_action_first_byte(act);
		if (act->type == ALLOY_ACT_KEYBOARD ||
		    act->type == ALLOY_ACT_MEDIA)
			field[1] = (uint8_t)act->value;
	}
	return 37;
}

size_t r600_build_save(uint8_t *buf)
{
	buf[0] = R600_CMD_SAVE;
	buf[1] = 0x00;
	return 2;
}

static int r600_apply_dpi(struct alloy_device *dev,
			  const struct alloy_config *cfg)
{
	uint8_t buf[ALLOY_HID_REPORT_SIZE];
	size_t len;
	int rc;

	len = r600_build_dpi(0, cfg->dpi[0][0], buf);
	rc = alloy_hid_send(&dev->hid, buf, len);
	if (rc < 0)
		return rc;

	if (cfg->dpi_count > 1)
		len = r600_build_dpi(1, cfg->dpi[1][0], buf);
	else
		len = r600_build_dpi(1, cfg->dpi[0][0], buf);

	return alloy_hid_send(&dev->hid, buf, len);
}

static int r600_apply_polling(struct alloy_device *dev,
			      const struct alloy_config *cfg)
{
	uint8_t buf[ALLOY_HID_REPORT_SIZE];
	size_t len;

	len = r600_build_polling(cfg->polling_hz, buf);
	return alloy_hid_send(&dev->hid, buf, len);
}

static int r600_apply_colors(struct alloy_device *dev,
			     const struct alloy_config *cfg)
{
	uint8_t buf[ALLOY_HID_REPORT_SIZE];
	size_t len;
	uint8_t i;
	int rc;

	/* 8 LED zones (led_id 0x00 through 0x07) */
	for (i = 0; i < 8; i++) {
		len = r600_build_zone_color(i, &cfg->zone_color[i], buf);
		rc = alloy_hid_send(&dev->hid, buf, len);
		if (rc < 0)
			return rc;
	}
	return 0;
}

static int r600_apply_buttons(struct alloy_device *dev,
			      const struct alloy_config *cfg)
{
	uint8_t buf[ALLOY_HID_REPORT_SIZE];
	size_t len;

	len = r600_build_buttons(cfg, buf);
	return alloy_hid_send(&dev->hid, buf, len);
}

static int r600_save(struct alloy_device *dev)
{
	uint8_t buf[ALLOY_HID_REPORT_SIZE];
	size_t len;

	len = r600_build_save(buf);
	return alloy_hid_send(&dev->hid, buf, len);
}

static const uint16_t r600_polling_rates[] = { 1000, 500, 250, 125 };

static const struct alloy_led_zone r600_zones[] = {
	{ .name = "WHEEL", .def_color = { 0xFF, 0x18, 0x00 } },
	{ .name = "LOGO", .def_color = { 0xFF, 0x18, 0x00 } },
	{ .name = "LEFT STRIP TOP", .def_color = { 0xFF, 0x18, 0x00 } },
	{ .name = "RIGHT STRIP TOP", .def_color = { 0xFF, 0x18, 0x00 } },
	{ .name = "LEFT STRIP MID", .def_color = { 0xFF, 0x18, 0x00 } },
	{ .name = "RIGHT STRIP MID", .def_color = { 0xFF, 0x18, 0x00 } },
	{ .name = "LEFT STRIP BOT", .def_color = { 0xFF, 0x18, 0x00 } },
	{ .name = "RIGHT STRIP BOT", .def_color = { 0xFF, 0x18, 0x00 } },
};

static const struct alloy_button r600_buttons[] = {
	{ "Button 1 (Left)", { ALLOY_ACT_MOUSE, 1 } },
	{ "Button 2 (Right)", { ALLOY_ACT_MOUSE, 2 } },
	{ "Button 3 (Middle)", { ALLOY_ACT_MOUSE, 3 } },
	{ "Button 4 (Side Back)", { ALLOY_ACT_MOUSE, 4 } },
	{ "Button 5 (Side Forward)", { ALLOY_ACT_MOUSE, 5 } },
	{ "Button 6 (Side Front)", { ALLOY_ACT_DISABLED, 0 } },
	{ "Button 7 (CPI)", { ALLOY_ACT_DPI_CYCLE, 0 } },
};

static const struct alloy_driver_ops r600_ops = {
	.apply_dpi = r600_apply_dpi,
	.apply_polling = r600_apply_polling,
	.apply_colors = r600_apply_colors,
	.apply_buttons = r600_apply_buttons,
	.save = r600_save,
};

#define R600_DRIVER(sym, drv_name, pid)                                        \
	static const struct alloy_driver sym = {                             \
		.name = drv_name,                                            \
		.vendor_id = 0x1038,                                         \
		.product_id = pid,                                           \
		.interface = 0,                                              \
		.dpi = {                                                     \
			.min = R600_DPI_MIN,                                 \
			.max = R600_DPI_MAX,                                 \
			.step = R600_DPI_STEP,                               \
			.max_presets = 2,                                    \
		},                                                           \
		.polling_rates = r600_polling_rates,                         \
		.num_polling_rates = ALLOY_ARRAY_SIZE(r600_polling_rates),   \
		.zones = r600_zones,                                         \
		.num_zones = ALLOY_ARRAY_SIZE(r600_zones),                   \
		.buttons = r600_buttons,                                     \
		.num_buttons = ALLOY_ARRAY_SIZE(r600_buttons),               \
		.caps = 0,                                                   \
		.ascii_art = alloy_default_mouse_art,                         \
		.ops = &r600_ops,                                            \
		.config_defaults = alloy_config_generic_defaults,            \
	}; \
	ALLOY_DRIVER_REGISTER(sym)

R600_DRIVER(steelseries_rival600, "SteelSeries Rival 600", 0x1724);
R600_DRIVER(steelseries_rival600_dota2, "SteelSeries Rival 600 Dota 2 Edition",
	    0x172E);
