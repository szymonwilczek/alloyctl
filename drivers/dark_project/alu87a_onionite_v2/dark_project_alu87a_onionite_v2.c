/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Dark Project ALU87A Onionite V2 (wired TKL mechanical keyboard), USB ID 342d:e40f.
 *
 * Protocol notes live in Documentation/protocol/dark-project-alu87a-onionite-v2.rst.
 * Maintainer: Szymon Wilczek <swilczek.lx@gmail.com>
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "art_dark_project_alu87a_onionite_v2.h"
#include "dark_project/darkproject_common.h"
#include "hid.h"
#include "lib/keyboard.h"

#define DARKPROJECT_CONFIG_IFACE 2

static const char *const darkproject_fx_names[] = {
	"SOLID",   "WAVE",	  "COLOR CYCLE", "BREATHING", "RIVER",
	"RAIN",	   "SPIRAL WAVE", "STAR",	 "RANDOM",    "COLOR DISCHARGE",
	"TRIGGER", "RIPPLES",	  "SINE WAVE",	 "FOOTPRINT", "CUSTOM",
};

#define ALU87A_KEY_COUNT 96

static const uint8_t alu87a_hid_effects[] = {
	5, /* SOLID */
	0, /* WAVE */
	1, /* COLOR CYCLE */
	3, /* BREATHING */
	8, /* RIVER */
	13, /* RAIN */
	6, /* SPIRAL WAVE */
	7, /* STAR */
	2, /* RANDOM */
	11, /* COLOR DISCHARGE */
	10, /* TRIGGER */
	9, /* RIPPLES */
	12, /* SINE WAVE */
	4, /* FOOTPRINT */
	14, /* CUSTOM */
};

static void alu87a_generate_custom_zone_matrix(uint8_t custom_index, uint8_t r,
					       uint8_t g, uint8_t b,
					       uint8_t *planar_rgb)
{
	/* Zone 0: WSAD + Arrows */
	static const uint8_t zone_wsad_arrows[] = {
		14, /* KeyW */
		9, /* KeyA */
		15, /* KeyS */
		21, /* KeyD */
		76, /* ArrowUp */
		95, /* ArrowDown */
		89, /* ArrowLeft */
		82, /* ArrowRight */
	};

	/* Zone 1: Letters A-Z */
	static const uint8_t zone_letters[] = { 9,  34, 22, 21, 20, 27, 33,
						39, 50, 45, 51, 57, 46, 40,
						56, 62, 8,  26, 15, 32, 44,
						28, 14, 16, 38, 10 };

	/* Zone 2: Digits 0-9 & Minus, Equal, Backquote, Backspace */
	static const uint8_t zone_digits[] = { 1,  7,  13, 19, 25, 31, 37,
					       43, 49, 55, 61, 67, 73, 79 };

	/* Zone 3: Function Keys & Navigation */
	static const uint8_t zone_fn_nav[] = { 0,  6,  12, 18, 24, 30, 36, 42,
					       48, 54, 60, 66, 72, 78, 84, 90,
					       85, 86, 91, 92, 87, 93 };

	const uint8_t *keys = NULL;
	size_t count = 0;

	memset(planar_rgb, 0, 512);

	switch (custom_index) {
	case 0: /* Cust1 */
		keys = zone_wsad_arrows;
		count = ALLOY_ARRAY_SIZE(zone_wsad_arrows);
		break;
	case 1: /* Cust2 */
		keys = zone_letters;
		count = ALLOY_ARRAY_SIZE(zone_letters);
		break;
	case 2: /* Cust3 */
		keys = zone_digits;
		count = ALLOY_ARRAY_SIZE(zone_digits);
		break;
	case 3: /* Cust4 */
		keys = zone_fn_nav;
		count = ALLOY_ARRAY_SIZE(zone_fn_nav);
		break;
	case 4: /* Cust5 (All keys) */
	default:
		for (size_t i = 0; i < ALU87A_KEY_COUNT; i++) {
			/* skip empty matrix positions */
			if (i == 35 || i == 41 || i == 47 || i == 53 ||
			    i == 59 || i == 71 || i == 77 || i == 80 ||
			    i == 88 || i == 94)
				continue;
			planar_rgb[i] = r;
			planar_rgb[96 + i] = g;
			planar_rgb[192 + i] = b;
		}
		return;
	}

	for (size_t i = 0; i < count; i++) {
		uint8_t k = keys[i];
		if (k < ALU87A_KEY_COUNT) {
			planar_rgb[k] = r;
			planar_rgb[96 + k] = g;
			planar_rgb[192 + k] = b;
		}
	}
}

static const uint16_t darkproject_polling_rates[] = { 1000, 500, 250, 125 };

static const struct alloy_led_zone darkproject_zones[] = {
	{ .name = "BACKLIGHT", .def_color = { 0x00, 0xff, 0x80 } },
};

/* Pure packet builders for unit testing */
size_t alu87a_build_lighting(const uint8_t *base_profile, uint8_t hid_effect,
			     uint8_t brightness_pct, uint8_t speed_level,
			     uint8_t multicolor, uint8_t direction,
			     uint8_t custom_index, uint8_t r, uint8_t g,
			     uint8_t b, uint8_t *buf);
size_t alu87a_build_polling(uint16_t polling_hz, const uint8_t *base_profile,
			    uint8_t *buf);
int alu87a_parse_profile_data(const uint8_t *resp, size_t len,
			      struct alloy_config *cfg);
int alu87a_parse_event(struct alloy_device *dev, const uint8_t *buf, size_t len,
		       struct alloy_config *cfg);

size_t alu87a_build_lighting(const uint8_t *base_profile, uint8_t hid_effect,
			     uint8_t brightness_pct, uint8_t speed_level,
			     uint8_t multicolor, uint8_t direction,
			     uint8_t custom_index, uint8_t r, uint8_t g,
			     uint8_t b, uint8_t *buf)
{
	uint8_t bright_val;
	uint8_t speed_val;

	memset(buf, 0, DARKPROJECT_REPORT_SIZE);
	if (base_profile)
		memcpy(buf + 1, base_profile + 1, DARKPROJECT_REPORT_SIZE - 1);

	buf[0] = DARKPROJECT_REPORT_ID;
	buf[1] = DARKPROJECT_CMD_WRITE_PROFILE;

	/* Brightness: 0..100% -> 0..4 (0%, 25%, 50%, 75%, 100%) */
	bright_val = (uint8_t)((brightness_pct * 4 + 50) / 100);
	if (bright_val > 4)
		bright_val = 4;

	/*
	 * Speed on hardware MCU:
	 * 1 is FASTEST, 5 is SLOWEST.
	 * speed_level from alloyctl is 1 (Slow) .. 5 (Fast).
	 * Map 1->5, 2->4, 3->3, 4->2, 5->1.
	 */
	if (speed_level < 1)
		speed_level = 3;
	if (speed_level > 5)
		speed_level = 5;
	speed_val = (uint8_t)(6 - speed_level);

	buf[1 + 8] = hid_effect;
	buf[1 + 9 + hid_effect] = bright_val;
	buf[1 + 23 + hid_effect] = speed_val;
	buf[1 + 37 + hid_effect] = (hid_effect == 5 || !multicolor) ? 0 : 8;

	/* Direction/Angle for Wave (byte 51) and Spiral Wave (byte 83) */
	if (hid_effect == 0)
		buf[1 + 51] = direction & 0x03;
	else if (hid_effect == 6)
		buf[1 + 83] = direction & 0x01;

	/* Custom zone preset index (Cust1=1, Cust2=2, etc) at byte 52 */
	if (hid_effect == 14)
		buf[1 + 52] =
			(uint8_t)(custom_index < 5 ? custom_index + 1 : 1);
	else
		buf[1 + 52] = 0;

	/* Static RGB color at payload offsets 58, 66, 74 */
	buf[1 + 58] = r;
	buf[1 + 66] = g;
	buf[1 + 74] = b;

	return DARKPROJECT_REPORT_SIZE;
}

size_t alu87a_build_polling(uint16_t polling_hz, const uint8_t *base_profile,
			    uint8_t *buf)
{
	uint8_t poll_idx = 4; /* default 1000 Hz */

	memset(buf, 0, DARKPROJECT_REPORT_SIZE);
	if (base_profile)
		memcpy(buf + 1, base_profile + 1, DARKPROJECT_REPORT_SIZE - 1);

	buf[0] = DARKPROJECT_REPORT_ID;
	buf[1] = DARKPROJECT_CMD_WRITE_PROFILE;

	switch (polling_hz) {
	case 125:
		poll_idx = 1;
		break;
	case 250:
		poll_idx = 2;
		break;
	case 500:
		poll_idx = 3;
		break;
	case 1000:
	default:
		poll_idx = 4;
		break;
	}

	buf[1 + 7] = poll_idx;
	return DARKPROJECT_REPORT_SIZE;
}

