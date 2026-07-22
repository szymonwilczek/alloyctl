// SPDX-License-Identifier: GPL-2.0-only
/*
 * SteelSeries Aerox 3 Wireless (1038:1838) driver tests.
 *
 * Exercises the pure packet builders and the battery decode against the exact
 * byte sequences verified on hardware (fw 1.3.1) and cross-checked against the
 * gort818 / rivalcfg captures.
 *
 * Protocol reference: Documentation/protocol/steelseries-aerox3-wireless.rst.
 */
#include <stdlib.h>
#include <string.h>

#include "driver.h"
#include "mock_hid.h"
#include "test.h"

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

static const struct alloy_driver *a3wl(void)
{
	const struct alloy_driver *drv = alloy_driver_find(0x1038, 0x1838);

	if (!drv) {
		printf("FAIL: aerox 3 wireless driver not registered\n");
		exit(1);
	}
	return drv;
}

ALLOY_TEST(test_registry)
{
	const struct alloy_driver *drv = a3wl();

	ASSERT_EQ(drv->interface, 3);
	ASSERT_EQ(drv->event_interface, 4);
	ASSERT_EQ(drv->dpi.min, 100);
	ASSERT_EQ(drv->dpi.max, 18000);
	ASSERT_EQ(drv->num_zones, 3);
	ASSERT_EQ(drv->num_buttons, 8);
	ASSERT_EQ(drv->num_fx, 2); /* steady + rainbow, per zone */
	ASSERT_TRUE((drv->caps & ALLOY_CAP_BATTERY) != 0);
	ASSERT_TRUE(drv->ops->battery != NULL);
	ASSERT_TRUE((drv->caps & ALLOY_CAP_HIGH_EFFICIENCY) != 0);
	ASSERT_TRUE(drv->ops->apply_high_efficiency != NULL);
	ASSERT_TRUE(drv->ops->apply_sleep != NULL);
	ASSERT_TRUE((drv->caps & ALLOY_CAP_PAIRING) != 0);
	ASSERT_TRUE(drv->ops->pair != NULL);
}

ALLOY_TEST(test_pair_stub_pending)
{
	struct alloy_device dev;
	const struct alloy_driver *drv = a3wl();

	memset(&dev, 0, sizeof(dev));
	dev.hid.fd = 42;
	dev.drv = drv;

	ASSERT_EQ(drv->ops->pair(&dev), ALLOY_PAIR_UNIMPLEMENTED);
}

ALLOY_TEST(test_dpi_packet)
{
	struct alloy_config cfg;
	uint8_t buf[ALLOY_HID_REPORT_SIZE];
	size_t len;

	a3wl()->config_defaults(a3wl(), &cfg);
	cfg.dpi_count = 5;
	cfg.dpi_active = 0;
	cfg.dpi[0][0] = 400;
	cfg.dpi[1][0] = 800;
	cfg.dpi[2][0] = 1200;
	cfg.dpi[3][0] = 2400;
	cfg.dpi[4][0] = 3200;

	len = a3wl_build_dpi(&cfg, buf);
	ASSERT_EQ(len, 8); /* cmd + count + active + 5 single-byte presets */
	ASSERT_EQ(buf[0], 0x6D);
	ASSERT_EQ(buf[1], 5); /* preset count */
	ASSERT_EQ(buf[2], 0); /* active, 0-based on the wire */
	/* TrueMove Air wire bytes (captured: 6d 05 00 04 09 0d 1b 26) */
	ASSERT_EQ(buf[3], 0x04); /* 400 */
	ASSERT_EQ(buf[4], 0x09); /* 800 */
	ASSERT_EQ(buf[5], 0x0D); /* 1200 */
	ASSERT_EQ(buf[6], 0x1B); /* 2400 */
	ASSERT_EQ(buf[7], 0x26); /* 3200 */

	/* active index is carried through, 0-based */
	cfg.dpi_active = 3;
	a3wl_build_dpi(&cfg, buf);
	ASSERT_EQ(buf[2], 3);

	/* boundaries of the TrueMove Air table */
	cfg.dpi_count = 1;
	cfg.dpi_active = 0;
	cfg.dpi[0][0] = 100;
	len = a3wl_build_dpi(&cfg, buf);
	ASSERT_EQ(len, 4);
	ASSERT_EQ(buf[3], 0x00);
	cfg.dpi[0][0] = 18000;
	a3wl_build_dpi(&cfg, buf);
	ASSERT_EQ(buf[3], 0xD6);

	/* out-of-range values clamp instead of overflowing the table */
	cfg.dpi[0][0] = 50;
	a3wl_build_dpi(&cfg, buf);
	ASSERT_EQ(buf[3], 0x00);
	cfg.dpi[0][0] = 60000;
	a3wl_build_dpi(&cfg, buf);
	ASSERT_EQ(buf[3], 0xD6);
}

