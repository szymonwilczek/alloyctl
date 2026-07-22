// SPDX-License-Identifier: GPL-2.0-only
/*
 * SteelSeries Aerox 3 Wireless (2.4 GHz receiver mode), USB ID 1038:1838.
 *
 * First wireless driver in the tree.
 *
 * Besides the usual DPI / polling / lighting / button configuration it reads
 * the battery gauge (ALLOY_CAP_BATTERY) and drives the High-Efficiency power
 * saver (ALLOY_CAP_HIGH_EFFICIENCY).
 * Everything else is the familiar 64-byte SteelSeries vendor protocol with one
 * twist: in receiver mode every command byte carries the 0x40 "wireless" flag
 * (verified on hardware - the wired opcodes 0x21/0x92 stay silent while their
 * flagged forms 0x61/0xD2 ACK).
 *
 * Config lives on interface 3 (usage page 0xFFC0),
 * unsolicited events on interface 4 (0xFFC1).
 *
 * Protocol notes live in Documentation/protocol/steelseries-aerox3-wireless.rst.
 * Maintainer: Szymon Wilczek <swilczek.lx@gmail.com>
 *
 * Every recognized command is ACKed by echo of the command byte on the
 * interrupt IN endpoint; firmware-version query (0x90) is the lone exception,
 * answering with bare ASCII string and no echo.
 */
#include <stdio.h>
#include <string.h>

#include "driver.h"
#include "art_steelseries_aerox3_wireless.h"

/*
 * Command bytes.
 * Receiver firmware takes the wired SteelSeries opcodes OR'd with 0x40;
 * Wired value is noted for cross-reference with the Aerox 3 (wired) rivalcfg profile.
 */
#define A3WL_CMD_SAVE 0x51 /* wired 0x11 */
#define A3WL_CMD_ZONE_COLOR 0x61 /* wired 0x21 */
#define A3WL_CMD_RAINBOW 0x62 /* wired 0x22 */
#define A3WL_CMD_ILLUM \
	0x63 /* wired 0x23: brightness + smart mode + dim timer */
#define A3WL_CMD_REACTIVE 0x66 /* wired 0x26 */
#define A3WL_CMD_STARTUP_FX 0x67 /* wired 0x27 */
#define A3WL_CMD_HIGHEFF 0x68 /* wired 0x28: High-Efficiency power saver */
#define A3WL_CMD_SLEEP 0x69 /* wired 0x29 (not driven yet) */
#define A3WL_CMD_BUTTONS 0x6A /* wired 0x2A */
#define A3WL_CMD_POLLING 0x6B /* wired 0x2B */
#define A3WL_CMD_DPI 0x6D /* wired 0x2D */
#define A3WL_CMD_FIRMWARE 0x90 /* not flagged; bare ASCII reply */
#define A3WL_CMD_BATTERY 0xD2 /* wired 0x92 */

/* unsolicited events, delivered on the vendor event interface (4) */
#define A3WL_EVT_CPI_LEVEL 0xAD /* active CPI-level switch */
#define A3WL_EVT_POWER 0xBC /* 0xBC <01 wake/link | 00 sleep/unlink> */
#define A3WL_EVT_BATTERY 0x12 /* 0x12 <lvl>: battery push, 0xD2 encoding */

/*
 * High-Efficiency forces these two registers while it is on;
 * exact bundle GG emits (verified on hardware, see a3wl_apply_high_efficiency).
 */
#define A3WL_HIGHEFF_POLLING_HZ 125
#define A3WL_HIGHEFF_BRIGHTNESS 0

#define A3WL_DPI_MIN 100
#define A3WL_DPI_MAX 18000
#define A3WL_DPI_STEP 100

/* illumination intensity is 4-bit level; SteelSeries GG maps 0-100% onto it */
#define A3WL_BRIGHTNESS_MAX 0x0F

/* battery response byte: bit7 = charging, low 7 bits = (level% / 5) + 1 */
#define A3WL_BAT_CHARGING 0x80