int alu87a_parse_profile_data(const uint8_t *resp, size_t len,
			      struct alloy_config *cfg)
{
	struct alloy_devcfg *d;
	uint8_t pidx;
	uint8_t hid_effect;
	uint8_t effect_idx = 0;

	if (!resp || len < 76 || !cfg)
		return -1;
	d = alloy_devcfg(cfg);

	/* Polling rate at byte 8: 1:125Hz, 2:250Hz, 3:500Hz, 4:1000Hz */
	pidx = resp[8];
	switch (pidx) {
	case 1:
		d->polling_hz = 125;
		break;
	case 2:
		d->polling_hz = 250;
		break;
	case 3:
		d->polling_hz = 500;
		break;
	case 4:
	default:
		d->polling_hz = 1000;
		break;
	}

	/* Effect at byte 9 (HidEffectID 0..14) mapped to ALU87A effect index */
	hid_effect = resp[9];
	for (size_t i = 0; i < ALLOY_ARRAY_SIZE(alu87a_hid_effects); i++) {
		if (alu87a_hid_effects[i] == hid_effect) {
			effect_idx = (uint8_t)i;
			break;
		}
	}
	d->zone_fx[0] = effect_idx;

	/* Brightness at byte 10 + hid_effect (0..4) */
	if ((size_t)(10 + hid_effect) < len) {
		uint8_t b = resp[10 + hid_effect];
		d->brightness = (uint8_t)ALLOY_CLAMP(b * 25, 0, 100);
	}

	/* Speed at byte 24 + hid_effect (1=Fastest, 5=Slowest -> 1..5) */
	if ((size_t)(24 + hid_effect) < len) {
		uint8_t s = resp[24 + hid_effect];
		if (s >= 1 && s <= 5)
			d->zone_fx_param[0][ALLOY_FX_P_SPEED] =
				(uint8_t)(6 - s);
		else
			d->zone_fx_param[0][ALLOY_FX_P_SPEED] = 3;
	}

	/* Multicolor at byte 38 + hid_effect (8=multi, 0=single) */
	if ((size_t)(38 + hid_effect) < len)
		d->zone_fx_param[0][ALLOY_FX_P_MULTICOLOR] =
			(resp[38 + hid_effect] == 8) ? 1 : 0;

	/* Direction / Angle for Wave (52) and Spiral Wave (84) */
	if (hid_effect == 0 && 52 < len)
		d->zone_fx_param[0][ALLOY_FX_P_DIRECTION] = resp[52] & 0x03;
	else if (hid_effect == 6 && 84 < len)
		d->zone_fx_param[0][ALLOY_FX_P_DIRECTION] = resp[84] & 0x01;

	/* Custom zone index at byte 53 (1..5 -> 0..4) */
	if (53 < len) {
		uint8_t cz = resp[53];
		if (cz >= 1 && cz <= 5)
			d->zone_fx_param[0][ALLOY_FX_P_CUSTOM] =
				(uint8_t)(cz - 1);
		else
			d->zone_fx_param[0][ALLOY_FX_P_CUSTOM] = 0;
	}

	/* Static RGB colors at payload offsets 59, 67, 75 */
	if (75 < len) {
		d->zone_color[0].r = resp[59];
		d->zone_color[0].g = resp[67];
		d->zone_color[0].b = resp[75];
	}

	return 0;
}

int alu87a_parse_event(struct alloy_device *dev, const uint8_t *buf, size_t len,
		       struct alloy_config *cfg);

int alu87a_parse_event(struct alloy_device *dev, const uint8_t *buf, size_t len,
		       struct alloy_config *cfg);

static int alu87a_read_config(struct alloy_device *dev,
			      struct alloy_config *cfg)
{
	uint8_t req[DARKPROJECT_REPORT_SIZE];
	uint8_t resp[DARKPROJECT_REPORT_SIZE];
	uint8_t profile = 1;

	if (!dev || !cfg)
		return -1;

	/* Query active profile (Cmd 0x81) */
	darkproject_build_read_current_profile(req);
	memset(resp, 0, sizeof(resp));
	resp[0] = DARKPROJECT_REPORT_ID;
	if (alloy_dev_send_feature(dev, req, sizeof(req)) == 0) {
		if (alloy_dev_get_feature(dev, resp, sizeof(resp)) == 0) {
			if (resp[2] >= 1 && resp[2] <= 3)
				profile = resp[2];
		}
	}
	alloy_kbd_cfg(cfg)->profile_active = profile;

	/* Query Snap Tap state for active profile (Cmd 0x89) */
	darkproject_build_read_snap_tap(profile, req);
	memset(resp, 0, sizeof(resp));
	resp[0] = DARKPROJECT_REPORT_ID;
	if (alloy_dev_send_feature(dev, req, sizeof(req)) == 0) {
		if (alloy_dev_get_feature(dev, resp, sizeof(resp)) == 0)
			darkproject_parse_snap_tap_data(resp, sizeof(resp),
							cfg);
	}

	/* Query Profile lighting & performance data for active profile (Cmd 0x82) */
	darkproject_build_read_profile(profile - 1, req);
	memset(resp, 0, sizeof(resp));
	resp[0] = DARKPROJECT_REPORT_ID;
	if (alloy_dev_send_feature(dev, req, sizeof(req)) == 0) {
		if (alloy_dev_get_feature(dev, resp, sizeof(resp)) == 0)
			alu87a_parse_profile_data(resp, sizeof(resp), cfg);
	}

	/* Synchronize global FX across zones */
	for (uint8_t i = 1; i < ALLOY_MAX_LED_ZONES; i++) {
		alloy_devcfg(cfg)->zone_color[i] =
			alloy_devcfg(cfg)->zone_color[0];
		alloy_devcfg(cfg)->zone_fx[i] = alloy_devcfg(cfg)->zone_fx[0];
		alloy_devcfg(cfg)->zone_fx_param[i][ALLOY_FX_P_SPEED] =
			alloy_devcfg(cfg)->zone_fx_param[0][ALLOY_FX_P_SPEED];
		alloy_devcfg(cfg)->zone_fx_param[i][ALLOY_FX_P_MULTICOLOR] =
			alloy_devcfg(cfg)
				->zone_fx_param[0][ALLOY_FX_P_MULTICOLOR];
		alloy_devcfg(cfg)->zone_fx_param[i][ALLOY_FX_P_DIRECTION] =
			alloy_devcfg(cfg)
				->zone_fx_param[0][ALLOY_FX_P_DIRECTION];
		alloy_devcfg(cfg)->zone_fx_param[i][ALLOY_FX_P_CUSTOM] =
			alloy_devcfg(cfg)->zone_fx_param[0][ALLOY_FX_P_CUSTOM];
	}

	return 0;
}

int alu87a_parse_event(struct alloy_device *dev, const uint8_t *buf, size_t len,
		       struct alloy_config *cfg)
{
	(void)dev;
	if (!buf || len < 2 || !cfg)
		return 0;

	/*
	 * Hardware notifications on Interface 1 (Usage Page 0xFF00, Report ID 0x04):
	 * - [0x04, 0xF0, ..., status at byte 6] / [0xF0, ..., status at byte 5]
	 * - [0x04, 0xF1, profile at byte 2] / [0xF1, profile at byte 1]
	 * - [0x05, 0xA4, 0xF0] / [0x05, 0xA4, 0xF1] (firmware variants)
	 */
	for (size_t i = 0; i + 1 < len; i++) {
		if (buf[i] == 0xf0) {
			if (i + 5 < len) {
				uint8_t st = buf[i + 5] ? 1 : 0;
				if (alloy_kbd_cfg(cfg)->snap_tap != st) {
					alloy_kbd_cfg(cfg)->snap_tap = st;
					return 1;
				}
			}
		} else if (buf[i] == 0xf1) {
			if (i + 1 < len) {
				uint8_t prof = buf[i + 1];
				if (prof >= 1 && prof <= 3 &&
				    alloy_kbd_cfg(cfg)->profile_active !=
					    prof) {
					alloy_kbd_cfg(cfg)->profile_active =
						prof;
					return 1;
				}
			}
		} else if (buf[i] == 0xa4 && i + 1 < len) {
			if (buf[i + 1] == 0xf0 && i + 6 < len) {
				uint8_t st = buf[i + 6] ? 1 : 0;
				if (alloy_kbd_cfg(cfg)->snap_tap != st) {
					alloy_kbd_cfg(cfg)->snap_tap = st;
					return 1;
				}
			} else if (buf[i + 1] == 0xf1 && i + 2 < len) {
				uint8_t prof = buf[i + 2];
				if (prof >= 1 && prof <= 3 &&
				    alloy_kbd_cfg(cfg)->profile_active !=
					    prof) {
					alloy_kbd_cfg(cfg)->profile_active =
						prof;
					return 1;
				}
			}
		}
	}
	return 0;
}

