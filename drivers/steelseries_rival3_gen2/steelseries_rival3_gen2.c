// SPDX-License-Identifier: GPL-2.0-only
/*
 * SteelSeries Rival 3 Gen 2 (wired), USB ID 1038:1870.
 *
 * Protocol notes live in Documentation/protocol/steelseries-rival3-gen2.rst.
 * Maintainer: Szymon Wilczek <swilczek.lx@gmail.com>
 *
 * Configuration uses 64-byte HID output reports on USB interface 3;
 * every recognized command is ACKed by echo of the command byte.
 *
 * TrueMove Core sensor takes one wire byte per axis;
 * mapping from DPI to that byte is not linear
 * (about dpi/43.1 with rounding quirks), so the exact table from
 * the sensor family is used.
 */
#include <string.h>

#include "driver.h"
#include "art_steelseries_rival3_gen2.h"

#define R3G2_CMD_SAVE 0x11
#define R3G2_CMD_ZONE_COLORS 0x21
#define R3G2_CMD_RAINBOW 0x22
#define R3G2_CMD_BRIGHTNESS 0x23
#define R3G2_CMD_REACTIVE 0x26
#define R3G2_CMD_STARTUP_FX 0x27
#define R3G2_CMD_BUTTONS 0x2A
#define R3G2_CMD_POLLING 0x2B
#define R3G2_CMD_DPI 0x34
#define R3G2_CMD_FIRMWARE 0x90

/* unsolicited event on the vendor interface (2), not the config one */
#define R3G2_EVT_CPI_LEVEL 0xAD

#define R3G2_DPI_MIN 200
#define R3G2_DPI_MAX 8500
#define R3G2_DPI_STEP 100

/*
 * TrueMove Core DPI-to-wire table, one entry per 100 DPI starting at 200.
 * Index: (dpi - 200) / 100.
 */
static const uint8_t r3g2_dpi_table[] = {
	0x04, 0x06, 0x08, 0x0B, 0x0D, 0x0F, 0x12, 0x14, 0x16, 0x19, 0x1B, 0x1D,
	0x20, 0x22, 0x24, 0x27, 0x29, 0x2B, 0x2E, 0x30, 0x32, 0x34, 0x37, 0x39,
	0x3B, 0x3E, 0x40, 0x42, 0x45, 0x47, 0x49, 0x4C, 0x4E, 0x50, 0x53, 0x55,
	0x57, 0x5A, 0x5C, 0x5E, 0x61, 0x63, 0x65, 0x68, 0x6A, 0x6C, 0x6F, 0x71,
	0x73, 0x76, 0x78, 0x7A, 0x7D, 0x7F, 0x81, 0x84, 0x86, 0x88, 0x8B, 0x8D,
	0x8F, 0x92, 0x94, 0x96, 0x99, 0x9B, 0x9D, 0xA0, 0xA2, 0xA4, 0xA7, 0xA9,
	0xAB, 0xAD, 0xB0, 0xB2, 0xB4, 0xB7, 0xB9, 0xBC, 0xBE, 0xC0, 0xC3, 0xC5,
};

static uint8_t r3g2_dpi_to_wire(uint16_t dpi)
{
	uint16_t clamped;

	clamped = ALLOY_CLAMP(dpi, R3G2_DPI_MIN, R3G2_DPI_MAX);
	/* Snap to the 100 DPI grid the sensor table is built on. */
	clamped = (uint16_t)((clamped / R3G2_DPI_STEP) * R3G2_DPI_STEP);
	return r3g2_dpi_table[(clamped - R3G2_DPI_MIN) / R3G2_DPI_STEP];
}

/*
 * Packet builders are pure functions over struct alloy_config so the wire format
 * can be unit tested without hardware.
 * Each returns the payload length.
 */
size_t r3g2_build_dpi(const struct alloy_config *cfg, uint8_t *buf);
size_t r3g2_build_polling(const struct alloy_config *cfg, uint8_t *buf);
size_t r3g2_build_colors(const struct alloy_config *cfg, uint8_t *buf);
size_t r3g2_build_rainbow(const struct alloy_config *cfg, uint8_t *buf);
size_t r3g2_build_reactive(const struct alloy_config *cfg, uint8_t *buf);
size_t r3g2_build_startup(const struct alloy_config *cfg, uint8_t *buf);
size_t r3g2_build_brightness(const struct alloy_config *cfg, uint8_t *buf);
size_t r3g2_build_buttons(const struct alloy_config *cfg, uint8_t *buf);
int r3g2_parse_event(const uint8_t *buf, size_t len, struct alloy_config *cfg);

