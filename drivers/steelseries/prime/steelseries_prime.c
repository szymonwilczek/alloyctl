// SPDX-License-Identifier: GPL-2.0-only
/*
 * SteelSeries Prime, USB IDs 1038:182E, 1038:182A, 1038:1856.
 *
 * Protocol notes live in Documentation/protocol/steelseries-prime.rst.
 *
 * Configuration uses HID reports on USB interface 0.
 * Commands send fire-and-forget.
 */
#include <stdio.h>
#include <string.h>

#include "default_art.h"
#include "hid.h"
#include "lib/mouse.h"
#include "steelseries/steelseries_common.h"

#define PRIME_CMD_SENSITIVITY 0x61
#define PRIME_CMD_POLLING 0x5D
#define PRIME_CMD_COLOR 0x62
#define PRIME_CMD_BRIGHTNESS 0x5F
#define PRIME_CMD_BUTTONS 0x5B
#define PRIME_CMD_SAVE 0x59

#define PRIME_DPI_MIN 50
#define PRIME_DPI_MAX 18000
#define PRIME_DPI_STEP 50

static uint16_t prime_dpi_to_wire(uint16_t dpi)
{
	uint16_t clamped;

	clamped = ALLOY_CLAMP(dpi, PRIME_DPI_MIN, PRIME_DPI_MAX);
	clamped = (uint16_t)(((clamped - PRIME_DPI_MIN + (PRIME_DPI_STEP / 2)) /
			      PRIME_DPI_STEP) *
				     PRIME_DPI_STEP +
			     PRIME_DPI_MIN);
	clamped = ALLOY_CLAMP(clamped, PRIME_DPI_MIN, PRIME_DPI_MAX);
	return (uint16_t)(clamped / PRIME_DPI_STEP);
}

static uint8_t prime_action_first_byte(const struct alloy_action *act)
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
size_t prime_build_dpi(const struct alloy_config *cfg, uint8_t *buf);
size_t prime_build_polling(uint16_t polling_hz, uint8_t *buf);
size_t prime_build_color(const struct alloy_rgb *color, uint8_t *buf);
size_t prime_build_brightness(uint8_t brightness, uint8_t *buf);
size_t prime_build_buttons(const struct alloy_config *cfg, uint8_t *buf);
size_t prime_build_save(uint8_t *buf);

size_t prime_build_dpi(const struct alloy_config *cfg, uint8_t *buf)
{
	uint8_t count;
	uint8_t active;
	uint8_t i;
	size_t n = 0;

	count = ALLOY_CLAMP(alloy_mouse_cfg_c(cfg)->dpi_count, 1, 5);
	active = (alloy_mouse_cfg_c(cfg)->dpi_active < count) ?
			 alloy_mouse_cfg_c(cfg)->dpi_active :
			 0;

	buf[n++] = PRIME_CMD_SENSITIVITY;
	buf[n++] = count;
	buf[n++] = active;

	for (i = 0; i < count; i++) {
		uint16_t wire =
			prime_dpi_to_wire(alloy_mouse_cfg_c(cfg)->dpi[i][0]);

		buf[n++] = (uint8_t)(wire & 0xFF);
		buf[n++] = (uint8_t)((wire >> 8) & 0xFF);
	}
	return n;
}

size_t prime_build_polling(uint16_t polling_hz, uint8_t *buf)
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

	buf[0] = PRIME_CMD_POLLING;
	buf[1] = wire;
	return 2;
}

size_t prime_build_color(const struct alloy_rgb *color, uint8_t *buf)
{
	buf[0] = PRIME_CMD_COLOR;
	buf[1] = 0x01;
	buf[2] = color->r;
	buf[3] = color->g;
	buf[4] = color->b;
	memset(buf + 5, 0, 15);
	buf[20] = 0xFF;
	return 21;
}

size_t prime_build_brightness(uint8_t brightness, uint8_t *buf)
{
	uint16_t wire;

	brightness = ALLOY_MIN(brightness, 100);
	wire = (uint16_t)(((uint32_t)brightness * 256 + 50) / 100);
	if (wire > 256)
		wire = 256;

	buf[0] = PRIME_CMD_BRIGHTNESS;
	buf[1] = (uint8_t)(wire & 0xFF);
	buf[2] = (uint8_t)((wire >> 8) & 0xFF);
	return 3;
}

size_t prime_build_buttons(const struct alloy_config *cfg, uint8_t *buf)
{
	size_t i;

	buf[0] = PRIME_CMD_BUTTONS;
	memset(buf + 1, 0, 6 * 5);

	for (i = 0; i < 6; i++) {
		const struct alloy_action *act =
			&alloy_mouse_cfg_c(cfg)->buttons[i];
		uint8_t *field = buf + 1 + i * 5;

		field[0] = prime_action_first_byte(act);
		if (act->type == ALLOY_ACT_KEYBOARD ||
		    act->type == ALLOY_ACT_MEDIA)
			field[1] = (uint8_t)act->value;
	}
	return 31;
}

size_t prime_build_save(uint8_t *buf)
{
	buf[0] = PRIME_CMD_SAVE;
	return 1;
}

static int prime_apply_dpi(struct alloy_device *dev,
			   const struct alloy_config *cfg)
{
	uint8_t buf[ALLOY_HID_REPORT_SIZE];
	size_t len;

	len = prime_build_dpi(cfg, buf);
	return alloy_dev_write(dev, buf, len);
}

static int prime_apply_polling(struct alloy_device *dev,
			       const struct alloy_config *cfg)
{
	uint8_t buf[ALLOY_HID_REPORT_SIZE];
	size_t len;

	len = prime_build_polling(alloy_devcfg_c(cfg)->polling_hz, buf);
	return alloy_dev_write(dev, buf, len);
}