static int darkproject_apply_colors(struct alloy_device *dev,
				    const struct alloy_config *cfg)
{
	uint8_t read_cmd[DARKPROJECT_REPORT_SIZE];
	uint8_t base_profile[DARKPROJECT_REPORT_SIZE];
	uint8_t buf[DARKPROJECT_REPORT_SIZE];
	size_t len;
	uint8_t r = 0, g = 0, b = 0;
	uint8_t hid_effect = 0;
	uint8_t speed_level = 3;
	uint8_t brightness_pct = 100;
	uint8_t multicolor = 1;
	uint8_t direction = 0;
	uint8_t custom_index = 0;
	uint8_t profile = alloy_kbd_cfg_c(cfg)->profile_active ?
				  alloy_kbd_cfg_c(cfg)->profile_active :
				  1;
	int ret;

	/* Query current active profile state as baseline */
	darkproject_build_read_profile(profile - 1, read_cmd);
	memset(base_profile, 0, sizeof(base_profile));
	base_profile[0] = DARKPROJECT_REPORT_ID;
	alloy_dev_send_feature(dev, read_cmd, sizeof(read_cmd));
	alloy_dev_get_feature(dev, base_profile, sizeof(base_profile));

	if (alloy_devcfg_c(cfg)->zone_fx[0] <
	    ALLOY_ARRAY_SIZE(alu87a_hid_effects))
		hid_effect =
			alu87a_hid_effects[alloy_devcfg_c(cfg)->zone_fx[0]];

	brightness_pct = alloy_devcfg_c(cfg)->brightness;
	if (alloy_devcfg_c(cfg)->zone_fx_param[0][ALLOY_FX_P_SPEED] >= 1 &&
	    alloy_devcfg_c(cfg)->zone_fx_param[0][ALLOY_FX_P_SPEED] <= 5)
		speed_level =
			alloy_devcfg_c(cfg)->zone_fx_param[0][ALLOY_FX_P_SPEED];

	multicolor =
		alloy_devcfg_c(cfg)->zone_fx_param[0][ALLOY_FX_P_MULTICOLOR];
	direction = alloy_devcfg_c(cfg)->zone_fx_param[0][ALLOY_FX_P_DIRECTION];
	custom_index = alloy_devcfg_c(cfg)->zone_fx_param[0][ALLOY_FX_P_CUSTOM];

	r = alloy_devcfg_c(cfg)->zone_color[0].r;
	g = alloy_devcfg_c(cfg)->zone_color[0].g;
	b = alloy_devcfg_c(cfg)->zone_color[0].b;

	len = alu87a_build_lighting(base_profile, hid_effect, brightness_pct,
				    speed_level, multicolor, direction,
				    custom_index, r, g, b, buf);
	ret = alloy_dev_send_feature(dev, buf, len);
	if (ret < 0)
		return ret;

	/* if custom lighting is selected, send per-key LED color matrix chunks (Cmd 0x0A) */
	if (hid_effect == 14) {
		uint8_t planar_rgb[512];
		alu87a_generate_custom_zone_matrix(custom_index, r, g, b,
						   planar_rgb);

		usleep(20000);
		/* chunk 0 (chunk index 1 on wire) */
		len = darkproject_build_custom_lighting_chunk(
			profile, custom_index, 0, planar_rgb, buf);
		alloy_dev_send_feature(dev, buf, len);

		usleep(20000);
		/* chunk 1 (chunk index 2 on wire) */
		len = darkproject_build_custom_lighting_chunk(
			profile, custom_index, 1, planar_rgb, buf);
		alloy_dev_send_feature(dev, buf, len);
	}

	return 0;
}

static int darkproject_apply_brightness(struct alloy_device *dev,
					const struct alloy_config *cfg)
{
	return darkproject_apply_colors(dev, cfg);
}

static int darkproject_apply_polling(struct alloy_device *dev,
				     const struct alloy_config *cfg)
{
	uint8_t read_cmd[DARKPROJECT_REPORT_SIZE];
	uint8_t base_profile[DARKPROJECT_REPORT_SIZE];
	uint8_t buf[DARKPROJECT_REPORT_SIZE];
	size_t len;

	darkproject_build_read_profile(0, read_cmd);
	memset(base_profile, 0, sizeof(base_profile));
	base_profile[0] = DARKPROJECT_REPORT_ID;
	alloy_dev_send_feature(dev, read_cmd, sizeof(read_cmd));
	alloy_dev_get_feature(dev, base_profile, sizeof(base_profile));

	len = alu87a_build_polling(alloy_devcfg_c(cfg)->polling_hz,
				   base_profile, buf);
	return alloy_dev_send_feature(dev, buf, len);
}

static int darkproject_apply_snap_tap(struct alloy_device *dev,
				      const struct alloy_config *cfg)
{
	uint8_t buf[DARKPROJECT_REPORT_SIZE];
	size_t len;

	len = darkproject_build_snap_tap(
		alloy_kbd_cfg_c(cfg)->profile_active,
		alloy_kbd_cfg_c(cfg)->snap_tap,
		alloy_kbd_cfg_c(cfg)->snap_tap_group_count,
		alloy_kbd_cfg_c(cfg)->snap_tap_groups, buf);
	return alloy_dev_send_feature(dev, buf, len);
}

static int darkproject_apply_profile(struct alloy_device *dev,
				     const struct alloy_config *cfg)
{
	uint8_t buf[DARKPROJECT_REPORT_SIZE];
	size_t len;

	len = darkproject_build_switch_profile(
		alloy_kbd_cfg_c(cfg)->profile_active ?
			alloy_kbd_cfg_c(cfg)->profile_active - 1 :
			0,
		buf);
	return alloy_dev_send_feature(dev, buf, len);
}

static int darkproject_save(struct alloy_device *dev)
{
	(void)dev;
	/* profile writes are committed to flash directly */
	return 0;
}

static int parse_multicolor(const struct alloy_driver *drv, const char *arg,
			    struct alloy_config *cfg, char *err, size_t err_len)
{
	(void)drv;
	if (!arg || !*arg || !strcasecmp(arg, "on") || !strcasecmp(arg, "1") ||
	    !strcasecmp(arg, "true")) {
		alloy_devcfg(cfg)->zone_fx_param[0][ALLOY_FX_P_MULTICOLOR] = 1;
		return 0;
	}
	if (!strcasecmp(arg, "off") || !strcasecmp(arg, "0") ||
	    !strcasecmp(arg, "false")) {
		alloy_devcfg(cfg)->zone_fx_param[0][ALLOY_FX_P_MULTICOLOR] = 0;
		return 0;
	}
	snprintf(err, err_len,
		 "invalid multicolor state '%s' (expected on/off)", arg);
	return -1;
}

static int parse_direction(const struct alloy_driver *drv, const char *arg,
			   struct alloy_config *cfg, char *err, size_t err_len)
{
	(void)drv;
	if (!arg || !*arg) {
		snprintf(err, err_len, "missing direction argument");
		return -1;
	}
	if (!strcasecmp(arg, "right") || !strcmp(arg, "0"))
		alloy_devcfg(cfg)->zone_fx_param[0][ALLOY_FX_P_DIRECTION] = 0;
	else if (!strcasecmp(arg, "left") || !strcmp(arg, "1"))
		alloy_devcfg(cfg)->zone_fx_param[0][ALLOY_FX_P_DIRECTION] = 1;
	else if (!strcasecmp(arg, "down") || !strcmp(arg, "2"))
		alloy_devcfg(cfg)->zone_fx_param[0][ALLOY_FX_P_DIRECTION] = 2;
	else if (!strcasecmp(arg, "up") || !strcmp(arg, "3"))
		alloy_devcfg(cfg)->zone_fx_param[0][ALLOY_FX_P_DIRECTION] = 3;
	else {
		snprintf(
			err, err_len,
			"invalid direction '%s' (expected 0-3, right/left/down/up)",
			arg);
		return -1;
	}
	return 0;
}

static int parse_custom_zone(const struct alloy_driver *drv, const char *arg,
			     struct alloy_config *cfg, char *err,
			     size_t err_len)
{
	int val;

	(void)drv;
	if (!arg || !*arg) {
		snprintf(err, err_len, "missing custom zone index");
		return -1;
	}
	val = atoi(arg);
	if (val >= 1 && val <= 5) {
		alloy_devcfg(cfg)->zone_fx_param[0][ALLOY_FX_P_CUSTOM] =
			(uint8_t)(val - 1);
		return 0;
	}
	snprintf(err, err_len, "invalid custom zone '%s' (expected 1-5)", arg);
	return -1;
}

static const struct alloy_cli_option darkproject_cli_options[] = {
	{
		.name = "--multicolor",
		.arg_desc = "[on|off]",
		.help = "Toggle multicolor animation effect mode",
		.has_arg = 1,
		.parse = parse_multicolor,
		.apply_step = ALLOY_STEP_COLORS,
	},
	{
		.name = "--direction",
		.alias = "--angle",
		.arg_desc = "<0-3|right|left|down|up>",
		.help = "Set animation direction for dynamic effects",
		.has_arg = 1,
		.parse = parse_direction,
		.apply_step = ALLOY_STEP_COLORS,
	},
	{
		.name = "--custom-zone",
		.alias = "--cust",
		.arg_desc = "<1-5>",
		.help = "Select active custom lighting zone (1-5)",
		.has_arg = 1,
		.parse = parse_custom_zone,
		.apply_step = ALLOY_STEP_COLORS,
	},
};