size_t r3g2_build_dpi(const struct alloy_config *cfg, uint8_t *buf)
{
	size_t n = 0;
	uint8_t i;

	buf[n++] = R3G2_CMD_DPI;
	buf[n++] = cfg->dpi_count;
	/*
	 * Active index is 0-based on the wire, matching the 0xAD level event
	 * (r3g2_parse_event) the firmware reports back.
	 * Sending dpi_active + 1 here selected the *next* level,
	 * which SAVE then latched to flash (#41)
	 */
	buf[n++] = cfg->dpi_active;
	for (i = 0; i < cfg->dpi_count; i++) {
		buf[n++] = r3g2_dpi_to_wire(cfg->dpi[i][0]);
		buf[n++] = r3g2_dpi_to_wire(cfg->dpi[i][1]);
	}
	return n;
}

size_t r3g2_build_polling(const struct alloy_config *cfg, uint8_t *buf)
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

	buf[0] = R3G2_CMD_POLLING;
	buf[1] = wire;
	return 2;
}

/*
 * Static colors: 0x21 <mask> followed by positional RGB triplets.
 * Only zones running in static mode are selected by the mask;
 * zones cycling the rainbow keep doing so.
 * Returns 0 when every zone is on the rainbow (nothing to send).
 */
size_t r3g2_build_colors(const struct alloy_config *cfg, uint8_t *buf)
{
	size_t n = 0;
	uint8_t mask = 0;
	uint8_t i;

	for (i = 0; i < 3; i++) {
		if (!cfg->zone_fx[i])
			mask |= (uint8_t)(1u << i);
	}
	if (!mask)
		return 0;

	buf[n++] = R3G2_CMD_ZONE_COLORS;
	buf[n++] = mask;
	for (i = 0; i < 3; i++) {
		buf[n++] = cfg->zone_color[i].r;
		buf[n++] = cfg->zone_color[i].g;
		buf[n++] = cfg->zone_color[i].b;
	}
	return n;
}

/*
 * Rainbow effect: 0x22 <mask> starts the cycle on the selected zones.
 * Returns 0 when no zone runs the rainbow;
 * Sending a static color afterwards clears the effect on those zones,
 * so ordering is rainbow first, colors second (see r3g2_apply_lighting).
 */
size_t r3g2_build_rainbow(const struct alloy_config *cfg, uint8_t *buf)
{
	uint8_t mask = 0;
	uint8_t i;

	for (i = 0; i < 3; i++) {
		if (cfg->zone_fx[i])
			mask |= (uint8_t)(1u << i);
	}
	if (!mask)
		return 0;

	buf[0] = R3G2_CMD_RAINBOW;
	buf[1] = mask;
	return 2;
}

/*
 * Reactive click color: 0x26 <enable> 0x00 <R> <G> <B>
 *
 * Verified on hardware (fw 1.1.6):
 * Flash overlays whatever the zones are showing (static colors, rainbow or off).
 * Enable byte is mandatory - short 0x26 <R> <G> <B> write is ACKed but latches
 * black flash, which is why the effect looked dead (#24).
 * Color survives neither the 0x11 save nor a power cycle,
 * so the host re-arms it on every apply.
 */
size_t r3g2_build_reactive(const struct alloy_config *cfg, uint8_t *buf)
{
	buf[0] = R3G2_CMD_REACTIVE;
	buf[1] = cfg->reactive_enabled ? 0x01 : 0x00;
	buf[2] = 0x00;
	if (cfg->reactive_enabled) {
		buf[3] = cfg->reactive_color.r;
		buf[4] = cfg->reactive_color.g;
		buf[5] = cfg->reactive_color.b;
	} else {
		buf[3] = 0x00;
		buf[4] = 0x00;
		buf[5] = 0x00;
	}
	return 6;
}

/*
 * Lighting mode: 0x27 <rainbow> <reactive>
 *
 * Hardware-verified (#23): rainbow byte is the live master switch of the rainbow
 * engine, not just the power-up default - 0x22 masks only enroll zones into already
 * running engine and are silently ignored while it is off.
 * Both roles share this one flag, so the effective rainbow bit is "any zone runs
 * the rainbow OR the startup preference wants it".
 * Config with rainbow zones therefore also wakes up cycling, which is the only
 * behaviour the firmware can express.
 */
size_t r3g2_build_startup(const struct alloy_config *cfg, uint8_t *buf)
{
	uint8_t rainbow_zones = 0;
	uint8_t i;

	for (i = 0; i < 3; i++)
		rainbow_zones |= cfg->zone_fx[i];

	buf[0] = R3G2_CMD_STARTUP_FX;
	buf[1] = rainbow_zones != 0 ||
		 cfg->startup_fx == ALLOY_STARTUP_RAINBOW ||
		 cfg->startup_fx == ALLOY_STARTUP_REACTIVE_RAINBOW;
	buf[2] = (cfg->startup_fx == ALLOY_STARTUP_REACTIVE ||
		  cfg->startup_fx == ALLOY_STARTUP_REACTIVE_RAINBOW);
	return 3;
}