ALLOY_TEST(test_polling_packet)
{
	struct alloy_config cfg;
	uint8_t buf[ALLOY_HID_REPORT_SIZE];
	static const struct {
		uint16_t hz;
		uint8_t wire;
	} cases[] = {
		{ 1000, 0x00 }, /* differs from the Rival 3 line */
		{ 500, 0x01 },
		{ 250, 0x02 },
		{ 125, 0x03 },
	};
	size_t i;

	a3wl()->config_defaults(a3wl(), &cfg);
	for (i = 0; i < ALLOY_ARRAY_SIZE(cases); i++) {
		cfg.polling_hz = cases[i].hz;
		ASSERT_EQ(a3wl_build_polling(&cfg, buf), 2);
		ASSERT_EQ(buf[0], 0x6B);
		ASSERT_EQ(buf[1], cases[i].wire);
	}
}

ALLOY_TEST(test_zone_color_packet)
{
	struct alloy_config cfg;
	uint8_t buf[ALLOY_HID_REPORT_SIZE];

	a3wl()->config_defaults(a3wl(), &cfg);
	cfg.zone_color[0] = (struct alloy_rgb){ 0xFF, 0x00, 0x00 };
	cfg.zone_color[1] = (struct alloy_rgb){ 0x11, 0x22, 0x33 };
	cfg.zone_color[2] = (struct alloy_rgb){ 0x44, 0x55, 0x66 };

	/* top zone: captured 61 01 00 ff 00 00 */
	ASSERT_EQ(a3wl_build_zone_color(&cfg, 0, buf), 6);
	ASSERT_EQ(buf[0], 0x61);
	ASSERT_EQ(buf[1], 0x01);
	ASSERT_EQ(buf[2], 0x00);
	ASSERT_EQ(buf[3], 0xFF);
	ASSERT_EQ(buf[4], 0x00);
	ASSERT_EQ(buf[5], 0x00);

	/* middle zone carries index 1 and its own triplet */
	a3wl_build_zone_color(&cfg, 1, buf);
	ASSERT_EQ(buf[2], 0x01);
	ASSERT_EQ(buf[3], 0x11);
	ASSERT_EQ(buf[5], 0x33);

	/* bottom zone carries index 2 */
	a3wl_build_zone_color(&cfg, 2, buf);
	ASSERT_EQ(buf[2], 0x02);
	ASSERT_EQ(buf[4], 0x55);
}

ALLOY_TEST(test_rainbow_packet)
{
	struct alloy_config cfg;
	uint8_t buf[ALLOY_HID_REPORT_SIZE];

	a3wl()->config_defaults(a3wl(), &cfg);

	/* defaults are all steady: no rainbow packet */
	cfg.zone_fx[0] = 0;
	cfg.zone_fx[1] = 0;
	cfg.zone_fx[2] = 0;
	ASSERT_EQ(a3wl_build_rainbow(&cfg, buf), 0);

	cfg.zone_fx[0] = 1;
	cfg.zone_fx[2] = 1;
	ASSERT_EQ(a3wl_build_rainbow(&cfg, buf), 2);
	ASSERT_EQ(buf[0], 0x62);
	ASSERT_EQ(buf[1], 0x05); /* bit0 | bit2 */

	/* all three zones on the rainbow */
	cfg.zone_fx[1] = 1;
	a3wl_build_rainbow(&cfg, buf);
	ASSERT_EQ(buf[1], 0x07);
}

ALLOY_TEST(test_reactive_packet)
{
	struct alloy_config cfg;
	uint8_t buf[ALLOY_HID_REPORT_SIZE];

	a3wl()->config_defaults(a3wl(), &cfg);

	cfg.reactive_enabled = 1;
	cfg.reactive_color = (struct alloy_rgb){ 0x12, 0x34, 0x56 };
	ASSERT_EQ(a3wl_build_reactive(&cfg, buf), 6);
	ASSERT_EQ(buf[0], 0x66);
	ASSERT_EQ(buf[1], 0x01); /* enable byte is mandatory */
	ASSERT_EQ(buf[2], 0x00);
	ASSERT_EQ(buf[3], 0x12);
	ASSERT_EQ(buf[4], 0x34);
	ASSERT_EQ(buf[5], 0x56);

	/* disabled: all-zero payload turns the effect off */
	cfg.reactive_enabled = 0;
	a3wl_build_reactive(&cfg, buf);
	ASSERT_EQ(buf[1], 0x00);
	ASSERT_EQ(buf[3], 0x00);
	ASSERT_EQ(buf[4], 0x00);
	ASSERT_EQ(buf[5], 0x00);
}