static struct alloy_rgb scale_rgb(struct alloy_rgb c, int num, int den)
{
	if (den <= 0)
		return (struct alloy_rgb){ 0, 0, 0 };
	c.r = (uint8_t)ALLOY_CLAMP((int)c.r * num / den, 0, 255);
	c.g = (uint8_t)ALLOY_CLAMP((int)c.g * num / den, 0, 255);
	c.b = (uint8_t)ALLOY_CLAMP((int)c.b * num / den, 0, 255);
	return c;
}

static struct alloy_rgb hue_to_rgb(int hue)
{
	int h = ((hue % 360) + 360) % 360;
	int sector = h / 60;
	int frac = (h % 60) * 255 / 60;
	int q = 255 - frac;

	switch (sector) {
	case 0:
		return (struct alloy_rgb){ 255, (uint8_t)frac, 0 };
	case 1:
		return (struct alloy_rgb){ (uint8_t)q, 255, 0 };
	case 2:
		return (struct alloy_rgb){ 0, 255, (uint8_t)frac };
	case 3:
		return (struct alloy_rgb){ 0, (uint8_t)q, 255 };
	case 4:
		return (struct alloy_rgb){ (uint8_t)frac, 0, 255 };
	default:
		return (struct alloy_rgb){ 255, 0, (uint8_t)q };
	}
}

static const struct alloy_rgb star_palette[] = {
	{ 255, 255, 0 }, /* Yellow */
	{ 160, 32, 240 }, /* Violet/Purple */
	{ 0, 255, 255 }, /* Cyan */
	{ 0, 120, 255 }, /* Blue */
	{ 0, 255, 0 }, /* Bright Pure Green */
	{ 255, 255, 255 }, /* White */
	{ 80, 255, 120 }, /* Seledyn / Lime */
	{ 255, 0, 180 }, /* Magenta */
	{ 255, 160, 0 }, /* Orange */
};

static const struct alloy_rgb random_palette[] = {
	{ 255, 255, 0 }, /* Yellow */
	{ 255, 255, 140 }, /* Pastel Yellow */
	{ 160, 32, 240 }, /* Violet/Purple */
	{ 200, 140, 255 }, /* Light Violet */
	{ 0, 255, 255 }, /* Cyan */
	{ 140, 255, 255 }, /* Pastel Cyan */
	{ 0, 120, 255 }, /* Blue */
	{ 130, 190, 255 }, /* Soft Blue */
	{ 0, 255, 0 }, /* Pure Green */
	{ 140, 255, 140 }, /* Pastel Green */
	{ 80, 255, 120 }, /* Seledyn / Lime */
	{ 255, 0, 180 }, /* Magenta */
	{ 255, 140, 200 }, /* Pastel Pink */
	{ 255, 160, 0 }, /* Orange */
	{ 255, 200, 130 }, /* Soft Peach */
	{ 255, 255, 255 }, /* White */
};

static struct alloy_rgb ripples_multi_color(double dist)
{
	/* Color gradient: Red (0..6) -> Orange (6..14) -> Yellow (14..24) -> Cyan (24..38) -> Blue (38+) */
	if (dist < 6.0) {
		double t = dist / 6.0;
		/* Red (255,0,0) to Orange (255,128,0) */
		return (struct alloy_rgb){ 255, (uint8_t)(t * 128.0), 0 };
	} else if (dist < 14.0) {
		double t = (dist - 6.0) / 8.0;
		/* Orange (255,128,0) to Yellow (255,255,0) */
		return (struct alloy_rgb){ 255, (uint8_t)(128.0 + t * 127.0),
					   0 };
	} else if (dist < 26.0) {
		double t = (dist - 14.0) / 12.0;
		/* Yellow (255,255,0) to Cyan (0,255,255) */
		return (struct alloy_rgb){ (uint8_t)(255.0 * (1.0 - t)), 255,
					   (uint8_t)(t * 255.0) };
	} else if (dist < 42.0) {
		double t = (dist - 26.0) / 16.0;
		/* Cyan (0,255,255) to Blue (0,120,255) */
		return (struct alloy_rgb){ 0, (uint8_t)(255.0 - t * 135.0),
					   255 };
	} else {
		/* Deep Blue (0, 100, 255) */
		return (struct alloy_rgb){ 0, 100, 255 };
	}
}

static struct alloy_rgb footprint_multi_color(double diag)
{
	/* Diagonal gradient (0.0 top-left to 1.0 bottom-right):
	 * Orange -> Yellow -> Green -> Cyan -> Blue -> Violet -> Pink -> Hot Pink (No Red)
	 */
	static const struct alloy_rgb stops[] = {
		{ 255, 140, 0 }, /* Orange (0.0) */
		{ 255, 230, 0 }, /* Yellow (0.15) */
		{ 0, 255, 50 }, /* Green (0.30) */
		{ 0, 240, 255 }, /* Cyan (0.45) */
		{ 0, 100, 255 }, /* Blue (0.60) */
		{ 160, 32, 240 }, /* Violet (0.75) */
		{ 255, 105, 180 }, /* Pink (0.88) */
		{ 255, 20, 147 }, /* Hot Pink (1.0) */
	};
	static const double pos[] = { 0.0,  0.15, 0.30, 0.45,
				      0.60, 0.75, 0.88, 1.0 };

	if (diag <= 0.0)
		return stops[0];
	if (diag >= 1.0)
		return stops[7];

	for (size_t i = 0; i < 7; i++) {
		if (diag >= pos[i] && diag <= pos[i + 1]) {
			double t = (diag - pos[i]) / (pos[i + 1] - pos[i]);
			return (struct alloy_rgb){
				.r = (uint8_t)(stops[i].r * (1.0 - t) +
					       stops[i + 1].r * t),
				.g = (uint8_t)(stops[i].g * (1.0 - t) +
					       stops[i + 1].g * t),
				.b = (uint8_t)(stops[i].b * (1.0 - t) +
					       stops[i + 1].b * t),
			};
		}
	}
	return stops[7];
}

static void alu87a_key_center(int row, int col, int *out_r, int *out_c,
			      int *out_id)
{
	int tier = (row - 1) / 3;
	if (tier < 0)
		tier = 0;
	if (tier > 5)
		tier = 5;

	int kc = col;
	int kid = 0;

	switch (tier) {
	case 0: /* Esc, F1..F12, Prt, Scr, Pau */
		if (col < 9) {
			kc = 5;
			kid = 1;
		} else if (col < 33) {
			int f = (col - 9) / 6;
			kc = 14 + f * 5;
			kid = 2 + f;
		} else if (col < 58) {
			int f = (col - 34) / 6;
			kc = 39 + f * 5;
			kid = 6 + f;
		} else if (col < 83) {
			int f = (col - 59) / 6;
			kc = 64 + f * 5;
			kid = 10 + f;
		} else if (col < 89) {
			kc = 86;
			kid = 14;
		} else if (col < 94) {
			kc = 91;
			kid = 15;
		} else {
			kc = 96;
			kid = 16;
		}
		break;
	case 1: /* ~ .. = , Backspace, Ins, Home, PgUp */
		if (col < 67) {
			int idx = (col >= 2) ? (col - 2) / 5 : 0;
			kc = 4 + idx * 5;
			kid = 20 + idx;
		} else if (col < 82) {
			kc = 74;
			kid = 33; /* Backspace */
		} else if (col < 89) {
			kc = 86;
			kid = 34; /* Ins */
		} else if (col < 94) {
			kc = 91;
			kid = 35; /* Home */
		} else {
			kc = 96;
			kid = 36; /* PgUp */
		}
		break;
	case 2: /* Tab, Q .. ], \, Del, End, PgDn */
		if (col < 9) {
			kc = 5;
			kid = 40; /* Tab */
		} else if (col < 69) {
			int idx = (col - 9) / 5;
			kc = 11 + idx * 5;
			kid = 41 + idx;
		} else if (col < 82) {
			kc = 75;
			kid = 53; /* Backslash */
		} else if (col < 89) {
			kc = 86;
			kid = 54; /* Del */
		} else if (col < 94) {
			kc = 91;
			kid = 55; /* End */
		} else {
			kc = 96;
			kid = 56; /* PgDn */
		}
		break;
	case 3: /* Caps, A .. ', Enter */
		if (col < 11) {
			kc = 6;
			kid = 60; /* Caps */
		} else if (col < 66) {
			int idx = (col - 11) / 5;
			kc = 13 + idx * 5;
			kid = 61 + idx;
		} else {
			kc = 74;
			kid = 72; /* Enter */
		}
		break;
	case 4: /* LShift, Z .. /, RShift, Up */
		if (col < 14) {
			kc = 8;
			kid = 80; /* LShift */
		} else if (col < 64) {
			int idx = (col - 14) / 5;
			kc = 16 + idx * 5;
			kid = 81 + idx;
		} else if (col < 83) {
			kc = 73;
			kid = 91; /* RShift */
		} else {
			kc = 90;
			kid = 92; /* Up Arrow */
		}
		break;
	case 5: /* Ctrl, Win, Alt, Space, Alt, Fn, Menu, Ctrl, Left, Down, Right */
	default:
		if (col < 8) {
			kc = 5;
			kid = 101; /* LCtrl */
		} else if (col < 14) {
			kc = 11;
			kid = 102; /* LWin */
		} else if (col < 20) {
			kc = 17;
			kid = 103; /* LAlt */
		} else if (col < 56) {
			kc = 38;
			kid = 100; /* Spacebar */
		} else if (col < 62) {
			kc = 59;
			kid = 104; /* RAlt */
		} else if (col < 68) {
			kc = 65;
			kid = 105; /* Fn */
		} else if (col < 74) {
			kc = 71;
			kid = 106; /* Menu */
		} else if (col < 82) {
			kc = 78;
			kid = 107; /* RCtrl */
		} else if (col < 89) {
			kc = 86;
			kid = 108; /* Left Arrow */
		} else if (col < 94) {
			kc = 91;
			kid = 109; /* Down Arrow */
		} else {
			kc = 96;
			kid = 110; /* Right Arrow */
		}
		break;
	}

	*out_r = 1 + tier * 3 + 1;
	*out_c = kc;
	*out_id = kid;
}