size_t r3g2_build_brightness(const struct alloy_config *cfg, uint8_t *buf)
{
	buf[0] = R3G2_CMD_BRIGHTNESS;
	buf[1] = ALLOY_MIN(cfg->brightness, 100);
	return 2;
}

/* Wire ids of the 5-byte action fields, in config order */
static const uint8_t r3g2_button_wire_id[] = {
	0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x31, 0x32,
};

static uint8_t r3g2_action_first_byte(const struct alloy_action *act)
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
		return 0x51;
	case ALLOY_ACT_MEDIA:
		return 0x61;
	case ALLOY_ACT_DISABLED:
	default:
		return 0x00;
	}
}

size_t r3g2_build_buttons(const struct alloy_config *cfg, uint8_t *buf)
{
	size_t n = 1;
	uint8_t i;

	buf[0] = R3G2_CMD_BUTTONS;
	memset(buf + 1, 0, 8 * 5);

	for (i = 0; i < ALLOY_ARRAY_SIZE(r3g2_button_wire_id); i++) {
		const struct alloy_action *act = &cfg->buttons[i];
		uint8_t *field = buf + 1 + i * 5;

		field[0] = r3g2_action_first_byte(act);
		if (act->type == ALLOY_ACT_KEYBOARD ||
		    act->type == ALLOY_ACT_MEDIA)
			field[1] = (uint8_t)act->value;
	}
	return n + 8 * 5;
}

/*
 * CPI-level switch notification, discovered on hardware (fw 1.1.6):
 *
 *   0xAD <count> <active> <wire1> ... <wireN>
 *
 * Emitted on the vendor interface every time the active level changes -
 * including switches made with the physical CPI button.
 * <active> is 0-based and the wire bytes repeat the level table in the 0x34 sensor encoding.
 * Only the active index is taken over:
 * the host configuration stays the source of truth for the level values themselves.
 */
int r3g2_parse_event(const uint8_t *buf, size_t len, struct alloy_config *cfg)
{
	uint8_t active;

	if (len < 3 || buf[0] != R3G2_EVT_CPI_LEVEL)
		return 0;
	active = buf[2];
	if (buf[1] < 1 || buf[1] > ALLOY_MAX_DPI_PRESETS || active >= buf[1])
		return 0;
	if (active >= cfg->dpi_count || active == cfg->dpi_active)
		return 0;
	cfg->dpi_active = active;
	return 1;
}

static int r3g2_apply_dpi(struct alloy_device *dev,
			  const struct alloy_config *cfg)
{
	uint8_t buf[ALLOY_HID_REPORT_SIZE];

	return alloy_hid_cmd(&dev->hid, buf, r3g2_build_dpi(cfg, buf));
}

static int r3g2_apply_polling(struct alloy_device *dev,
			      const struct alloy_config *cfg)
{
	uint8_t buf[ALLOY_HID_REPORT_SIZE];

	return alloy_hid_cmd(&dev->hid, buf, r3g2_build_polling(cfg, buf));
}

/*
 * Full lighting state, in engine order (#23):
 * 0x27 mode goes out first - its rainbow byte is the live master switch
 * of the rainbow engine, and a 0x22 mask sent while the engine is off is
 * silently ignored.
 * Then the mask enrolls the rainbow zones, the masked static colors carve
 * their zones out of the cycle, and the reactive overlay closes the batch.
 */
static int r3g2_apply_colors(struct alloy_device *dev,
			     const struct alloy_config *cfg)
{
	uint8_t buf[ALLOY_HID_REPORT_SIZE];
	size_t len;
	int ret = 0;

	ret |= alloy_hid_cmd(&dev->hid, buf, r3g2_build_startup(cfg, buf));

	len = r3g2_build_rainbow(cfg, buf);
	if (len)
		ret |= alloy_hid_cmd(&dev->hid, buf, len);

	len = r3g2_build_colors(cfg, buf);
	if (len)
		ret |= alloy_hid_cmd(&dev->hid, buf, len);

	ret |= alloy_hid_cmd(&dev->hid, buf, r3g2_build_reactive(cfg, buf));

	return ret ? -1 : 0;
}

static int r3g2_apply_brightness(struct alloy_device *dev,
				 const struct alloy_config *cfg)
{
	uint8_t buf[ALLOY_HID_REPORT_SIZE];

	return alloy_hid_cmd(&dev->hid, buf, r3g2_build_brightness(cfg, buf));
}