ALLOY_TEST(test_startup_packet)
{
	struct alloy_config cfg;
	uint8_t buf[ALLOY_HID_REPORT_SIZE];
	static const struct {
		uint8_t fx;
		uint8_t rainbow;
		uint8_t reactive;
	} cases[] = {
		{ ALLOY_STARTUP_OFF, 0, 0 },
		{ ALLOY_STARTUP_REACTIVE, 0, 1 },
		{ ALLOY_STARTUP_RAINBOW, 1, 0 },
		{ ALLOY_STARTUP_REACTIVE_RAINBOW, 1, 1 },
	};
	size_t i;

	a3wl()->config_defaults(a3wl(), &cfg);
	cfg.zone_fx[0] = 0;
	cfg.zone_fx[1] = 0;
	cfg.zone_fx[2] = 0;
	for (i = 0; i < ALLOY_ARRAY_SIZE(cases); i++) {
		cfg.startup_fx = cases[i].fx;
		ASSERT_EQ(a3wl_build_startup(&cfg, buf), 3);
		ASSERT_EQ(buf[0], 0x67);
		ASSERT_EQ(buf[1], cases[i].rainbow);
		ASSERT_EQ(buf[2], cases[i].reactive);
	}

	/* any rainbow zone forces the engine on regardless of startup choice */
	cfg.zone_fx[1] = 1;
	cfg.startup_fx = ALLOY_STARTUP_OFF;
	a3wl_build_startup(&cfg, buf);
	ASSERT_EQ(buf[1], 1);
}

ALLOY_TEST(test_brightness_packet)
{
	struct alloy_config cfg;
	uint8_t buf[ALLOY_HID_REPORT_SIZE];

	a3wl()->config_defaults(a3wl(), &cfg);

	/* 0-100% maps onto the 16-level (0x00-0x0F) illumination knob */
	cfg.brightness = 100;
	ASSERT_EQ(a3wl_build_brightness(&cfg, buf), 8);
	ASSERT_EQ(buf[0], 0x63);
	ASSERT_EQ(buf[1], 0x0F); /* full */
	ASSERT_EQ(buf[2], 0x01); /* apply flag */
	ASSERT_EQ(buf[3], 0x00); /* smart mode off */
	ASSERT_EQ(buf[5], 0x00); /* dim timer off */

	cfg.brightness = 0;
	a3wl_build_brightness(&cfg, buf);
	ASSERT_EQ(buf[1], 0x00);

	cfg.brightness = 50;
	a3wl_build_brightness(&cfg, buf);
	ASSERT_EQ(buf[1], 0x08);

	cfg.brightness = 255; /* clamps to 100% -> full */
	a3wl_build_brightness(&cfg, buf);
	ASSERT_EQ(buf[1], 0x0F);

	/* smart mode rides byte 3 of the same command */
	cfg.brightness = 100;
	cfg.illum_smart = 1;
	a3wl_build_brightness(&cfg, buf);
	ASSERT_EQ(buf[3], 0x01);

	/* dim timer: seconds -> 3-byte little-endian ms (30 s = 30000 = 0x7530) */
	cfg.illum_smart = 0;
	cfg.illum_dim_s = 30;
	a3wl_build_brightness(&cfg, buf);
	ASSERT_EQ(buf[5], 0x30);
	ASSERT_EQ(buf[6], 0x75);
	ASSERT_EQ(buf[7], 0x00);

	/* dim timer clamps to the 1200 s ceiling (1200 s = 1200000 = 0x124F80) */
	cfg.illum_dim_s = 5000;
	a3wl_build_brightness(&cfg, buf);
	ASSERT_EQ(buf[5], 0x80);
	ASSERT_EQ(buf[6], 0x4F);
	ASSERT_EQ(buf[7], 0x12);
}