static int alu87a_cell_color(const struct alloy_driver *drv,
			     const struct alloy_config *config, int row,
			     int col, long ms, struct alloy_rgb *out)
{
	const struct alloy_devcfg *cfg = alloy_devcfg_c(config);
	uint8_t fx = cfg->zone_fx[0];
	uint8_t multi = cfg->zone_fx_param[0][ALLOY_FX_P_MULTICOLOR];
	uint8_t dir = cfg->zone_fx_param[0][ALLOY_FX_P_DIRECTION];
	int speed = cfg->zone_fx_param[0][ALLOY_FX_P_SPEED] ?
			    cfg->zone_fx_param[0][ALLOY_FX_P_SPEED] :
			    3;

	(void)drv;
	if (speed < 1)
		speed = 1;
	if (speed > 5)
		speed = 5;
	uint8_t brightness = cfg->brightness ? cfg->brightness : 100;
	struct alloy_rgb base_color = cfg->zone_color[0];
	struct alloy_rgb c = base_color;

	/* Speed multiplier in alloyctl (1=Slow, 5=Fast) */
	long tms = ms * speed / 2;

	if (fx == 1) {
		/* Effect 1: WAVE */
		long spatial_deg = 0;
		long time_deg = (tms / 6) % 360;
		long phase_deg = 0;

		/*
		 * Directions for ALU87A:
		 * 0 = Right (Left-to-Right wave)
		 * 1 = Left  (Right-to-Left wave)
		 * 2 = Down  (Top-to-Bottom wave)
		 * 3 = Up    (Bottom-to-Top wave)
		 */
		if (dir == 0) {
			spatial_deg = (col * 360) / 48;
			phase_deg =
				((time_deg - spatial_deg) % 360 + 360) % 360;
		} else if (dir == 1) {
			spatial_deg = (col * 360) / 48;
			phase_deg =
				((time_deg + spatial_deg) % 360 + 360) % 360;
		} else if (dir == 2) {
			spatial_deg = (row * 360) / 10;
			phase_deg =
				((time_deg - spatial_deg) % 360 + 360) % 360;
		} else {
			spatial_deg = (row * 360) / 10;
			phase_deg =
				((time_deg + spatial_deg) % 360 + 360) % 360;
		}

		if (multi) {
			/* Flowing rainbow spectrum */
			c = hue_to_rgb((int)phase_deg);
		} else {
			/* Flowing brightness crest in base color */
			int level = 30 + (phase_deg < 180 ?
						  (int)(phase_deg * 225 / 180) :
						  (int)((360 - phase_deg) *
							225 / 180));
			c = scale_rgb(base_color, level, 255);
		}
	} else if (fx == 2) {
		/* Effect 2: COLOR CYCLE (sweeps whole keyboard through rainbow hues) */
		long cycle_period_ms = 24000 - (speed - 1) * 4500;
		if (cycle_period_ms < 4000)
			cycle_period_ms = 4000;

		int hue =
			(int)(((ms % cycle_period_ms) * 360) / cycle_period_ms);
		c = hue_to_rgb(hue);
	} else if (fx == 3) {
		/* Effect 3: BREATHING (smooth pulsation) */
		long cycle_ms = 8000 - (speed - 1) * 1400;
		if (cycle_ms < 1500)
			cycle_ms = 1500;
		long phase = ((ms % cycle_ms) * 360) / cycle_ms;
		int level = 20 + (phase < 180 ?
					  (int)(phase * 235 / 180) :
					  (int)((360 - phase) * 235 / 180));
		if (multi) {
			long rainbow_ms = 20000;
			int hue = (int)(((ms % rainbow_ms) * 360) / rainbow_ms);
			c = hue_to_rgb(hue);
			c = scale_rgb(c, level, 255);
		} else {
			c = scale_rgb(base_color, level, 255);
		}
	} else if (fx == 4) {
		/* Effect 4: RIVER (starts top-right going left, then snake down to bottom-right) */
		int krow = (row - 1) / 3;
		if (krow < 0)
			krow = 0;
		if (krow > 5)
			krow = 5;

		const int art_w = 98;
		int x = (krow % 2 == 0) ? (art_w - 1 - col) : col;

		if (x < 0)
			x = 0;
		if (x >= art_w)
			x = art_w - 1;

		int snake_pos = krow * art_w + x;
		int total_len = 6 * art_w;
		long tms_river = ms;
		long time_pos = (tms_river / 8) % total_len;
		long dist = ((time_pos - snake_pos) % total_len + total_len) %
			    total_len;
		int trail = 110;
		int level = (dist < trail) ?
				    (30 + (int)((trail - dist) * 225 / trail)) :
				    20;

		if (multi) {
			int hue = (int)(((snake_pos * 360) / total_len +
					 (tms_river / 16)) %
					360);
			c = hue_to_rgb(hue);
			c = scale_rgb(c, level, 255);
		} else {
			c = scale_rgb(base_color, level, 255);
		}
	} else if (fx == 5) {
		/* Effect 5: RAIN (distinct vertical columnar rain streams) */
		int kcol = (col * 17) / 98;
		if (kcol < 0)
			kcol = 0;
		if (kcol > 16)
			kcol = 16;

		long col_period = 2400 - (speed - 1) * 280;
		if (col_period < 1000)
			col_period = 1000;

		static const uint8_t col_order[17] = { 0, 8,  3, 12, 5, 15,
						       1, 10, 6, 14, 2, 9,
						       4, 13, 7, 16, 11 };
		long col_offset = (long)col_order[kcol] * (col_period / 17);
		long t = (ms + col_offset) % col_period;
		double drop_y = ((double)t * 26.0 / (double)col_period) - 3.0;
		double dy = (double)row - drop_y;

		int level = 15;
		if (dy >= -0.5 && dy <= 6.0)
			level = 30 + (int)((6.0 - dy) * 225.0 / 6.5);

		if (multi) {
			int hue = (int)((kcol * 22 + (ms / 30)) % 360);
			c = hue_to_rgb(hue);
			c = scale_rgb(c, level, 255);
		} else {
			c = scale_rgb(base_color, level, 255);
		}
	} else if (fx == 6) {
		/* Effect 6: SPIRAL WAVE (rotating clock color wheel sweep around center) */
		double dx = (double)col - 49.0;
		double dy = ((double)row - 9.5) * 2.2;
		double angle = atan2(dy, dx) * 180.0 / 3.14159265;
		if (angle < 0.0)
			angle += 360.0;

		long cycle_ms = 10000 - (speed - 1) * 2000;
		if (cycle_ms < 2000)
			cycle_ms = 2000;
		long time_deg = ((ms % cycle_ms) * 360) / cycle_ms;

		long phase_deg =
			(dir == 1) ?
				((long)(angle + time_deg) % 360 + 360) % 360 :
				((long)(angle - time_deg) % 360 + 360) % 360;
		c = hue_to_rgb((int)phase_deg);
	} else if (fx == 7) {
		/* Effect 7: STAR (three-phase radial starburst in vivid colors) */
		int kr, kc, kid;
		alu87a_key_center(row, col, &kr, &kc, &kid);

		double dx = (double)kc - 49.0;
		double dy = ((double)kr - 9.5) * 2.2;
		double dist = sqrt(dx * dx + dy * dy);
		int key_group = (int)(dist / 14.0) % 3;

		long pulse_ms = 1250 - (speed - 1) * 180;
		if (pulse_ms < 480)
			pulse_ms = 480;

		long cycle_index = ms / pulse_ms;
		int active_phase = (int)(cycle_index % 3);
		long phase_t = ms % pulse_ms;

		if (key_group == active_phase) {
			double prog = (double)phase_t / (double)pulse_ms;
			double intensity = sin(prog * 3.14159265);
			int level = (int)(intensity * 255.0);

			unsigned color_seed =
				(unsigned)((uint32_t)kid * 2654435761u +
					   (uint32_t)cycle_index * 1013904223u +
					   19);
			int palette_idx = (int)(color_seed %
						ALLOY_ARRAY_SIZE(star_palette));
			struct alloy_rgb key_color = star_palette[palette_idx];
			c = scale_rgb(key_color, level, 255);
		} else {
			c = (struct alloy_rgb){ 0, 0, 0 };
		}
	} else if (fx == 8) {
		/* Effect 8: RANDOM (entire keyboard illuminated, keys smoothly crossfade to new random colors) */
		int kr, kc, kid;
		alu87a_key_center(row, col, &kr, &kc, &kid);

		long change_period = 3600 - (speed - 1) * 550;
		if (change_period < 1200)
			change_period = 1200;

		long key_offset = (long)(((uint32_t)kid * 1013904223u) %
					 (uint32_t)change_period);
		long total_t = ms + key_offset;
		long step = total_t / change_period;
		long step_t = total_t % change_period;

		unsigned seed_prev =
			(unsigned)((uint32_t)kid * 2654435761u +
				   (uint32_t)step * 1013904223u + 17);
		struct alloy_rgb c_prev =
			random_palette[seed_prev %
				       ALLOY_ARRAY_SIZE(random_palette)];

		unsigned seed_next =
			(unsigned)((uint32_t)kid * 2654435761u +
				   (uint32_t)(step + 1) * 1013904223u + 17);
		struct alloy_rgb c_next =
			random_palette[seed_next %
				       ALLOY_ARRAY_SIZE(random_palette)];

		long trans_dur = 600;
		if (step_t < trans_dur) {
			double alpha = (double)step_t / (double)trans_dur;
			c.r = (uint8_t)(c_prev.r * (1.0 - alpha) +
					c_next.r * alpha);
			c.g = (uint8_t)(c_prev.g * (1.0 - alpha) +
					c_next.g * alpha);
			c.b = (uint8_t)(c_prev.b * (1.0 - alpha) +
					c_next.b * alpha);
		} else {
			c = c_prev;
		}
	} else if (fx == 9) {
		/* Effect 9: COLOR DISCHARGE (horizontal rainbow discharge streak across row upon simulated key press) */
		int kr, kc, kid;
		alu87a_key_center(row, col, &kr, &kc, &kid);
		int cell_tier = (row - 1) / 3;
		if (cell_tier < 0)
			cell_tier = 0;
		if (cell_tier > 5)
			cell_tier = 5;

		long strike_interval = 2800 - (speed - 1) * 350;
		if (strike_interval < 1400)
			strike_interval = 1400;

		long streak_duration = 1800 - (speed - 1) * 260;
		if (streak_duration < 760)
			streak_duration = 760;

		struct alloy_rgb discharge_c = { 0, 0, 0 };

		for (int s = 0; s < 4; s++) {
			long strike_index = ms / strike_interval - s;
			if (strike_index < 0)
				continue;
			long strike_start = strike_index * strike_interval;
			long elapsed = ms - strike_start;
			if (elapsed < 0 || elapsed >= streak_duration)
				continue;

			unsigned seed = (unsigned)((uint32_t)strike_index *
							   2654435761u +
						   41);
			int strike_tier = (int)(seed % 6);
			if (cell_tier != strike_tier)
				continue;

			int strike_col = 12 + (int)((seed * 37) % 76);
			double dcol = (double)kc - (double)strike_col;

			/* Edge boundary: if near left edge, travel right only; if near right edge, travel left only */
			if (strike_col < 20 && dcol < -2.0)
				continue;
			if (strike_col > 80 && dcol > 2.0)
				continue;

			double abs_dcol = fabs(dcol);
			double travel_dist =
				((double)elapsed / (double)streak_duration) *
				85.0;
			double dist_to_wave = fabs(abs_dcol - travel_dist);

			if (dist_to_wave < 12.0) {
				double wave_profile =
					1.0 - (dist_to_wave / 12.0);
				double fade = 1.0 - ((double)elapsed /
						     (double)streak_duration);
				double intensity = wave_profile * fade;

				int hue = (int)((strike_index * 75 +
						 (unsigned)(abs_dcol * 9.0) +
						 (unsigned)(elapsed * 0.3))) %
					  360;
				struct alloy_rgb w_rgb = hue_to_rgb(hue);
				struct alloy_rgb key_pulse = scale_rgb(
					w_rgb, (int)(intensity * 255.0), 255);

				if (key_pulse.r > discharge_c.r)
					discharge_c.r = key_pulse.r;
				if (key_pulse.g > discharge_c.g)
					discharge_c.g = key_pulse.g;
				if (key_pulse.b > discharge_c.b)
					discharge_c.b = key_pulse.b;
			}
		}

		c = discharge_c;
	} else if (fx == 10) {
		/* Effect 10: TRIGGER (reactive individual key flash and smooth fade) */
		int kr, kc, kid;
		alu87a_key_center(row, col, &kr, &kc, &kid);

		long fade_ms = 1000 - (speed - 1) * 160;
		if (fade_ms < 360)
			fade_ms = 360;

		long strike_interval = 400 - (speed - 1) * 50;
		if (strike_interval < 200)
			strike_interval = 200;

		struct alloy_rgb trigger_c = { 0, 0, 0 };

		for (int s = 0; s < 6; s++) {
			long strike_index = ms / strike_interval - s;
			if (strike_index < 0)
				continue;
			long strike_start = strike_index * strike_interval;
			long elapsed = ms - strike_start;
			if (elapsed < 0 || elapsed >= fade_ms)
				continue;

			unsigned seed = (unsigned)((uint32_t)strike_index *
							   2654435761u +
						   73);
			int struck_kid = (int)(seed % 111);

			if (kid == struck_kid) {
				double fade = 1.0 - ((double)elapsed /
						     (double)fade_ms);
				fade = fade *
				       fade; /* smooth natural light decay */

				struct alloy_rgb flash_color;
				if (multi) {
					int hue = (int)((strike_index * 97) %
							360);
					flash_color = hue_to_rgb(hue);
				} else {
					flash_color = base_color;
				}

				struct alloy_rgb pulse = scale_rgb(
					flash_color, (int)(fade * 255.0), 255);

				if (pulse.r > trigger_c.r)
					trigger_c.r = pulse.r;
				if (pulse.g > trigger_c.g)
					trigger_c.g = pulse.g;
				if (pulse.b > trigger_c.b)
					trigger_c.b = pulse.b;
			}
		}

		c = trigger_c;
	} else if (fx == 11) {
		/* Effect 11: RIPPLES (expanding circular ripple wave from simulated key strikes) */
		int kr, kc, kid;
		alu87a_key_center(row, col, &kr, &kc, &kid);

		long strike_interval = 2200;
		long ripple_duration = 1900;

		struct alloy_rgb ripple_c = { 0, 0, 0 };

		for (int s = 0; s < 3; s++) {
			long strike_index = ms / strike_interval - s;
			if (strike_index < 0)
				continue;
			long strike_start = strike_index * strike_interval;
			long elapsed = ms - strike_start;
			if (elapsed < 0 || elapsed >= ripple_duration)
				continue;

			unsigned seed = (unsigned)((uint32_t)strike_index *
							   2654435761u +
						   89);
			int strike_tier = 1 + (int)(seed % 4); /* tier 1..4 */
			int strike_col = 20 + (int)((seed * 43) % 60);

			int strike_r = 1 + strike_tier * 3 + 1;
			int strike_c = strike_col;

			double dr = ((double)kr - (double)strike_r) * 2.2;
			double dc = (double)kc - (double)strike_c;
			double dist = sqrt(dr * dr + dc * dc);

			double wave_radius =
				((double)elapsed / (double)ripple_duration) *
				110.0;
			double dist_to_wave = fabs(dist - wave_radius);

			if (dist_to_wave < 12.0) {
				double wave_profile =
					1.0 - (dist_to_wave / 12.0);
				double fade = 1.0 - ((double)elapsed /
						     (double)ripple_duration);
				double intensity = wave_profile * fade;

				struct alloy_rgb wave_rgb =
					multi ? ripples_multi_color(dist) :
						base_color;
				struct alloy_rgb pulse = scale_rgb(
					wave_rgb, (int)(intensity * 255.0),
					255);

				if (pulse.r > ripple_c.r)
					ripple_c.r = pulse.r;
				if (pulse.g > ripple_c.g)
					ripple_c.g = pulse.g;
				if (pulse.b > ripple_c.b)
					ripple_c.b = pulse.b;
			}
		}

		c = ripple_c;
	} else if (fx == 12) {
		/* Effect 12: SINE WAVE (2 undulating vertical sine crests propagating across keyboard) */
		int kr, kc, kid;
		alu87a_key_center(row, col, &kr, &kc, &kid);

		long cycle_ms = 4500 - (speed - 1) * 700;
		if (cycle_ms < 1400)
			cycle_ms = 1400;

		double phase = (double)(ms % cycle_ms) / (double)cycle_ms *
			       2.0 * 3.14159265;
		double k_spatial = 2.0 * (2.0 * 3.14159265) / 100.0;
		double target_y =
			9.5 +
			6.2 * sin(k_spatial * ((double)kc - 45.0) + phase);

		double dy = fabs((double)kr - target_y);
		double thickness = 3.4;

		if (dy < thickness) {
			double intensity = 1.0 - (dy / thickness);
			intensity = intensity * intensity;

			if (multi) {
				int hue = (int)((kc * 3.6 +
						 (ms * 360) / cycle_ms)) %
					  360;
				struct alloy_rgb line_c = hue_to_rgb(hue);
				c = scale_rgb(line_c, (int)(intensity * 255.0),
					      255);
			} else {
				c = scale_rgb(base_color,
					      (int)(intensity * 255.0), 255);
			}
		} else {
			c = (struct alloy_rgb){ 0, 0, 0 };
		}
	} else if (fx == 13) {
		/* Effect 13: FOOTPRINT (full illuminated keyboard, clicked keys dim to black and recover) */
		int kr, kc, kid;
		alu87a_key_center(row, col, &kr, &kc, &kid);

		long recovery_duration = 2000 - (speed - 1) * 350;
		if (recovery_duration < 550)
			recovery_duration = 550;

		long strike_interval = 400;

		double key_brightness = 1.0;

		for (int s = 0; s < 8; s++) {
			long strike_index = ms / strike_interval - s;
			if (strike_index < 0)
				continue;
			long strike_start = strike_index * strike_interval;
			long elapsed = ms - strike_start;
			if (elapsed < 0 || elapsed >= recovery_duration)
				continue;

			unsigned seed = (unsigned)((uint32_t)strike_index *
							   2654435761u +
						   103);
			int struck_kid = (int)(seed % 111);

			if (kid == struck_kid) {
				double dim = (double)elapsed /
					     (double)recovery_duration;
				dim = dim *
				      dim; /* smooth recovery curve from dark to full */
				if (dim < key_brightness)
					key_brightness = dim;
			}
		}

		struct alloy_rgb base_k;
		if (multi) {
			double diag = ((double)kr / 18.0 * 0.35) +
				      ((double)kc / 100.0 * 0.65);
			if (diag < 0.0)
				diag = 0.0;
			if (diag > 1.0)
				diag = 1.0;
			base_k = footprint_multi_color(diag);
		} else {
			base_k = base_color;
		}

		c = scale_rgb(base_k, (int)(key_brightness * 255.0), 255);
	} else if (fx == 14) {
		/* Effect 14: CUSTOM (5 preset planar zones: Cust1..Cust5) */
		int kr, kc, kid;
		alu87a_key_center(row, col, &kr, &kc, &kid);
		uint8_t custom_index = cfg->zone_fx_param[0][ALLOY_FX_P_CUSTOM];
		if (custom_index > 4)
			custom_index = 0;

		int lit = 0;
		if (custom_index == 0) {
			/* Cust1: WSAD + Arrows */
			if (kid == 42 || kid == 61 || kid == 62 || kid == 63 ||
			    kid == 92 || kid == 108 || kid == 109 || kid == 110)
				lit = 1;
		} else if (custom_index == 1) {
			/* Cust2: Letters A-Z */
			if ((kid >= 41 && kid <= 50) || /* Q..P */
			    (kid >= 61 && kid <= 69) || /* A..L */
			    (kid >= 81 && kid <= 87)) /* Z..M */
				lit = 1;
		} else if (custom_index == 2) {
			/* Cust3: Digits 0-9 & - = ` Backspace */
			if (kid >= 20 && kid <= 33)
				lit = 1;
		} else if (custom_index == 3) {
			/* Cust4: Function keys (Esc, F1..F12, Prt, Scr, Pau) & Navigation (Ins, Home, PgUp, Del, End, PgDn) */
			if ((kid >= 1 && kid <= 16) ||
			    (kid >= 34 && kid <= 36) ||
			    (kid >= 54 && kid <= 56))
				lit = 1;
		} else {
			/* Cust5: All keys */
			lit = 1;
		}

		if (lit)
			c = base_color;
		else
			c = (struct alloy_rgb){ 0, 0, 0 };
	} else {
		/* Effect 0: SOLID / static color */
		c = base_color;
	}

	/* Apply master brightness */
	*out = scale_rgb(c, brightness, 100);
	return 1;
}