static int r3g2_apply_buttons(struct alloy_device *dev,
			      const struct alloy_config *cfg)
{
	uint8_t buf[ALLOY_HID_REPORT_SIZE];

	return alloy_hid_cmd(&dev->hid, buf, r3g2_build_buttons(cfg, buf));
}

static int r3g2_save(struct alloy_device *dev)
{
	static const uint8_t cmd[] = { R3G2_CMD_SAVE, 0x00 };

	return alloy_hid_cmd(&dev->hid, cmd, sizeof(cmd));
}

static int r3g2_firmware_version(struct alloy_device *dev, char *buf,
				 size_t len)
{
	static const uint8_t cmd[] = { R3G2_CMD_FIRMWARE };
	uint8_t resp[ALLOY_HID_REPORT_SIZE];
	int n;
	size_t out;

	n = alloy_hid_cmd_read(&dev->hid, cmd, sizeof(cmd), resp, sizeof(resp));
	if (n < 2 || resp[0] != R3G2_CMD_FIRMWARE)
		return -1;

	/* Response:
	 * 0x90 followed by ASCII string */
	out = ALLOY_MIN((size_t)(n - 1), len - 1);
	memcpy(buf, resp + 1, out);
	buf[out] = '\0';
	return 0;
}

static const uint16_t r3g2_polling_rates[] = { 1000, 500, 250, 125 };

/*
 * Per-zone effect list;
 * index 1 maps to the 0x22 rainbow mask, everything else is steady.
 */
static const char *const r3g2_fx_names[] = { "STEADY", "RAINBOW" };

static const struct alloy_led_zone r3g2_zones[] = {
	{ .name = "TOP", .def_color = { 0xFF, 0x00, 0x00 } },
	{ .name = "MIDDLE", .def_color = { 0x00, 0xFF, 0x00 } },
	{ .name = "BOTTOM", .def_color = { 0x00, 0x00, 0xFF } },
};

static const struct alloy_button r3g2_buttons[] = {
	{ "Button 1 (Left)", { ALLOY_ACT_MOUSE, 1 } },
	{ "Button 2 (Right)", { ALLOY_ACT_MOUSE, 2 } },
	{ "Button 3 (Middle)", { ALLOY_ACT_MOUSE, 3 } },
	{ "Button 4 (Back)", { ALLOY_ACT_MOUSE, 4 } },
	{ "Button 5 (Forward)", { ALLOY_ACT_MOUSE, 5 } },
	{ "Button 6 (CPI)", { ALLOY_ACT_DPI_CYCLE, 0 } },
	{ "Scroll Up", { ALLOY_ACT_SCROLL_UP, 0 } },
	{ "Scroll Down", { ALLOY_ACT_SCROLL_DOWN, 0 } },
};

static const struct alloy_driver_ops r3g2_ops = {
	.apply_dpi = r3g2_apply_dpi,
	.apply_polling = r3g2_apply_polling,
	.apply_colors = r3g2_apply_colors,
	.apply_brightness = r3g2_apply_brightness,
	.apply_buttons = r3g2_apply_buttons,
	.save = r3g2_save,
	.firmware_version = r3g2_firmware_version,
	.parse_event = r3g2_parse_event,
};

static const struct alloy_driver steelseries_rival3_gen2 = {
	.name = "SteelSeries Rival 3 Gen 2",
	.vendor_id = 0x1038,
	.product_id = 0x1870,
	.interface = 3,
	.event_interface = 2,
	.dpi = {
		.min = R3G2_DPI_MIN,
		.max = R3G2_DPI_MAX,
		.step = R3G2_DPI_STEP,
		.max_presets = 5,
	},
	.polling_rates = r3g2_polling_rates,
	.num_polling_rates = ALLOY_ARRAY_SIZE(r3g2_polling_rates),
	.zones = r3g2_zones,
	.num_zones = ALLOY_ARRAY_SIZE(r3g2_zones),
	.buttons = r3g2_buttons,
	.num_buttons = ALLOY_ARRAY_SIZE(r3g2_buttons),
	.caps = ALLOY_CAP_BRIGHTNESS | ALLOY_CAP_FIRMWARE_VERSION |
		ALLOY_CAP_FX_RAINBOW | ALLOY_CAP_FX_REACTIVE |
		ALLOY_CAP_FX_STARTUP,
	.fx_names = r3g2_fx_names,
	.num_fx = ALLOY_ARRAY_SIZE(r3g2_fx_names),
	.ascii_art = alloy_art_steelseries_rival3_gen2,
	.ops = &r3g2_ops,
	.config_defaults = alloy_config_generic_defaults,
};

ALLOY_DRIVER_REGISTER(steelseries_rival3_gen2);