/*
 * TrueMove Air DPI-to-wire table, one entry per 100 DPI starting at 100.
 * Index: (dpi - 100) / 100.
 * Values from the sensor family table cross-checked against rivalcfg
 * (100 -> 0x00, 400 -> 0x04, 800 -> 0x09, 18000 -> 0xD6)
 */
static const uint8_t a3wl_dpi_table[] = {
	0x00, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x09, 0x0A, 0x0B, 0x0C, 0x0D,
	0x0E, 0x10, 0x11, 0x12, 0x13, 0x14, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B,
	0x1D, 0x1E, 0x1F, 0x20, 0x21, 0x23, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A,
	0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x32, 0x33, 0x34, 0x35, 0x36, 0x38, 0x39,
	0x3A, 0x3B, 0x3C, 0x3E, 0x3F, 0x40, 0x41, 0x42, 0x44, 0x45, 0x46, 0x47,
	0x48, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x50, 0x51, 0x52, 0x53, 0x54, 0x56,
	0x57, 0x58, 0x59, 0x5A, 0x5C, 0x5D, 0x5E, 0x5F, 0x60, 0x62, 0x63, 0x64,
	0x65, 0x66, 0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6E, 0x6F, 0x70, 0x71, 0x72,
	0x74, 0x75, 0x76, 0x77, 0x78, 0x7A, 0x7B, 0x7C, 0x7D, 0x7E, 0x80, 0x81,
	0x82, 0x83, 0x84, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x8C, 0x8D, 0x8E, 0x8F,
	0x90, 0x92, 0x93, 0x94, 0x95, 0x96, 0x98, 0x99, 0x9A, 0x9B, 0x9C, 0x9E,
	0x9F, 0xA0, 0xA1, 0xA2, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xAA, 0xAB, 0xAC,
	0xAD, 0xAE, 0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9,
	0xBA, 0xBB, 0xBC, 0xBD, 0xBF, 0xC0, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7,
	0xC9, 0xCA, 0xCB, 0xCC, 0xCD, 0xCF, 0xD0, 0xD1, 0xD2, 0xD3, 0xD5, 0xD6,
};

static uint8_t a3wl_dpi_to_wire(uint16_t dpi)
{
	uint16_t clamped;

	clamped = ALLOY_CLAMP(dpi, A3WL_DPI_MIN, A3WL_DPI_MAX);
	/* snap to the 100 DPI grid the sensor table is built on */
	clamped = (uint16_t)((clamped / A3WL_DPI_STEP) * A3WL_DPI_STEP);
	return a3wl_dpi_table[(clamped - A3WL_DPI_MIN) / A3WL_DPI_STEP];
}

/* 0-100% brightness -> 0-15 illumination level, matching the GG slider */
static uint8_t a3wl_brightness_to_wire(uint8_t pct)
{
	uint8_t p = ALLOY_MIN(pct, 100);

	return (uint8_t)((p * A3WL_BRIGHTNESS_MAX + 50) / 100);
}

/*
 * Packet builders are pure functions over struct alloy_config so the wire
 * format can be unit tested without hardware.
 * Each returns the payload length.
 */
size_t a3wl_build_dpi(const struct alloy_config *cfg, uint8_t *buf);
size_t a3wl_build_polling(const struct alloy_config *cfg, uint8_t *buf);
size_t a3wl_build_zone_color(const struct alloy_config *cfg, int zone,
			     uint8_t *buf);
size_t a3wl_build_rainbow(const struct alloy_config *cfg, uint8_t *buf);
size_t a3wl_build_reactive(const struct alloy_config *cfg, uint8_t *buf);
size_t a3wl_build_startup(const struct alloy_config *cfg, uint8_t *buf);
size_t a3wl_build_brightness(const struct alloy_config *cfg, uint8_t *buf);
size_t a3wl_build_buttons(const struct alloy_config *cfg, uint8_t *buf);
size_t a3wl_build_high_efficiency(const struct alloy_config *cfg, uint8_t *buf);
size_t a3wl_build_sleep(const struct alloy_config *cfg, uint8_t *buf);
int a3wl_parse_event(const uint8_t *buf, size_t len, struct alloy_config *cfg);