/* Control getters and setters */
static int get_speed(const struct alloy_config *config, uint8_t zone)
{
	const struct alloy_devcfg *cfg = alloy_devcfg_c(config);

	int s = cfg->zone_fx_param[zone][ALLOY_FX_P_SPEED] ?
			cfg->zone_fx_param[zone][ALLOY_FX_P_SPEED] :
			3;
	return ALLOY_CLAMP(s, 1, 5);
}

static void set_speed(struct alloy_config *config, uint8_t zone, int val)
{
	struct alloy_devcfg *cfg = alloy_devcfg(config);

	cfg->zone_fx_param[zone][ALLOY_FX_P_SPEED] =
		(uint8_t)ALLOY_CLAMP(val, 1, 5);
}

static int get_multicolor(const struct alloy_config *config, uint8_t zone)
{
	const struct alloy_devcfg *cfg = alloy_devcfg_c(config);

	return cfg->zone_fx_param[zone][ALLOY_FX_P_MULTICOLOR] != 0;
}

static void set_multicolor(struct alloy_config *config, uint8_t zone, int val)
{
	struct alloy_devcfg *cfg = alloy_devcfg(config);

	cfg->zone_fx_param[zone][ALLOY_FX_P_MULTICOLOR] =
		(uint8_t)(val ? 1 : 0);
}

