// SPDX-License-Identifier: GPL-2.0-only
/*
 * SteelSeries Rival 3 (Gen 1, wired), USB IDs 1038:1824 and 1038:184C
 * (firmware v0.37+ revision; same protocol).
 *
 * Protocol notes live in Documentation/protocol/steelseries-rival3.rst.
 * Maintainer: Szymon Wilczek <swilczek.lx@gmail.com>
 *
 * Configuration uses 32-byte HID output reports on USB interface 3.
 * Unlike the Gen 2, this firmware does NOT acknowledge commands on
 * the interrupt IN endpoint (only the firmware version query 0x10
 * answers), so every op sends fire-and-forget.
 *
 * Every command starts with <cmd> 0x00. Lighting is four zones
 * (the three-zone strip plus the logo), each set individually with
 * per-write brightness byte, plus a global light effect selector
 * that includes three breathing speeds, rainbow modes and disco.
 */
#include <stdio.h>
#include <string.h>

#include "driver.h"

#define R3_CMD_POLLING 0x04
#define R3_CMD_ZONE_COLOR 0x05
#define R3_CMD_LIGHT_EFFECT 0x06
#define R3_CMD_BUTTONS 0x07
#define R3_CMD_SAVE 0x09
#define R3_CMD_DPI 0x0B
#define R3_CMD_FIRMWARE 0x10

#define R3_REPORT_SIZE 32

#define R3_DPI_MIN 200
#define R3_DPI_MAX 8500
#define R3_DPI_STEP 100

/* same TrueMove Core sensor and table as the Gen 2 */
static const uint8_t r3_dpi_table[] = {
	0x04, 0x06, 0x08, 0x0B, 0x0D, 0x0F, 0x12, 0x14, 0x16, 0x19, 0x1B, 0x1D,
	0x20, 0x22, 0x24, 0x27, 0x29, 0x2B, 0x2E, 0x30, 0x32, 0x34, 0x37, 0x39,
	0x3B, 0x3E, 0x40, 0x42, 0x45, 0x47, 0x49, 0x4C, 0x4E, 0x50, 0x53, 0x55,
	0x57, 0x5A, 0x5C, 0x5E, 0x61, 0x63, 0x65, 0x68, 0x6A, 0x6C, 0x6F, 0x71,
	0x73, 0x76, 0x78, 0x7A, 0x7D, 0x7F, 0x81, 0x84, 0x86, 0x88, 0x8B, 0x8D,
	0x8F, 0x92, 0x94, 0x96, 0x99, 0x9B, 0x9D, 0xA0, 0xA2, 0xA4, 0xA7, 0xA9,
	0xAB, 0xAD, 0xB0, 0xB2, 0xB4, 0xB7, 0xB9, 0xBC, 0xBE, 0xC0, 0xC3, 0xC5,
};

static uint8_t r3_dpi_to_wire(uint16_t dpi)
{
	uint16_t clamped;

	clamped = ALLOY_CLAMP(dpi, R3_DPI_MIN, R3_DPI_MAX);
	clamped = (uint16_t)((clamped / R3_DPI_STEP) * R3_DPI_STEP);
	return r3_dpi_table[(clamped - R3_DPI_MIN) / R3_DPI_STEP];
}

/*
 * Global light effects, index 0 = steady (static colors).
 * Wire values are not in this order, hence the mapping table.
 */
static const char *const r3_fx_names[] = {
	"STEADY",	 "BREATH SLOW",	   "BREATH", "BREATH FAST",
	"RAINBOW SHIFT", "RAINBOW BREATH", "DISCO",
};

static const uint8_t r3_fx_wire[] = {
	0x04, /* steady */
	0x03, /* breath slow */
	0x02, /* breath */
	0x01, /* breath fast */
	0x00, /* rainbow shift */
	0x05, /* rainbow breath */
	0x06, /* disco */
};

/* Packet builders are pure for unit testing; length returned */
size_t r3_build_dpi(const struct alloy_config *cfg, uint8_t *buf);
size_t r3_build_polling(const struct alloy_config *cfg, uint8_t *buf);
size_t r3_build_zone_color(const struct alloy_config *cfg, int zone,
			   uint8_t *buf);
size_t r3_build_effect(const struct alloy_config *cfg, uint8_t *buf);
size_t r3_build_buttons(const struct alloy_config *cfg, uint8_t *buf);

size_t r3_build_dpi(const struct alloy_config *cfg, uint8_t *buf)
{
	size_t n = 0;
	uint8_t i;

	buf[n++] = R3_CMD_DPI;
	buf[n++] = 0x00;
	buf[n++] = cfg->dpi_count;
	buf[n++] = (uint8_t)(cfg->dpi_active + 1); /* 1-based on wire */
	for (i = 0; i < cfg->dpi_count; i++)
		buf[n++] = r3_dpi_to_wire(cfg->dpi[i][0]);
	return n;
}