ALLOY_TEST(test_sleep_packet)
{
	struct alloy_config cfg;
	uint8_t buf[ALLOY_HID_REPORT_SIZE];

	a3wl()->config_defaults(a3wl(), &cfg);

	/* 5 min: captured 69 e0 93 04 (0x000493E0 = 300000 ms) */
	cfg.sleep_min = 5;
	ASSERT_EQ(a3wl_build_sleep(&cfg, buf), 4);
	ASSERT_EQ(buf[0], 0x69);
	ASSERT_EQ(buf[1], 0xE0);
	ASSERT_EQ(buf[2], 0x93);
	ASSERT_EQ(buf[3], 0x04);

	/* 20 min ceiling: 0x00124F80 = 1200000 ms -> 69 80 4f 12 */
	cfg.sleep_min = 20;
	a3wl_build_sleep(&cfg, buf);
	ASSERT_EQ(buf[1], 0x80);
	ASSERT_EQ(buf[2], 0x4F);
	ASSERT_EQ(buf[3], 0x12);

	/* 0 = never: 69 00 00 00 */
	cfg.sleep_min = 0;
	a3wl_build_sleep(&cfg, buf);
	ASSERT_EQ(buf[1], 0x00);
	ASSERT_EQ(buf[2], 0x00);
	ASSERT_EQ(buf[3], 0x00);

	/* out-of-range minutes clamp to the 20 min ceiling */
	cfg.sleep_min = 200;
	a3wl_build_sleep(&cfg, buf);
	ASSERT_EQ(buf[1], 0x80);
	ASSERT_EQ(buf[2], 0x4F);
	ASSERT_EQ(buf[3], 0x12);
}

ALLOY_TEST(test_buttons_packet)
{
	struct alloy_config cfg;
	uint8_t buf[ALLOY_HID_REPORT_SIZE];
	size_t len;

	a3wl()->config_defaults(a3wl(), &cfg);
	len = a3wl_build_buttons(&cfg, buf);
	ASSERT_EQ(len, 41);
	ASSERT_EQ(buf[0], 0x6A);
	/* factory mapping: fields at 5-byte offsets */
	ASSERT_EQ(buf[1 + 0x00], 0x01);
	ASSERT_EQ(buf[1 + 0x05], 0x02);
	ASSERT_EQ(buf[1 + 0x0A], 0x03);
	ASSERT_EQ(buf[1 + 0x0F], 0x04);
	ASSERT_EQ(buf[1 + 0x14], 0x05);
	ASSERT_EQ(buf[1 + 0x19], 0x30); /* CPI toggle */
	ASSERT_EQ(buf[1 + 0x1E], 0x31); /* scroll up */
	ASSERT_EQ(buf[1 + 0x23], 0x32); /* scroll down */

	/* rebind button 4 to keyboard 'a' (usage 0x04) */
	cfg.buttons[3].type = ALLOY_ACT_KEYBOARD;
	cfg.buttons[3].value = 0x04;
	a3wl_build_buttons(&cfg, buf);
	ASSERT_EQ(buf[1 + 0x0F], 0x51);
	ASSERT_EQ(buf[1 + 0x0F + 1], 0x04);

	/* disable button 5 */
	cfg.buttons[4].type = ALLOY_ACT_DISABLED;
	a3wl_build_buttons(&cfg, buf);
	ASSERT_EQ(buf[1 + 0x14], 0x00);
}

ALLOY_TEST(test_high_efficiency_packet)
{
	struct alloy_config cfg;
	uint8_t buf[ALLOY_HID_REPORT_SIZE];

	a3wl()->config_defaults(a3wl(), &cfg);

	/* enable flag: 0x68 0x01 (captured with the mode toggled on) */
	cfg.high_efficiency = 1;
	ASSERT_EQ(a3wl_build_high_efficiency(&cfg, buf), 2);
	ASSERT_EQ(buf[0], 0x68);
	ASSERT_EQ(buf[1], 0x01);

	cfg.high_efficiency = 0;
	a3wl_build_high_efficiency(&cfg, buf);
	ASSERT_EQ(buf[1], 0x00);
}

ALLOY_TEST(test_high_efficiency_bundle)
{
	struct alloy_device dev;
	const struct alloy_driver *drv = a3wl();
	struct alloy_config cfg;

	memset(&dev, 0, sizeof(dev));
	dev.hid.fd = 42;
	dev.drv = drv;
	drv->config_defaults(drv, &cfg);
	cfg.polling_hz = 1000;
	cfg.brightness = 100;

	/*
	 * Enabling mirrors the GG bundle:
	 * flag on, polling forced to 125 Hz (0x6B 0x03) and LEDs blanked (0x63 level 0)
	 */
	mock_hid_reset();
	cfg.high_efficiency = 1;
	ASSERT_EQ(drv->ops->apply_high_efficiency(&dev, &cfg), 0);
	ASSERT_EQ(mock_hid.num_cmds, 3);
	ASSERT_EQ(mock_hid.cmds[0].payload[0], 0x68);
	ASSERT_EQ(mock_hid.cmds[0].payload[1], 0x01);
	ASSERT_EQ(mock_hid.cmds[1].payload[0], 0x6B);
	ASSERT_EQ(mock_hid.cmds[1].payload[1], 0x03); /* 125 Hz */
	ASSERT_EQ(mock_hid.cmds[2].payload[0], 0x63);
	ASSERT_EQ(mock_hid.cmds[2].payload[1], 0x00); /* brightness 0 */

	/* disabling clears the flag and restores polling/brightness from cfg */
	mock_hid_reset();
	cfg.high_efficiency = 0;
	ASSERT_EQ(drv->ops->apply_high_efficiency(&dev, &cfg), 0);
	ASSERT_EQ(mock_hid.num_cmds, 3);
	ASSERT_EQ(mock_hid.cmds[0].payload[0], 0x68);
	ASSERT_EQ(mock_hid.cmds[0].payload[1], 0x00);
	ASSERT_EQ(mock_hid.cmds[1].payload[0], 0x6B);
	ASSERT_EQ(mock_hid.cmds[1].payload[1], 0x00); /* 1000 Hz restored */
	ASSERT_EQ(mock_hid.cmds[2].payload[0], 0x63);
	ASSERT_EQ(mock_hid.cmds[2].payload[1],
		  0x0F); /* full brightness restored */
}