static int get_direction(const struct alloy_config *config, uint8_t zone)
{
	const struct alloy_devcfg *cfg = alloy_devcfg_c(config);

	int d = cfg->zone_fx_param[zone][ALLOY_FX_P_DIRECTION];
	return ALLOY_CLAMP(d, 0, 3);
}

static void set_direction(struct alloy_config *config, uint8_t zone, int val)
{
	struct alloy_devcfg *cfg = alloy_devcfg(config);

	cfg->zone_fx_param[zone][ALLOY_FX_P_DIRECTION] =
		(uint8_t)ALLOY_CLAMP(val, 0, 3);
}

static int get_custom_zone(const struct alloy_config *config, uint8_t zone)
{
	const struct alloy_devcfg *cfg = alloy_devcfg_c(config);

	int c = cfg->zone_fx_param[zone][ALLOY_FX_P_CUSTOM];
	return ALLOY_CLAMP(c, 0, 4);
}

static void set_custom_zone(struct alloy_config *config, uint8_t zone, int val)
{
	struct alloy_devcfg *cfg = alloy_devcfg(config);

	cfg->zone_fx_param[zone][ALLOY_FX_P_CUSTOM] =
		(uint8_t)ALLOY_CLAMP(val, 0, 4);
}

static const char *const wave_directions[] = { "Right", "Left", "Down", "Up" };
static const char *const spiral_directions[] = { "Reverse", "Forward" };
static const char *const custom_choices[] = { "Cust1", "Cust2", "Cust3",
					      "Cust4", "Cust5" };

static const struct alloy_effect_ctrl ctrl_speed = {
	.name = "SPEED",
	.type = ALLOY_CTRL_SLIDER,
	.min_val = 1,
	.max_val = 5,
	.get = get_speed,
	.set = set_speed,
};

static const struct alloy_effect_ctrl ctrl_multicolor = {
	.name = "MULTICOLOR",
	.type = ALLOY_CTRL_TOGGLE,
	.get = get_multicolor,
	.set = set_multicolor,
};

static const struct alloy_effect_ctrl ctrl_wave_direction = {
	.name = "DIRECTION",
	.type = ALLOY_CTRL_CHOICE,
	.choices = wave_directions,
	.num_choices = ALLOY_ARRAY_SIZE(wave_directions),
	.get = get_direction,
	.set = set_direction,
};

static const struct alloy_effect_ctrl ctrl_spiral_direction = {
	.name = "DIRECTION",
	.type = ALLOY_CTRL_CHOICE,
	.choices = spiral_directions,
	.num_choices = ALLOY_ARRAY_SIZE(spiral_directions),
	.get = get_direction,
	.set = set_direction,
};

static const struct alloy_effect_ctrl ctrl_custom_zone = {
	.name = "CUSTOM ZONE",
	.type = ALLOY_CTRL_CHOICE,
	.choices = custom_choices,
	.num_choices = ALLOY_ARRAY_SIZE(custom_choices),
	.get = get_custom_zone,
	.set = set_custom_zone,
};