static int prime_apply_colors(struct alloy_device *dev,
			      const struct alloy_config *cfg)
{
	uint8_t buf[ALLOY_HID_REPORT_SIZE];
	size_t len;

	len = prime_build_color(&alloy_devcfg_c(cfg)->zone_color[0], buf);
	return alloy_dev_write(dev, buf, len);
}

static int prime_apply_brightness(struct alloy_device *dev,
				  const struct alloy_config *cfg)
{
	uint8_t buf[ALLOY_HID_REPORT_SIZE];
	size_t len;

	len = prime_build_brightness(alloy_devcfg_c(cfg)->brightness, buf);
	return alloy_dev_write(dev, buf, len);
}

static int prime_apply_buttons(struct alloy_device *dev,
			       const struct alloy_config *cfg)
{
	uint8_t buf[ALLOY_HID_REPORT_SIZE];
	size_t len;

	len = prime_build_buttons(cfg, buf);
	return alloy_dev_write(dev, buf, len);
}

static int prime_save(struct alloy_device *dev)
{
	uint8_t buf[ALLOY_HID_REPORT_SIZE];
	size_t len;

	len = prime_build_save(buf);
	return alloy_dev_write(dev, buf, len);
}

static const uint16_t prime_polling_rates[] = { 1000, 500, 250, 125 };

static const struct alloy_led_zone prime_zones[] = {
	{ .name = "WHEEL", .def_color = { 0xFF, 0x52, 0x00 } },
};

static const struct alloy_button prime_buttons[] = {
	{ "Button 1 (Left)", { ALLOY_ACT_MOUSE, 1 } },
	{ "Button 2 (Right)", { ALLOY_ACT_MOUSE, 2 } },
	{ "Button 3 (Middle)", { ALLOY_ACT_MOUSE, 3 } },
	{ "Button 4 (Side Back)", { ALLOY_ACT_MOUSE, 4 } },
	{ "Button 5 (Side Forward)", { ALLOY_ACT_MOUSE, 5 } },
	{ "Button 6 (CPI)", { ALLOY_ACT_DPI_CYCLE, 0 } },
};

static const struct alloy_mouse_info prime_mouse = {
	.dpi = {
		.min = PRIME_DPI_MIN,
		.max = PRIME_DPI_MAX,
		.step = PRIME_DPI_STEP,
		.max_presets = 5,
	},
	.buttons = prime_buttons,
	.num_buttons = ALLOY_ARRAY_SIZE(prime_buttons),
};

static const struct alloy_devinfo prime_info = {
	.caps = ALLOY_CAP_COLOR | ALLOY_CAP_BRIGHTNESS | ALLOY_CAP_ACCEL |
		ALLOY_CAP_DECEL | ALLOY_CAP_ANGLE_SNAPPING | ALLOY_CAP_DPI |
		ALLOY_CAP_BUTTONS,
	.polling_rates = prime_polling_rates,
	.num_polling_rates = ALLOY_ARRAY_SIZE(prime_polling_rates),
	.zones = prime_zones,
	.num_zones = ALLOY_ARRAY_SIZE(prime_zones),
	.ext = &prime_mouse,
};

static const struct alloy_apply_step prime_steps[] = {
	{ ALLOY_STEP_DPI, ALLOY_APPLY_SKIP_SYNC, prime_apply_dpi },
	{ ALLOY_STEP_POLLING, 0, prime_apply_polling },
	{ ALLOY_STEP_COLORS, 0, prime_apply_colors },
	{ ALLOY_STEP_BRIGHTNESS, 0, prime_apply_brightness },
	{ ALLOY_STEP_BUTTONS, 0, prime_apply_buttons },
};

static const struct alloy_driver_ops prime_ops = {
	.config_defaults = alloy_mouse_defaults,
	.state_save = alloy_mouse_state_save,
	.state_load = alloy_mouse_state_load,
	.state_done = alloy_mouse_state_done,
	.save = prime_save,
};

static const struct alloy_cli_table prime_cli[] = {
	{ alloy_devcfg_cli_options, ALLOY_DEVCFG_CLI_COUNT },
	{ alloy_mouse_cli_options, ALLOY_MOUSE_CLI_COUNT },
};

static const struct alloy_hid_params prime_hid = {
	.interface = 0,
};

#define PRIME_DRIVER(sym, drv_name, pid)                          \
	static const struct alloy_driver sym = {                  \
		.name = drv_name,                                 \
		.kind = "mouse",                                  \
		.vendor_id = 0x1038,                              \
		.product_id = pid,                                \
		.transport_data = &prime_hid,                     \
		.config_size = sizeof(struct alloy_mouse_config), \
		.data = &prime_info,                              \
		.ascii_art = alloy_default_mouse_art,             \
		.cli_tables = prime_cli,                          \
		.num_cli_tables = ALLOY_ARRAY_SIZE(prime_cli),    \
		.apply_steps = prime_steps,                       \
		.num_apply_steps = ALLOY_ARRAY_SIZE(prime_steps), \
		.ui = &alloy_mouse_ui,                            \
		.ops = &prime_ops,                                \
	};                                                        \
	ALLOY_DRIVER_REGISTER(sym)

PRIME_DRIVER(steelseries_prime, "SteelSeries Prime", 0x182E);
PRIME_DRIVER(steelseries_prime_black_ice,
	     "SteelSeries Prime Rainbow 6 Siege Black Ice Edition", 0x182A);
PRIME_DRIVER(steelseries_prime_neo_noir,
	     "SteelSeries Prime CS:GO Neo Noir Edition", 0x1856);