size_t r3_build_polling(const struct alloy_config *cfg, uint8_t *buf)
{
	uint8_t wire;

	switch (cfg->polling_hz) {
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

	buf[0] = R3_CMD_POLLING;
	buf[1] = 0x00;
	buf[2] = wire;
	return 3;
}

/*
 * One zone per write:
 * 0x05 0x00 <zone 1-4> <R> <G> <B> <brightness>
 * There is no global brightness command; per-write byte carries shared brightness setting.
 */
size_t r3_build_zone_color(const struct alloy_config *cfg, int zone,
			   uint8_t *buf)
{
	buf[0] = R3_CMD_ZONE_COLOR;
	buf[1] = 0x00;
	buf[2] = (uint8_t)(zone + 1); /* 1-based; 0 would mean "all" */
	buf[3] = cfg->zone_color[zone].r;
	buf[4] = cfg->zone_color[zone].g;
	buf[5] = cfg->zone_color[zone].b;
	buf[6] = ALLOY_MIN(cfg->brightness, 100);
	return 7;
}

size_t r3_build_effect(const struct alloy_config *cfg, uint8_t *buf)
{
	uint8_t idx = cfg->fx_index;

	if (idx >= ALLOY_ARRAY_SIZE(r3_fx_wire))
		idx = 0;

	buf[0] = R3_CMD_LIGHT_EFFECT;
	buf[1] = 0x00;
	buf[2] = r3_fx_wire[idx];
	return 3;
}

/* Wire ids of the 2-byte action fields, in config order */
static const uint8_t r3_button_wire_id[] = {
	0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x31, 0x32,
};

static uint8_t r3_action_first_byte(const struct alloy_action *act)
{
	switch (act->type) {
	case ALLOY_ACT_MOUSE:
		return (uint8_t)act->value; /* 0x01 - 0x06 */
	case ALLOY_ACT_DPI_CYCLE:
		return 0x30;
	case ALLOY_ACT_SCROLL_UP:
		return 0x31;
	case ALLOY_ACT_SCROLL_DOWN:
		return 0x32;
	case ALLOY_ACT_KEYBOARD:
		return 0x33;
	case ALLOY_ACT_MEDIA:
		return 0x34;
	case ALLOY_ACT_DISABLED:
	default:
		return 0x00;
	}
}

size_t r3_build_buttons(const struct alloy_config *cfg, uint8_t *buf)
{
	size_t n = 2;
	uint8_t i;

	buf[0] = R3_CMD_BUTTONS;
	buf[1] = 0x00;
	memset(buf + 2, 0, 8 * 2);

	for (i = 0; i < ALLOY_ARRAY_SIZE(r3_button_wire_id); i++) {
		const struct alloy_action *act = &cfg->buttons[i];
		uint8_t *field = buf + 2 + i * 2;

		field[0] = r3_action_first_byte(act);
		if (act->type == ALLOY_ACT_KEYBOARD ||
		    act->type == ALLOY_ACT_MEDIA)
			field[1] = (uint8_t)act->value;
	}
	return n + 8 * 2;
}

static int r3_apply_dpi(struct alloy_device *dev,
			const struct alloy_config *cfg)
{
	uint8_t buf[R3_REPORT_SIZE];

	return alloy_hid_send(&dev->hid, buf, r3_build_dpi(cfg, buf));
}

static int r3_apply_polling(struct alloy_device *dev,
			    const struct alloy_config *cfg)
{
	uint8_t buf[R3_REPORT_SIZE];

	return alloy_hid_send(&dev->hid, buf, r3_build_polling(cfg, buf));
}

/*
 * Full lighting state:
 * effect selector first (switching back to steady restores static colors),
 * then all four zones.
 */
static int r3_apply_colors(struct alloy_device *dev,
			   const struct alloy_config *cfg)
{
	uint8_t buf[R3_REPORT_SIZE];
	int ret = 0;
	int zone;

	ret |= alloy_hid_send(&dev->hid, buf, r3_build_effect(cfg, buf));
	for (zone = 0; zone < 4; zone++)
		ret |= alloy_hid_send(&dev->hid, buf,
				      r3_build_zone_color(cfg, zone, buf));
	return ret ? -1 : 0;
}

/* Brightness rides in every color write; just resend the zones */
static int r3_apply_brightness(struct alloy_device *dev,
			       const struct alloy_config *cfg)
{
	uint8_t buf[R3_REPORT_SIZE];
	int ret = 0;
	int zone;

	for (zone = 0; zone < 4; zone++)
		ret |= alloy_hid_send(&dev->hid, buf,
				      r3_build_zone_color(cfg, zone, buf));
	return ret ? -1 : 0;
}

static int r3_apply_buttons(struct alloy_device *dev,
			    const struct alloy_config *cfg)
{
	uint8_t buf[R3_REPORT_SIZE];

	return alloy_hid_send(&dev->hid, buf, r3_build_buttons(cfg, buf));
}

static int r3_save(struct alloy_device *dev)
{
	static const uint8_t cmd[] = { R3_CMD_SAVE, 0x00 };

	return alloy_hid_send(&dev->hid, cmd, sizeof(cmd));
}

static int r3_firmware_version(struct alloy_device *dev, char *buf, size_t len)
{
	static const uint8_t cmd[] = { R3_CMD_FIRMWARE, 0x00 };
	uint8_t resp[R3_REPORT_SIZE];
	int n;

	n = alloy_hid_cmd_read(&dev->hid, cmd, sizeof(cmd), resp, sizeof(resp));
	if (n < 2)
		return -1;

	/* Response: <major> <minor> as raw bytes */
	snprintf(buf, len, "%u.%u", resp[0], resp[1]);
	return 0;
}

static const uint16_t r3_polling_rates[] = { 1000, 500, 250, 125 };

static const struct alloy_led_zone r3_zones[] = {
	{ .name = "TOP", .def_color = { 0xFF, 0x00, 0x00 } },
	{ .name = "MIDDLE", .def_color = { 0x00, 0xFF, 0x00 } },
	{ .name = "BOTTOM", .def_color = { 0x00, 0x00, 0xFF } },
	{ .name = "LOGO", .def_color = { 0xFF, 0x00, 0xFF } },
};

static const struct alloy_button r3_buttons[] = {
	{ "Button 1 (Left)", { ALLOY_ACT_MOUSE, 1 } },
	{ "Button 2 (Right)", { ALLOY_ACT_MOUSE, 2 } },
	{ "Button 3 (Middle)", { ALLOY_ACT_MOUSE, 3 } },
	{ "Button 4 (Back)", { ALLOY_ACT_MOUSE, 4 } },
	{ "Button 5 (Forward)", { ALLOY_ACT_MOUSE, 5 } },
	{ "Button 6 (CPI)", { ALLOY_ACT_DPI_CYCLE, 0 } },
	{ "Scroll Up", { ALLOY_ACT_SCROLL_UP, 0 } },
	{ "Scroll Down", { ALLOY_ACT_SCROLL_DOWN, 0 } },
};

/* clang-format off */
static const char r3_art[] =
	"              _.-------._\n"
	"           ,-'     |     '-.\n"
	"         ,'   .----'----.   ',\n"
	"        /    |     |     |    \\\n"
	"       /     |    | |    |     \\\n"
	"      ;      |    |_|    |      ;\n"
	" B4 --|      |     |     |      |\n"
	"      |       '----'----'       |\n"
	" B5 --|            O -- B6      |\n"
	"      |                         |\n"
	"  Z1 =|        .-'''-.          |= Z1\n"
	"      ;       ( LOGO  )         ;\n"
	"  Z2 =\\        '-...-'   Z4    /= Z2\n"
	"        \\                     /\n"
	"  Z3 ==='.                  ,'=== Z3\n"
	"           '-.           ,-'\n"
	"              '-._____.-'\n";
/* clang-format on */

static const struct alloy_driver_ops r3_ops = {
	.apply_dpi = r3_apply_dpi,
	.apply_polling = r3_apply_polling,
	.apply_colors = r3_apply_colors,
	.apply_brightness = r3_apply_brightness,
	.apply_buttons = r3_apply_buttons,
	.save = r3_save,
	.firmware_version = r3_firmware_version,
};

#define R3_DRIVER(sym, drv_name, pid)                                       \
	static const struct alloy_driver sym = {                          \
		.name = drv_name,                                         \
		.vendor_id = 0x1038,                                      \
		.product_id = pid,                                        \
		.interface = 3,                                           \
		.report_size = R3_REPORT_SIZE,                            \
		.dpi = {                                                  \
			.min = R3_DPI_MIN,                                \
			.max = R3_DPI_MAX,                                \
			.step = R3_DPI_STEP,                              \
			.max_presets = 5,                                 \
		},                                                        \
		.polling_rates = r3_polling_rates,                        \
		.num_polling_rates = ALLOY_ARRAY_SIZE(r3_polling_rates),  \
		.zones = r3_zones,                                        \
		.num_zones = ALLOY_ARRAY_SIZE(r3_zones),                  \
		.buttons = r3_buttons,                                    \
		.num_buttons = ALLOY_ARRAY_SIZE(r3_buttons),              \
		.caps = ALLOY_CAP_BRIGHTNESS |                            \
			ALLOY_CAP_FIRMWARE_VERSION | ALLOY_CAP_FX_GLOBAL, \
		.fx_names = r3_fx_names,                                  \
		.num_fx = ALLOY_ARRAY_SIZE(r3_fx_names),                  \
		.ascii_art = r3_art,                                      \
		.ops = &r3_ops,                                           \
		.config_defaults = alloy_config_generic_defaults,         \
	}; \
	ALLOY_DRIVER_REGISTER(sym)

R3_DRIVER(steelseries_rival3, "SteelSeries Rival 3", 0x1824);
R3_DRIVER(steelseries_rival3_fw037, "SteelSeries Rival 3 (fw 0.37+)", 0x184C);