static const struct alloy_effect_ctrl wave_ctrls[] = {
	ctrl_speed,
	ctrl_multicolor,
	ctrl_wave_direction,
};

static const struct alloy_effect_ctrl color_cycle_ctrls[] = {
	ctrl_speed,
};

static const struct alloy_effect_ctrl breathing_ctrls[] = {
	ctrl_speed,
	ctrl_multicolor,
};

static const struct alloy_effect_ctrl river_ctrls[] = {
	ctrl_multicolor,
};

static const struct alloy_effect_ctrl rain_ctrls[] = {
	ctrl_speed,
	ctrl_multicolor,
};

static const struct alloy_effect_ctrl spiral_ctrls[] = {
	ctrl_speed,
	ctrl_spiral_direction,
};

static const struct alloy_effect_ctrl star_ctrls[] = {
	ctrl_speed,
};

static const struct alloy_effect_ctrl random_ctrls[] = {
	ctrl_speed,
};

static const struct alloy_effect_ctrl color_discharge_ctrls[] = {
	ctrl_speed,
};

static const struct alloy_effect_ctrl trigger_ctrls[] = {
	ctrl_speed,
	ctrl_multicolor,
};

static const struct alloy_effect_ctrl ripples_ctrls[] = {
	ctrl_multicolor,
};

static const struct alloy_effect_ctrl sine_wave_ctrls[] = {
	ctrl_speed,
	ctrl_multicolor,
};

static const struct alloy_effect_ctrl footprint_ctrls[] = {
	ctrl_speed,
	ctrl_multicolor,
};

static const struct alloy_effect_ctrl custom_ctrls[] = {
	ctrl_custom_zone,
};

static size_t alu87a_fx_ctrls(const struct alloy_driver *drv, uint8_t fx,
			      const struct alloy_effect_ctrl **out_ctrls)
{
	(void)drv;

	switch (fx) {
	case 1: /* WAVE */
		*out_ctrls = wave_ctrls;
		return ALLOY_ARRAY_SIZE(wave_ctrls);
	case 2: /* COLOR CYCLE */
		*out_ctrls = color_cycle_ctrls;
		return ALLOY_ARRAY_SIZE(color_cycle_ctrls);
	case 3: /* BREATHING */
		*out_ctrls = breathing_ctrls;
		return ALLOY_ARRAY_SIZE(breathing_ctrls);
	case 4: /* RIVER */
		*out_ctrls = river_ctrls;
		return ALLOY_ARRAY_SIZE(river_ctrls);
	case 5: /* RAIN */
		*out_ctrls = rain_ctrls;
		return ALLOY_ARRAY_SIZE(rain_ctrls);
	case 6: /* SPIRAL WAVE */
		*out_ctrls = spiral_ctrls;
		return ALLOY_ARRAY_SIZE(spiral_ctrls);
	case 7: /* STAR */
		*out_ctrls = star_ctrls;
		return ALLOY_ARRAY_SIZE(star_ctrls);
	case 8: /* RANDOM */
		*out_ctrls = random_ctrls;
		return ALLOY_ARRAY_SIZE(random_ctrls);
	case 9: /* COLOR DISCHARGE */
		*out_ctrls = color_discharge_ctrls;
		return ALLOY_ARRAY_SIZE(color_discharge_ctrls);
	case 10: /* TRIGGER */
		*out_ctrls = trigger_ctrls;
		return ALLOY_ARRAY_SIZE(trigger_ctrls);
	case 11: /* RIPPLES */
		*out_ctrls = ripples_ctrls;
		return ALLOY_ARRAY_SIZE(ripples_ctrls);
	case 12: /* SINE WAVE */
		*out_ctrls = sine_wave_ctrls;
		return ALLOY_ARRAY_SIZE(sine_wave_ctrls);
	case 13: /* FOOTPRINT */
		*out_ctrls = footprint_ctrls;
		return ALLOY_ARRAY_SIZE(footprint_ctrls);
	case 14: /* CUSTOM */
		*out_ctrls = custom_ctrls;
		return ALLOY_ARRAY_SIZE(custom_ctrls);
	case 0: /* SOLID */
	default:
		*out_ctrls = NULL;
		return 0;
	}
}

static int alu87a_fx_has_color(const struct alloy_driver *drv, uint8_t fx)
{
	(void)drv;

	/* Color Cycle (2), Spiral Wave (6), Star (7), Random (8), and Color Discharge (9) are purely continuous rainbow/multicolor */
	if (fx == 2 || fx == 6 || fx == 7 || fx == 8 || fx == 9)
		return 0;
	return 1;
}

static void alu87a_config_defaults(const struct alloy_driver *drv,
				   struct alloy_config *cfg)
{
	alloy_keyboard_defaults(drv, cfg);
	struct alloy_devcfg *d = alloy_devcfg(cfg);
	d->zone_fx[0] = 0; /* SOLID */
	d->zone_fx_param[0][ALLOY_FX_P_SPEED] = 3;
	d->zone_fx_param[0][ALLOY_FX_P_MULTICOLOR] = 1;
	d->zone_fx_param[0][ALLOY_FX_P_DIRECTION] = 0;
	d->zone_fx_param[0][ALLOY_FX_P_CUSTOM] = 0;
}

static const struct alloy_light_ops alu87a_light = {
	.fx_ctrls = alu87a_fx_ctrls,
	.fx_has_color = alu87a_fx_has_color,
	.cell_color = alu87a_cell_color,
};

static const struct alloy_keyboard_info alu87a_keyboard = {
	.num_profiles = 3,
};

static const struct alloy_devinfo alu87a_info = {
	.caps = ALLOY_CAP_COLOR | ALLOY_CAP_BRIGHTNESS | ALLOY_CAP_FX_GLOBAL |
		ALLOY_CAP_FX_SPEED | ALLOY_CAP_FX_REACTIVE |
		ALLOY_CAP_SNAP_TAP | ALLOY_CAP_PROFILE,
	.polling_rates = darkproject_polling_rates,
	.num_polling_rates = ALLOY_ARRAY_SIZE(darkproject_polling_rates),
	.zones = darkproject_zones,
	.num_zones = ALLOY_ARRAY_SIZE(darkproject_zones),
	.fx_names = darkproject_fx_names,
	.num_fx = ALLOY_ARRAY_SIZE(darkproject_fx_names),
	.light = &alu87a_light,
	.ext = &alu87a_keyboard,
};

static const struct alloy_apply_step alu87a_steps[] = {
	{ ALLOY_STEP_POLLING, 0, darkproject_apply_polling },
	{ ALLOY_STEP_COLORS, 0, darkproject_apply_colors },
	{ ALLOY_STEP_BRIGHTNESS, 0, darkproject_apply_brightness },
	{ ALLOY_STEP_SNAP_TAP, 0, darkproject_apply_snap_tap },
	{ ALLOY_STEP_PROFILE, 0, darkproject_apply_profile },
};

static const struct alloy_driver_ops darkproject_ops = {
	.config_defaults = alu87a_config_defaults,
	.state_save = alloy_keyboard_state_save,
	.state_load = alloy_keyboard_state_load,
	.state_done = alloy_keyboard_state_done,
	.save = darkproject_save,
	.read_config = alu87a_read_config,
	.parse_event = alu87a_parse_event,
};

static const struct alloy_cli_table alu87a_cli[] = {
	{ alloy_devcfg_cli_options, ALLOY_DEVCFG_CLI_COUNT },
	{ alloy_keyboard_cli_options, ALLOY_KEYBOARD_CLI_COUNT },
	{ darkproject_cli_options, ALLOY_ARRAY_SIZE(darkproject_cli_options) },
};

static const struct alloy_hid_params alu87a_hid = {
	.interface = DARKPROJECT_CONFIG_IFACE,
	.event_interface = 1,
	.report_id = DARKPROJECT_REPORT_ID,
	.report_size = DARKPROJECT_REPORT_SIZE,
};

static const struct alloy_driver dark_project_alu87a_onionite_v2 = {
	.name = "Dark Project ALU87A Onionite V2",
	.kind = "keyboard",
	.vendor_id = DARKPROJECT_VENDOR_ID,
	.product_id = 0xe40f,
	.transport_data = &alu87a_hid,
	.config_size = sizeof(struct alloy_keyboard_config),
	.data = &alu87a_info,
	.ascii_art = alloy_art_dark_project_alu87a_onionite_v2,
	.cli_tables = alu87a_cli,
	.num_cli_tables = ALLOY_ARRAY_SIZE(alu87a_cli),
	.apply_steps = alu87a_steps,
	.num_apply_steps = ALLOY_ARRAY_SIZE(alu87a_steps),
	.ui = &alloy_keyboard_ui,
	.ops = &darkproject_ops,
};
ALLOY_DRIVER_REGISTER(dark_project_alu87a_onionite_v2);