ALLOY_TEST(test_cpi_level_event)
{
	/* exact notification captured on hardware: 5 levels, toggling 0<->1 */
	uint8_t evt[64] = { 0xAD, 0x02, 0x01, 0x04, 0x12 };
	struct alloy_config cfg;

	a3wl()->config_defaults(a3wl(), &cfg);
	cfg.dpi_count = 2;
	cfg.dpi_active = 0;

	/* hardware switched to level 2 (0-based 1) */
	ASSERT_EQ(a3wl_parse_event(evt, sizeof(evt), &cfg), 1);
	ASSERT_EQ(cfg.dpi_active, 1);

	/* same level again: no change to report */
	ASSERT_EQ(a3wl_parse_event(evt, sizeof(evt), &cfg), 0);
	ASSERT_EQ(cfg.dpi_active, 1);

	/* not the CPI notification */
	evt[0] = 0x61;
	ASSERT_EQ(a3wl_parse_event(evt, sizeof(evt), &cfg), 0);
	evt[0] = 0xAD;

	/* truncated report */
	ASSERT_EQ(a3wl_parse_event(evt, 2, &cfg), 0);

	/* active beyond what the host config knows: ignored, not clamped */
	evt[1] = 0x05;
	evt[2] = 0x04;
	ASSERT_EQ(a3wl_parse_event(evt, sizeof(evt), &cfg), 0);
	ASSERT_EQ(cfg.dpi_active, 1);
}

ALLOY_TEST(test_battery_decode)
{
	struct alloy_device dev;
	const struct alloy_driver *drv = a3wl();
	int pct = -1;
	int charging = -1;

	memset(&dev, 0, sizeof(dev));
	dev.hid.fd = 42;
	dev.drv = drv;

	/* captured: d2 14 -> byte 0x14 = level 20 -> (20-1)*5 = 95%, not charging */
	mock_hid_reset();
	mock_hid.next_response[0] = 0xD2;
	mock_hid.next_response[1] = 0x14;
	mock_hid.next_response_len = 2;
	ASSERT_EQ(drv->ops->battery(&dev, &pct, &charging), 0);
	ASSERT_EQ(pct, 95);
	ASSERT_EQ(charging, 0);
	/* query is the wireless-flagged 0xD2 */
	ASSERT_EQ(mock_hid.cmds[0].payload[0], 0xD2);

	/* charging bit (0x80) set, same level */
	mock_hid_reset();
	mock_hid.next_response[0] = 0xD2;
	mock_hid.next_response[1] = 0x94; /* 0x80 | 0x14 */
	mock_hid.next_response_len = 2;
	ASSERT_EQ(drv->ops->battery(&dev, &pct, &charging), 0);
	ASSERT_EQ(pct, 95);
	ASSERT_EQ(charging, 1);

	/* top of the scale (level 21) clamps to 100% */
	mock_hid_reset();
	mock_hid.next_response[0] = 0xD2;
	mock_hid.next_response[1] = 0x15;
	mock_hid.next_response_len = 2;
	ASSERT_EQ(drv->ops->battery(&dev, &pct, &charging), 0);
	ASSERT_EQ(pct, 100);

	/* idle receiver (no mouse linked) answers 40 ff -> not a valid level */
	mock_hid_reset();
	mock_hid.next_response[0] = 0x40;
	mock_hid.next_response[1] = 0xFF;
	mock_hid.next_response_len = 2;
	ASSERT_EQ(drv->ops->battery(&dev, &pct, &charging), -1);
}