size_t a3wl_build_dpi(const struct alloy_config *cfg, uint8_t *buf)
{
	size_t n = 0;
	uint8_t i;

	buf[n++] = A3WL_CMD_DPI;
	buf[n++] = cfg->dpi_count;
	/*
	 * active index is 0-based on the wire, matching the 0xAD level event
	 * (a3wl_parse_event) the receiver reports back
	 */
	buf[n++] = cfg->dpi_active;
	/*
	 * one byte per preset (TrueMove Air single-value CPI);
	 * sensor has no independent X/Y axis here,
	 * so the Y column of cfg->dpi is unused
	 */
	for (i = 0; i < cfg->dpi_count; i++)
		buf[n++] = a3wl_dpi_to_wire(cfg->dpi[i][0]);
	return n;
}

size_t a3wl_build_polling(const struct alloy_config *cfg, uint8_t *buf)
{
	uint8_t wire;

	/* note the encoding differs from the Rival 3 line (1000 Hz is 0x00) */
	switch (cfg->polling_hz) {
	case 125:
		wire = 0x03;
		break;
	case 250:
		wire = 0x02;
		break;
	case 500:
		wire = 0x01;
		break;
	case 1000:
	default:
		wire = 0x00;
		break;
	}

	buf[0] = A3WL_CMD_POLLING;
	buf[1] = wire;
	return 2;
}

/*
 * One zone per write: 0x61 0x01 <zone> <R> <G> <B>.
 * zone is 0 (strip top), 1 (middle) or 2 (bottom);
 * 0x01 sub-byte is constant across every capture.
 * Writing static color clears the rainbow on that zone.
 */
size_t a3wl_build_zone_color(const struct alloy_config *cfg, int zone,
			     uint8_t *buf)
{
	buf[0] = A3WL_CMD_ZONE_COLOR;
	buf[1] = 0x01;
	buf[2] = (uint8_t)zone;
	buf[3] = cfg->zone_color[zone].r;
	buf[4] = cfg->zone_color[zone].g;
	buf[5] = cfg->zone_color[zone].b;
	return 6;
}

/*
 * Rainbow effect: 0x62 <mask> enrolls the selected zones into the cycle.
 * Returns 0 when no zone runs the rainbow.
 * Like the Rival 3 Gen 2, the mask only works while the rainbow engine
 * (the rainbow byte of 0x67) is on, so 0x67 is sent first in a3wl_apply_colors.
 */
size_t a3wl_build_rainbow(const struct alloy_config *cfg, uint8_t *buf)
{
	uint8_t mask = 0;
	uint8_t i;

	for (i = 0; i < 3; i++) {
		if (cfg->zone_fx[i])
			mask |= (uint8_t)(1u << i);
	}
	if (!mask)
		return 0;

	buf[0] = A3WL_CMD_RAINBOW;
	buf[1] = mask;
	return 2;
}

/*
 * Reactive click color: 0x66 <enable> 0x00 <R> <G> <B>.
 * The enable byte is mandatory; short write latches a black flash.
 * Color survives neither the 0x51 save nor a sleep, so the host re-arms it on apply.
 */
