// SPDX-License-Identifier: GPL-2.0-only
/*
 * SteelSeries Apex 100 Gaming Keyboard, USB ID 1038:160E.
 *
 * Protocol notes live in Documentation/protocol/steelseries-apex100.rst.
 * Maintainer: Szymon Wilczek <swilczek.lx@gmail.com>
 *
 * Configuration uses 32-byte HID output reports on USB interface 1.
 * Supports:
 * - Polling rate control: 125, 250, 500, 1000 Hz (cmd 0x04)
 * - Illumination brightness (0-100%, cmd 0x05)
 * - Lighting effects (Steady, Breath with Slow/Med/Fast speeds, cmd 0x07)
 * - Save to onboard flash memory (cmd 0x09)
 * - Firmware version query (cmd 0x10)
 */
#include <stdio.h>
#include <string.h>

#include "art_steelseries_apex100.h"
#include "driver.h"
#include "steelseries/steelseries_common.h"

#define APEX100_CMD_POLLING 0x04
#define APEX100_CMD_BRIGHTNESS 0x05
#define APEX100_CMD_EFFECT 0x07
#define APEX100_CMD_SAVE 0x09
#define APEX100_CMD_FIRMWARE 0x10

#define APEX100_REPORT_SIZE 32

static const char *const apex100_fx_names[] = {
	"STEADY",
	"BREATH",
};

/* Pure packet builders for unit testing */
size_t apex100_build_polling(uint16_t polling_hz, uint8_t *buf);
size_t apex100_build_brightness(uint8_t brightness, uint8_t *buf);
size_t apex100_build_effect(uint8_t fx_mode, uint8_t fx_speed, uint8_t *buf);
size_t apex100_build_save(uint8_t *buf);
size_t apex100_build_firmware_query(uint8_t *buf);

size_t apex100_build_polling(uint16_t polling_hz, uint8_t *buf)
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

	memset(buf, 0, APEX100_REPORT_SIZE);
	buf[0] = APEX100_CMD_POLLING;
	buf[1] = 0x00;
	buf[2] = wire;
	return APEX100_REPORT_SIZE;
}

size_t apex100_build_brightness(uint8_t brightness, uint8_t *buf)
{
	uint8_t wire = (uint8_t)ALLOY_CLAMP(brightness, 0, 100);

	memset(buf, 0, APEX100_REPORT_SIZE);
	buf[0] = APEX100_CMD_BRIGHTNESS;
	buf[1] = 0x00;
	buf[2] = wire;
	return APEX100_REPORT_SIZE;
}

size_t apex100_build_effect(uint8_t fx_mode, uint8_t fx_speed, uint8_t *buf)
{
	uint8_t wire;

	if (fx_mode == 1) {
		/*
		 * Breath mode wire values:
		 * speed 1 = SLOW (wire 0x02)
		 * speed 2 = MEDIUM (wire 0x03)
		 * speed 3 = FAST (wire 0x04)
		 */
		if (fx_speed <= 1)
			wire = 0x02;
		else if (fx_speed == 2)
			wire = 0x03;
		else
			wire = 0x04;
	} else {
		wire = 0x01; /* STEADY */
	}

	memset(buf, 0, APEX100_REPORT_SIZE);
	buf[0] = APEX100_CMD_EFFECT;
	buf[1] = 0x00;
	buf[2] = wire;
	return APEX100_REPORT_SIZE;
}

size_t apex100_build_save(uint8_t *buf)
{
	memset(buf, 0, APEX100_REPORT_SIZE);
	buf[0] = APEX100_CMD_SAVE;
	buf[1] = 0x00;
	return APEX100_REPORT_SIZE;
}

size_t apex100_build_firmware_query(uint8_t *buf)
{
	memset(buf, 0, APEX100_REPORT_SIZE);
	buf[0] = APEX100_CMD_FIRMWARE;
	buf[1] = 0x00;
	return APEX100_REPORT_SIZE;
}

static int apex100_apply_polling(struct alloy_device *dev,
				 const struct alloy_config *cfg)
{
	uint8_t buf[ALLOY_HID_REPORT_SIZE];
	size_t len;

	len = apex100_build_polling(cfg->common.polling_hz, buf);
	return alloy_hid_send(&dev->hid, buf, len);
}

static int apex100_apply_brightness(struct alloy_device *dev,
				    const struct alloy_config *cfg)
{
	uint8_t buf[ALLOY_HID_REPORT_SIZE];
	size_t len;

	len = apex100_build_brightness(cfg->common.brightness, buf);
	return alloy_hid_send(&dev->hid, buf, len);
}

static int apex100_apply_colors(struct alloy_device *dev,
				const struct alloy_config *cfg)
{
	uint8_t buf[ALLOY_HID_REPORT_SIZE];
	size_t len;
	int ret;

	len = apex100_build_brightness(cfg->common.brightness, buf);
	ret = alloy_hid_send(&dev->hid, buf, len);
	if (ret)
		return ret;

	len = apex100_build_effect(cfg->common.zone_fx[0],
				   cfg->common.zone_fx_speed[0], buf);
	return alloy_hid_send(&dev->hid, buf, len);
}

static int apex100_save(struct alloy_device *dev)
{
	uint8_t buf[ALLOY_HID_REPORT_SIZE];
	size_t len;

	len = apex100_build_save(buf);
	return alloy_hid_send(&dev->hid, buf, len);
}

static int apex100_firmware_version(struct alloy_device *dev, char *buf,
				    size_t len)
{
	uint8_t cmd[ALLOY_HID_REPORT_SIZE];
	uint8_t resp[ALLOY_HID_REPORT_SIZE];
	size_t cmd_len;
	int n;

	if (!buf || len == 0)
		return -1;

	cmd_len = apex100_build_firmware_query(cmd);
	n = steelseries_cmd_read(&dev->hid, cmd, cmd_len, resp, sizeof(resp));
	if (n < 1)
		return -1;

	snprintf(buf, len, "0.%02x", resp[0]);
	return 0;
}

static const uint16_t apex100_polling_rates[] = { 1000, 500, 250, 125 };

static const struct alloy_led_zone apex100_zones[] = {
	{ .name = "BACKLIGHT", .def_color = { 0x00, 0x84, 0xFF } },
};

static const struct alloy_driver_ops apex100_ops = {
	.apply_polling = apex100_apply_polling,
	.apply_colors = apex100_apply_colors,
	.apply_brightness = apex100_apply_brightness,
	.save = apex100_save,
	.firmware_version = apex100_firmware_version,
};

static const struct alloy_driver steelseries_apex100 = {
	.name = "SteelSeries Apex 100",
	.type = ALLOY_DEV_KEYBOARD,
	.vendor_id = 0x1038,
	.product_id = 0x160E,
	.interface = 1,
	.report_size = APEX100_REPORT_SIZE,
	.polling_rates = apex100_polling_rates,
	.num_polling_rates = ALLOY_ARRAY_SIZE(apex100_polling_rates),
	.zones = apex100_zones,
	.num_zones = ALLOY_ARRAY_SIZE(apex100_zones),
	.fx_names = apex100_fx_names,
	.num_fx = ALLOY_ARRAY_SIZE(apex100_fx_names),
	.caps = ALLOY_CAP_BRIGHTNESS | ALLOY_CAP_FIRMWARE_VERSION |
		ALLOY_CAP_FX_GLOBAL | ALLOY_CAP_FX_SPEED,
	.ascii_art = alloy_art_steelseries_apex100,
	.ops = &apex100_ops,
	.config_defaults = alloy_config_generic_defaults,
};
ALLOY_DRIVER_REGISTER(steelseries_apex100);