size_t a3wl_build_reactive(const struct alloy_config *cfg, uint8_t *buf)
{
	buf[0] = A3WL_CMD_REACTIVE;
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
 * Default lighting / rainbow master switch: 0x67 <rainbow> <reactive>.
 * The rainbow byte is the live engine switch, so it is derived as "any zone
 * runs the rainbow OR the startup preference wants it", exactly as on the
 * Rival 3 Gen 2.
 */
size_t a3wl_build_startup(const struct alloy_config *cfg, uint8_t *buf)
{
	uint8_t rainbow_zones = 0;
	uint8_t i;

	for (i = 0; i < 3; i++)
		rainbow_zones |= cfg->zone_fx[i];

	buf[0] = A3WL_CMD_STARTUP_FX;
	buf[1] = rainbow_zones != 0 ||
		 cfg->startup_fx == ALLOY_STARTUP_RAINBOW ||
		 cfg->startup_fx == ALLOY_STARTUP_REACTIVE_RAINBOW;
	buf[2] = (cfg->startup_fx == ALLOY_STARTUP_REACTIVE ||
		  cfg->startup_fx == ALLOY_STARTUP_REACTIVE_RAINBOW);
	return 3;
}

/*
 * Unified illumination command: 0x63 <level> 0x01 <smart> 0x00 <dim0 dim1 dim2>.
 * Brightness is the 4-bit level; smart mode (blank the LEDs while moving) and
 * the idle dim timer (3-byte little-endian ms) are the wireless-power fields.
 * All three reset on sleep, so the host re-pushes them on apply.
 */
size_t a3wl_build_brightness(const struct alloy_config *cfg, uint8_t *buf)
{
	uint16_t dim_s = ALLOY_MIN(cfg->illum_dim_s, ALLOY_ILLUM_DIM_MAX);
	uint32_t dim_ms = (uint32_t)dim_s * 1000u;

	buf[0] = A3WL_CMD_ILLUM;
	buf[1] = a3wl_brightness_to_wire(cfg->brightness);
	buf[2] = 0x01; /* apply-illumination flag (constant in GG captures) */
	buf[3] = cfg->illum_smart ? 0x01 : 0x00;
	buf[4] = 0x00;
	buf[5] = (uint8_t)(dim_ms & 0xFF);
	buf[6] = (uint8_t)((dim_ms >> 8) & 0xFF);
	buf[7] = (uint8_t)((dim_ms >> 16) & 0xFF);
	return 8;
}

/*
 * Sleep timer: 0x69 <t0 t1 t2>, idle time before the mouse sleeps as a 3-byte
 * little-endian millisecond count (cfg->sleep_min minutes, 0 = never).
 * Captured: 5 min -> 69 e0 93 04 (0x000493E0 = 300000 ms).
 */
size_t a3wl_build_sleep(const struct alloy_config *cfg, uint8_t *buf)
{
	uint8_t min = ALLOY_MIN(cfg->sleep_min, ALLOY_SLEEP_MAX);
	uint32_t ms = (uint32_t)min * 60000u;

	buf[0] = A3WL_CMD_SLEEP;
	buf[1] = (uint8_t)(ms & 0xFF);
	buf[2] = (uint8_t)((ms >> 8) & 0xFF);
	buf[3] = (uint8_t)((ms >> 16) & 0xFF);
	return 4;
}

/* wire ids of the 5-byte action fields, in config order */
static const uint8_t a3wl_button_wire_id[] = {
	0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x31, 0x32,
};

static uint8_t a3wl_action_first_byte(const struct alloy_action *act)
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

size_t a3wl_build_buttons(const struct alloy_config *cfg, uint8_t *buf)
{
	size_t n = 1;
	uint8_t i;

	buf[0] = A3WL_CMD_BUTTONS;
	memset(buf + 1, 0, 8 * 5);

	for (i = 0; i < ALLOY_ARRAY_SIZE(a3wl_button_wire_id); i++) {
		const struct alloy_action *act = &cfg->buttons[i];
		uint8_t *field = buf + 1 + i * 5;

		field[0] = a3wl_action_first_byte(act);
		if (act->type == ALLOY_ACT_KEYBOARD ||
		    act->type == ALLOY_ACT_MEDIA)
			field[1] = (uint8_t)act->value;
	}
	return n + 8 * 5;
}

/*
 * High-Efficiency Mode enable flag: 0x68 <enable>.
 * This is only the mode's flag byte;
 * Full power-saver behaviour is the register bundle
 * applied in a3wl_apply_high_efficiency.
 */
size_t a3wl_build_high_efficiency(const struct alloy_config *cfg, uint8_t *buf)
{
	buf[0] = A3WL_CMD_HIGHEFF;
	buf[1] = cfg->high_efficiency ? 0x01 : 0x00;
	return 2;
}

/*
 * CPI-level switch notification, captured on hardware (fw 1.3.1):
 *
 *   0xAD <count> <active> <wire1> ... <wireN>
 *
 * Emitted on the event interface every time the active level changes,
 * including physical CPI-button presses. <active> is 0-based and the wire
 * bytes repeat the level table in the 0x6D sensor encoding.
 * Only the active index is taken over; host stays the source of truth for the values.
 *
 * The interface also streams two status events that do not map to struct
 * alloy_config, so they are recognized and ignored here (they carry no
 * value the config owns): the power notification 0xBC <01 wake | 00 sleep>
 * and the unsolicited battery push 0x12 <level>. Link state and charge are
 * surfaced host-side through ops->battery, which the UI polls; the wake edge
 * is also what prompts the host to re-push the non-persistent lighting.
 */
int a3wl_parse_event(const uint8_t *buf, size_t len, struct alloy_config *cfg)
{
	uint8_t active;

	if (len < 3 || buf[0] != A3WL_EVT_CPI_LEVEL)
		return 0;
	active = buf[2];
	if (buf[1] < 1 || buf[1] > ALLOY_MAX_DPI_PRESETS || active >= buf[1])
		return 0;
	if (active >= cfg->dpi_count || active == cfg->dpi_active)
		return 0;
	cfg->dpi_active = active;
	return 1;
}

static int a3wl_apply_dpi(struct alloy_device *dev,
			  const struct alloy_config *cfg)
{
	uint8_t buf[ALLOY_HID_REPORT_SIZE];

	return alloy_hid_cmd(&dev->hid, buf, a3wl_build_dpi(cfg, buf));
}

static int a3wl_apply_polling(struct alloy_device *dev,
			      const struct alloy_config *cfg)
{
	uint8_t buf[ALLOY_HID_REPORT_SIZE];

	return alloy_hid_cmd(&dev->hid, buf, a3wl_build_polling(cfg, buf));
}

/*
 * Full lighting state, in engine order (verified on hardware):
 * 0x67 first so the rainbow engine is live, then the 0x62 mask enrolls the
 * rainbow zones, the per-zone 0x61 statics carve their zones out of the cycle,
 * and the reactive overlay closes the batch.
 */
static int a3wl_apply_colors(struct alloy_device *dev,
			     const struct alloy_config *cfg)
{
	uint8_t buf[ALLOY_HID_REPORT_SIZE];
	size_t len;
	int ret = 0;
	int zone;

	ret |= alloy_hid_cmd(&dev->hid, buf, a3wl_build_startup(cfg, buf));

	len = a3wl_build_rainbow(cfg, buf);
	if (len)
		ret |= alloy_hid_cmd(&dev->hid, buf, len);

	for (zone = 0; zone < 3; zone++) {
		if (cfg->zone_fx[zone])
			continue; /* zone on the rainbow, leave it cycling */
		ret |= alloy_hid_cmd(&dev->hid, buf,
				     a3wl_build_zone_color(cfg, zone, buf));
	}

	ret |= alloy_hid_cmd(&dev->hid, buf, a3wl_build_reactive(cfg, buf));

	return ret ? -1 : 0;
}

static int a3wl_apply_brightness(struct alloy_device *dev,
				 const struct alloy_config *cfg)
{
	uint8_t buf[ALLOY_HID_REPORT_SIZE];

	return alloy_hid_cmd(&dev->hid, buf, a3wl_build_brightness(cfg, buf));
}

static int a3wl_apply_buttons(struct alloy_device *dev,
			      const struct alloy_config *cfg)
{
	uint8_t buf[ALLOY_HID_REPORT_SIZE];

	return alloy_hid_cmd(&dev->hid, buf, a3wl_build_buttons(cfg, buf));
}

/*
 * High-Efficiency Mode is not a single opcode but the register bundle GG emits,
 * mirrored here byte-for-byte from the hardware capture:
 * enabling sets the 0x68 flag, forces polling to 125 Hz (0x6B 0x03) and blanks
 * the LEDs (0x63 level 0);
 * Disabling clears the flag and restores the user's polling and brightness
 * straight from cfg.
 * That is how the firmware implements the mode - the flag alone changes nothing,
 * the host drives the saver.
 */
static int a3wl_apply_high_efficiency(struct alloy_device *dev,
				      const struct alloy_config *cfg)
{
	uint8_t buf[ALLOY_HID_REPORT_SIZE];
	struct alloy_config eff = *cfg;
	int ret = 0;

	if (cfg->high_efficiency) {
		eff.polling_hz = A3WL_HIGHEFF_POLLING_HZ;
		eff.brightness = A3WL_HIGHEFF_BRIGHTNESS;
	}

	ret |= alloy_hid_cmd(&dev->hid, buf,
			     a3wl_build_high_efficiency(cfg, buf));
	ret |= alloy_hid_cmd(&dev->hid, buf, a3wl_build_polling(&eff, buf));
	ret |= alloy_hid_cmd(&dev->hid, buf, a3wl_build_brightness(&eff, buf));

	return ret ? -1 : 0;
}

static int a3wl_apply_sleep(struct alloy_device *dev,
			    const struct alloy_config *cfg)
{
	uint8_t buf[ALLOY_HID_REPORT_SIZE];

	return alloy_hid_cmd(&dev->hid, buf, a3wl_build_sleep(cfg, buf));
}

static int a3wl_save(struct alloy_device *dev)
{
	static const uint8_t cmd[] = { A3WL_CMD_SAVE, 0x00 };

	return alloy_hid_cmd(&dev->hid, cmd, sizeof(cmd));
}

static int a3wl_firmware_version(struct alloy_device *dev, char *buf,
				 size_t len)
{
	static const uint8_t cmd[] = { A3WL_CMD_FIRMWARE };
	uint8_t resp[ALLOY_HID_REPORT_SIZE];
	int n;
	size_t i;

	if (!len)
		return -1;

	n = alloy_hid_cmd_read(&dev->hid, cmd, sizeof(cmd), resp, sizeof(resp));
	if (n < 1)
		return -1;

	/*
	 * response is bare ASCII string from byte 0 (no command echo),
	 * e.g. "1.3.1", followed by NUL and build id
	 */
	for (i = 0;
	     i < (size_t)n && i < len - 1 && resp[i] >= 0x20 && resp[i] < 0x7F;
	     i++)
		buf[i] = (char)resp[i];
	buf[i] = '\0';
	return i ? 0 : -1;
}

static int a3wl_battery(struct alloy_device *dev, int *percent, int *charging)
{
	static const uint8_t cmd[] = { A3WL_CMD_BATTERY };
	uint8_t resp[ALLOY_HID_REPORT_SIZE];
	int n;
	uint8_t b;
	int lvl;

	/*
	 * match the 0xD2 echo explicitly and keep the retry budget light:
	 * this is background poll, so sleeping mouse that never answers must not
	 * stall the render loop
	 * few wake attempts recover merely-idle link; unlinked receiver only ever
	 * emits the 0x40 0xFF idle marker, which hid_read_matching skips,
	 * so the call returns "no reading" instead of bogus level
	 */
	n = alloy_hid_cmd_read_want(&dev->hid, cmd, sizeof(cmd),
				    A3WL_CMD_BATTERY, resp, sizeof(resp),
				    ALLOY_HID_ATTEMPTS_POLL);
	if (n < 2 || resp[0] != A3WL_CMD_BATTERY)
		return -1;

	b = resp[1];
	if (charging)
		*charging = (b & A3WL_BAT_CHARGING) ? 1 : 0;
	if (percent) {
		lvl = (int)((b & (uint8_t)~A3WL_BAT_CHARGING) - 1) * 5;
		*percent = ALLOY_CLAMP(lvl, 0, 100);
	}
	return 0;
}

/*
 * Dongle pairing.
 * Binding fresh mouse (mouse OFF, hold CPI, flick to 2.4 GHz) only completes while
 * GG is running, which means GG puts the *receiver* into a bind/listen mode over USB
 * - the mouse-side gesture alone is not enough.
 * That USB opcode has not been captured yet
 * (see the pairing entry under "Open questions" in
 * Documentation/protocol/steelseries-aerox3-wireless.rst),
 * so this reports the gap honestly instead of pretending to pair.
 *
 * TODO: once the bind command is reverse-engineered, send it here (with the
 * usual alloy_hid_cmd wake-retry) and return 0 / negative; then drop the
 * ALLOY_PAIR_UNIMPLEMENTED path and update the driver test.
 */
static int a3wl_pair(struct alloy_device *dev)
{
	(void)dev;
	return ALLOY_PAIR_UNIMPLEMENTED;
}

static const uint16_t a3wl_polling_rates[] = { 1000, 500, 250, 125 };

/*
 * Per-zone effect list.
 * index 1 maps to the 0x62 rainbow mask, everything else is steady.
 */
static const char *const a3wl_fx_names[] = { "STEADY", "RAINBOW" };

static const struct alloy_led_zone a3wl_zones[] = {
	{ .name = "TOP", .def_color = { 0xFF, 0x00, 0x00 } },
	{ .name = "MIDDLE", .def_color = { 0x00, 0xFF, 0x00 } },
	{ .name = "BOTTOM", .def_color = { 0x00, 0x00, 0xFF } },
};

static const struct alloy_button a3wl_buttons[] = {
	{ "Button 1 (Left)", { ALLOY_ACT_MOUSE, 1 } },
	{ "Button 2 (Right)", { ALLOY_ACT_MOUSE, 2 } },
	{ "Button 3 (Middle)", { ALLOY_ACT_MOUSE, 3 } },
	{ "Button 4 (Back)", { ALLOY_ACT_MOUSE, 4 } },
	{ "Button 5 (Forward)", { ALLOY_ACT_MOUSE, 5 } },
	{ "Button 6 (CPI)", { ALLOY_ACT_DPI_CYCLE, 0 } },
	{ "Scroll Up", { ALLOY_ACT_SCROLL_UP, 0 } },
	{ "Scroll Down", { ALLOY_ACT_SCROLL_DOWN, 0 } },
};

static const struct alloy_driver_ops a3wl_ops = {
	.apply_dpi = a3wl_apply_dpi,
	.apply_polling = a3wl_apply_polling,
	.apply_colors = a3wl_apply_colors,
	.apply_brightness = a3wl_apply_brightness,
	.apply_buttons = a3wl_apply_buttons,
	.apply_high_efficiency = a3wl_apply_high_efficiency,
	.apply_sleep = a3wl_apply_sleep,
	.save = a3wl_save,
	.firmware_version = a3wl_firmware_version,
	.battery = a3wl_battery,
	.pair = a3wl_pair,
	.parse_event = a3wl_parse_event,
};

static const struct alloy_driver steelseries_aerox3_wireless = {
	.name = "SteelSeries Aerox 3 Wireless",
	.vendor_id = 0x1038,
	.product_id = 0x1838,
	.interface = 3,
	.event_interface = 4,
	.bt_product_id = 0x183A, /* product id the mouse shows over Bluetooth */
	.dpi = {
		.min = A3WL_DPI_MIN,
		.max = A3WL_DPI_MAX,
		.step = A3WL_DPI_STEP,
		.max_presets = 5,
	},
	.polling_rates = a3wl_polling_rates,
	.num_polling_rates = ALLOY_ARRAY_SIZE(a3wl_polling_rates),
	.zones = a3wl_zones,
	.num_zones = ALLOY_ARRAY_SIZE(a3wl_zones),
	.buttons = a3wl_buttons,
	.num_buttons = ALLOY_ARRAY_SIZE(a3wl_buttons),
	.caps = ALLOY_CAP_BRIGHTNESS | ALLOY_CAP_FIRMWARE_VERSION |
		ALLOY_CAP_BATTERY | ALLOY_CAP_HIGH_EFFICIENCY |
		ALLOY_CAP_PAIRING | ALLOY_CAP_FX_RAINBOW |
		ALLOY_CAP_FX_REACTIVE | ALLOY_CAP_FX_STARTUP,
	.fx_names = a3wl_fx_names,
	.num_fx = ALLOY_ARRAY_SIZE(a3wl_fx_names),
	.ascii_art = alloy_art_steelseries_aerox3_wireless,
	.ops = &a3wl_ops,
	.config_defaults = alloy_config_generic_defaults,
};

ALLOY_DRIVER_REGISTER(steelseries_aerox3_wireless);
