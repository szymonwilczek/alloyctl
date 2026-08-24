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
#include "hid.h"
#include "lib/mouse.h"
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
size_t a3wl_build_sleep(const struct alloy_config *cfg, uint8_t *buf);

int a3wl_parse_event(struct alloy_device *dev, const uint8_t *buf, size_t len,
		     struct alloy_config *cfg);

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
	const struct alloy_hid_params *hid = drv->transport_data;
	const struct alloy_devinfo *info = alloy_devinfo(drv);
	const struct alloy_mouse_info *mouse = alloy_mouse_info(drv);

	ASSERT_EQ(hid->interface, 3);
	ASSERT_EQ(hid->event_interface, 4);
	ASSERT_EQ(mouse->dpi.min, 100);
	ASSERT_EQ(mouse->dpi.max, 18000);
	ASSERT_EQ(info->num_zones, 3);
	ASSERT_EQ(mouse->num_buttons, 8);
	ASSERT_EQ(info->num_fx, 2); /* steady + rainbow, per zone */
	ASSERT_TRUE((info->caps & ALLOY_CAP_BATTERY) != 0);
	ASSERT_TRUE(mouse->battery != NULL);
	ASSERT_TRUE((info->caps & ALLOY_CAP_ACCEL) != 0);
	ASSERT_TRUE((info->caps & ALLOY_CAP_ANGLE_SNAPPING) != 0);
	ASSERT_TRUE(alloy_driver_step(drv, ALLOY_STEP_SLEEP) != NULL);
	ASSERT_TRUE((info->caps & ALLOY_CAP_PAIRING) != 0);
	ASSERT_TRUE(mouse->pair != NULL);
}

ALLOY_TEST(test_pair_sends_bind)
{
	const struct alloy_driver *drv = a3wl();
	const struct alloy_mouse_info *mouse = alloy_mouse_info(drv);
	struct alloy_device dev = { 0 };

	alloy_device_open_id(&dev, drv->vendor_id, drv->product_id);

	mock_hid_reset();
	ASSERT_EQ(mouse->pair(&dev), 0);
	ASSERT_EQ(mock_hid.num_cmds, 3);
	ASSERT_EQ(mock_hid.cmds[0].len, 1);
	ASSERT_EQ(mock_hid.cmds[0].payload[0], 0x3B);
	ASSERT_EQ(mock_hid.cmds[1].payload[0], 0x11);
	ASSERT_EQ(mock_hid.cmds[2].payload[0], 0x01);

	mock_hid_reset();
	mock_hid.fail_cmds = 1;
	ASSERT_TRUE(mouse->pair(&dev) < 0);

	alloy_device_close(&dev);
}

ALLOY_TEST(test_dpi_packet)
{
	const struct alloy_driver *drv = a3wl();
	struct alloy_config *cfg = alloy_config_alloc(drv);
	uint8_t buf[ALLOY_HID_REPORT_SIZE];
	size_t len;

	alloy_config_defaults(drv, cfg);
	struct alloy_mouse_config *m = alloy_mouse_cfg(cfg);
	m->dpi_count = 5;
	m->dpi_active = 0;
	m->dpi[0][0] = 400;
	m->dpi[1][0] = 800;
	m->dpi[2][0] = 1200;
	m->dpi[3][0] = 2400;
	m->dpi[4][0] = 3200;

	len = a3wl_build_dpi(cfg, buf);
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
	m->dpi_active = 3;
	a3wl_build_dpi(cfg, buf);
	ASSERT_EQ(buf[2], 3);

	/* boundaries of the TrueMove Air table */
	m->dpi_count = 1;
	m->dpi_active = 0;
	m->dpi[0][0] = 100;
	len = a3wl_build_dpi(cfg, buf);
	ASSERT_EQ(len, 4);
	ASSERT_EQ(buf[3], 0x00);
	m->dpi[0][0] = 18000;
	a3wl_build_dpi(cfg, buf);
	ASSERT_EQ(buf[3], 0xD6);

	/* out-of-range values clamp instead of overflowing the table */
	m->dpi[0][0] = 50;
	a3wl_build_dpi(cfg, buf);
	ASSERT_EQ(buf[3], 0x00);
	m->dpi[0][0] = 60000;
	a3wl_build_dpi(cfg, buf);
	ASSERT_EQ(buf[3], 0xD6);

	alloy_config_free(cfg);
}

ALLOY_TEST(test_polling_packet)
{
	const struct alloy_driver *drv = a3wl();
	struct alloy_config *cfg = alloy_config_alloc(drv);
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

	alloy_config_defaults(drv, cfg);
	struct alloy_mouse_config *m = alloy_mouse_cfg(cfg);
	for (i = 0; i < ALLOY_ARRAY_SIZE(cases); i++) {
		m->dev.polling_hz = cases[i].hz;
		ASSERT_EQ(a3wl_build_polling(cfg, buf), 2);
		ASSERT_EQ(buf[0], 0x6B);
		ASSERT_EQ(buf[1], cases[i].wire);
	}

	alloy_config_free(cfg);
}

ALLOY_TEST(test_zone_color_packet)
{
	const struct alloy_driver *drv = a3wl();
	struct alloy_config *cfg = alloy_config_alloc(drv);
	uint8_t buf[ALLOY_HID_REPORT_SIZE];

	alloy_config_defaults(drv, cfg);
	struct alloy_mouse_config *m = alloy_mouse_cfg(cfg);
	m->dev.zone_color[0] = (struct alloy_rgb){ 0xFF, 0x00, 0x00 };
	m->dev.zone_color[1] = (struct alloy_rgb){ 0x11, 0x22, 0x33 };
	m->dev.zone_color[2] = (struct alloy_rgb){ 0x44, 0x55, 0x66 };

	/* top zone: captured 61 01 00 ff 00 00 */
	ASSERT_EQ(a3wl_build_zone_color(cfg, 0, buf), 6);
	ASSERT_EQ(buf[0], 0x61);
	ASSERT_EQ(buf[1], 0x01);
	ASSERT_EQ(buf[2], 0x00);
	ASSERT_EQ(buf[3], 0xFF);
	ASSERT_EQ(buf[4], 0x00);
	ASSERT_EQ(buf[5], 0x00);

	/* middle zone carries index 1 and its own triplet */
	a3wl_build_zone_color(cfg, 1, buf);
	ASSERT_EQ(buf[2], 0x01);
	ASSERT_EQ(buf[3], 0x11);
	ASSERT_EQ(buf[5], 0x33);

	/* bottom zone carries index 2 */
	a3wl_build_zone_color(cfg, 2, buf);
	ASSERT_EQ(buf[2], 0x02);
	ASSERT_EQ(buf[4], 0x55);

	alloy_config_free(cfg);
}

ALLOY_TEST(test_rainbow_packet)
{
	const struct alloy_driver *drv = a3wl();
	struct alloy_config *cfg = alloy_config_alloc(drv);
	uint8_t buf[ALLOY_HID_REPORT_SIZE];

	alloy_config_defaults(drv, cfg);
	struct alloy_mouse_config *m = alloy_mouse_cfg(cfg);

	/* defaults are all steady: no rainbow packet */
	m->dev.zone_fx[0] = 0;
	m->dev.zone_fx[1] = 0;
	m->dev.zone_fx[2] = 0;
	ASSERT_EQ(a3wl_build_rainbow(cfg, buf), 0);

	m->dev.zone_fx[0] = 1;
	m->dev.zone_fx[2] = 1;
	ASSERT_EQ(a3wl_build_rainbow(cfg, buf), 2);
	ASSERT_EQ(buf[0], 0x62);
	ASSERT_EQ(buf[1], 0x05); /* bit0 | bit2 */

	/* all three zones on the rainbow */
	m->dev.zone_fx[1] = 1;
	a3wl_build_rainbow(cfg, buf);
	ASSERT_EQ(buf[1], 0x07);

	alloy_config_free(cfg);
}

ALLOY_TEST(test_reactive_packet)
{
	const struct alloy_driver *drv = a3wl();
	struct alloy_config *cfg = alloy_config_alloc(drv);
	uint8_t buf[ALLOY_HID_REPORT_SIZE];

	alloy_config_defaults(drv, cfg);
	struct alloy_mouse_config *m = alloy_mouse_cfg(cfg);

	m->reactive_enabled = 1;
	m->reactive_color = (struct alloy_rgb){ 0x12, 0x34, 0x56 };
	ASSERT_EQ(a3wl_build_reactive(cfg, buf), 6);
	ASSERT_EQ(buf[0], 0x66);
	ASSERT_EQ(buf[1], 0x01); /* enable byte is mandatory */
	ASSERT_EQ(buf[2], 0x00);
	ASSERT_EQ(buf[3], 0x12);
	ASSERT_EQ(buf[4], 0x34);
	ASSERT_EQ(buf[5], 0x56);

	/* disabled: all-zero payload turns the effect off */
	m->reactive_enabled = 0;
	a3wl_build_reactive(cfg, buf);
	ASSERT_EQ(buf[1], 0x00);
	ASSERT_EQ(buf[3], 0x00);
	ASSERT_EQ(buf[4], 0x00);
	ASSERT_EQ(buf[5], 0x00);

	alloy_config_free(cfg);
}

ALLOY_TEST(test_startup_packet)
{
	const struct alloy_driver *drv = a3wl();
	struct alloy_config *cfg = alloy_config_alloc(drv);
	uint8_t buf[ALLOY_HID_REPORT_SIZE];

	alloy_config_defaults(drv, cfg);
	struct alloy_mouse_config *m = alloy_mouse_cfg(cfg);
	m->dev.zone_fx[0] = 0;
	m->dev.zone_fx[1] = 0;
	m->dev.zone_fx[2] = 0;
	m->reactive_enabled = 0;

	ASSERT_EQ(a3wl_build_startup(cfg, buf), 3);
	ASSERT_EQ(buf[0], 0x67);
	ASSERT_EQ(buf[1], 0);
	ASSERT_EQ(buf[2], 0);

	m->reactive_enabled = 1;
	ASSERT_EQ(a3wl_build_startup(cfg, buf), 3);
	ASSERT_EQ(buf[1], 0);
	ASSERT_EQ(buf[2], 1);

	/* any rainbow zone forces rainbow on */
	m->dev.zone_fx[1] = 1;
	a3wl_build_startup(cfg, buf);
	ASSERT_EQ(buf[1], 1);
	ASSERT_EQ(buf[2], 1);

	alloy_config_free(cfg);
}

ALLOY_TEST(test_brightness_packet)
{
	const struct alloy_driver *drv = a3wl();
	struct alloy_config *cfg = alloy_config_alloc(drv);
	uint8_t buf[ALLOY_HID_REPORT_SIZE];

	alloy_config_defaults(drv, cfg);
	struct alloy_mouse_config *m = alloy_mouse_cfg(cfg);

	/* 0-100% maps onto the 16-level (0x00-0x0F) illumination knob */
	m->dev.brightness = 100;
	ASSERT_EQ(a3wl_build_brightness(cfg, buf), 8);
	ASSERT_EQ(buf[0], 0x63);
	ASSERT_EQ(buf[1], 0x0F); /* full */
	ASSERT_EQ(buf[2], 0x01); /* apply flag */
	ASSERT_EQ(buf[3], 0x00); /* smart mode off */
	ASSERT_EQ(buf[5], 0x00); /* dim timer off */

	m->dev.brightness = 0;
	a3wl_build_brightness(cfg, buf);
	ASSERT_EQ(buf[1], 0x00);

	m->dev.brightness = 50;
	a3wl_build_brightness(cfg, buf);
	ASSERT_EQ(buf[1], 0x08);

	m->dev.brightness = 255; /* clamps to 100% -> full */
	a3wl_build_brightness(cfg, buf);
	ASSERT_EQ(buf[1], 0x0F);

	/* smart mode rides byte 3 of the same command */
	m->dev.brightness = 100;
	m->illum_smart = 1;
	a3wl_build_brightness(cfg, buf);
	ASSERT_EQ(buf[3], 0x01);

	/* dim timer: seconds -> 3-byte little-endian ms (30 s = 30000 = 0x7530) */
	m->illum_smart = 0;
	m->illum_dim_s = 30;
	a3wl_build_brightness(cfg, buf);
	ASSERT_EQ(buf[5], 0x30);
	ASSERT_EQ(buf[6], 0x75);
	ASSERT_EQ(buf[7], 0x00);

	/* dim timer clamps to the 1200 s ceiling (1200 s = 1200000 = 0x124F80) */
	m->illum_dim_s = 5000;
	a3wl_build_brightness(cfg, buf);
	ASSERT_EQ(buf[5], 0x80);
	ASSERT_EQ(buf[6], 0x4F);
	ASSERT_EQ(buf[7], 0x12);

	alloy_config_free(cfg);
}

ALLOY_TEST(test_sleep_packet)
{
	const struct alloy_driver *drv = a3wl();
	struct alloy_config *cfg = alloy_config_alloc(drv);
	uint8_t buf[ALLOY_HID_REPORT_SIZE];

	alloy_config_defaults(drv, cfg);
	struct alloy_mouse_config *m = alloy_mouse_cfg(cfg);

	/* 5 min: captured 69 e0 93 04 (0x000493E0 = 300000 ms) */
	m->sleep_min = 5;
	ASSERT_EQ(a3wl_build_sleep(cfg, buf), 4);
	ASSERT_EQ(buf[0], 0x69);
	ASSERT_EQ(buf[1], 0xE0);
	ASSERT_EQ(buf[2], 0x93);
	ASSERT_EQ(buf[3], 0x04);

	/* 20 min ceiling: 0x00124F80 = 1200000 ms -> 69 80 4f 12 */
	m->sleep_min = 20;
	a3wl_build_sleep(cfg, buf);
	ASSERT_EQ(buf[1], 0x80);
	ASSERT_EQ(buf[2], 0x4F);
	ASSERT_EQ(buf[3], 0x12);

	/* 0 = never: 69 00 00 00 */
	m->sleep_min = 0;
	a3wl_build_sleep(cfg, buf);
	ASSERT_EQ(buf[1], 0x00);
	ASSERT_EQ(buf[2], 0x00);
	ASSERT_EQ(buf[3], 0x00);

	/* out-of-range minutes clamp to the 20 min ceiling */
	m->sleep_min = 200;
	a3wl_build_sleep(cfg, buf);
	ASSERT_EQ(buf[1], 0x80);
	ASSERT_EQ(buf[2], 0x4F);
	ASSERT_EQ(buf[3], 0x12);

	alloy_config_free(cfg);
}

ALLOY_TEST(test_buttons_packet)
{
	const struct alloy_driver *drv = a3wl();
	struct alloy_config *cfg = alloy_config_alloc(drv);
	uint8_t buf[ALLOY_HID_REPORT_SIZE];
	size_t len;

	alloy_config_defaults(drv, cfg);
	struct alloy_mouse_config *m = alloy_mouse_cfg(cfg);
	len = a3wl_build_buttons(cfg, buf);
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
	m->buttons[3].type = ALLOY_ACT_KEYBOARD;
	m->buttons[3].value = 0x04;
	a3wl_build_buttons(cfg, buf);
	ASSERT_EQ(buf[1 + 0x0F], 0x51);
	ASSERT_EQ(buf[1 + 0x0F + 1], 0x04);

	/* disable button 5 */
	m->buttons[4].type = ALLOY_ACT_DISABLED;
	a3wl_build_buttons(cfg, buf);
	ASSERT_EQ(buf[1 + 0x14], 0x00);

	alloy_config_free(cfg);
}

ALLOY_TEST(test_cpi_level_event)
{
	const struct alloy_driver *drv = a3wl();
	struct alloy_device dev = { 0 };
	struct alloy_config *cfg = alloy_config_alloc(drv);
	uint8_t evt[64] = { 0xAD, 0x02, 0x01, 0x04, 0x12 };

	alloy_device_open_id(&dev, drv->vendor_id, drv->product_id);
	alloy_config_defaults(drv, cfg);
	struct alloy_mouse_config *m = alloy_mouse_cfg(cfg);
	m->dpi_count = 2;
	m->dpi_active = 0;

	/* hardware switched to level 2 (0-based 1) */
	ASSERT_EQ(a3wl_parse_event(&dev, evt, sizeof(evt), cfg), 1);
	ASSERT_EQ(m->dpi_active, 1);

	/* same level again: no change to report */
	ASSERT_EQ(a3wl_parse_event(&dev, evt, sizeof(evt), cfg), 0);
	ASSERT_EQ(m->dpi_active, 1);

	/* not the CPI notification */
	evt[0] = 0x61;
	ASSERT_EQ(a3wl_parse_event(&dev, evt, sizeof(evt), cfg), 0);
	evt[0] = 0xAD;

	/* truncated report */
	ASSERT_EQ(a3wl_parse_event(&dev, evt, 2, cfg), 0);

	/* active beyond what the host config knows: ignored, not clamped */
	evt[1] = 0x05;
	evt[2] = 0x04;
	ASSERT_EQ(a3wl_parse_event(&dev, evt, sizeof(evt), cfg), 0);
	ASSERT_EQ(m->dpi_active, 1);

	alloy_device_close(&dev);
	alloy_config_free(cfg);
}

ALLOY_TEST(test_battery_decode)
{
	const struct alloy_driver *drv = a3wl();
	const struct alloy_mouse_info *mouse = alloy_mouse_info(drv);
	struct alloy_device dev = { 0 };
	int pct = -1;
	int charging = -1;

	alloy_device_open_id(&dev, drv->vendor_id, drv->product_id);

	/* captured: d2 14 -> byte 0x14 = level 20 -> (20-1)*5 = 95%, not charging */
	mock_hid_reset();
	mock_hid.next_response[0] = 0xD2;
	mock_hid.next_response[1] = 0x14;
	mock_hid.next_response_len = 2;
	ASSERT_EQ(mouse->battery(&dev, &pct, &charging), 0);
	ASSERT_EQ(pct, 95);
	ASSERT_EQ(charging, 0);
	/* query is the wireless-flagged 0xD2 */
	ASSERT_EQ(mock_hid.cmds[0].payload[0], 0xD2);

	/* charging bit (0x80) set, same level */
	mock_hid_reset();
	mock_hid.next_response[0] = 0xD2;
	mock_hid.next_response[1] = 0x94; /* 0x80 | 0x14 */
	mock_hid.next_response_len = 2;
	ASSERT_EQ(mouse->battery(&dev, &pct, &charging), 0);
	ASSERT_EQ(pct, 95);
	ASSERT_EQ(charging, 1);

	/* top of the scale (level 21) clamps to 100% */
	mock_hid_reset();
	mock_hid.next_response[0] = 0xD2;
	mock_hid.next_response[1] = 0x15;
	mock_hid.next_response_len = 2;
	ASSERT_EQ(mouse->battery(&dev, &pct, &charging), 0);
	ASSERT_EQ(pct, 100);

	/* idle receiver (no mouse linked) answers 40 ff -> not a valid level */
	mock_hid_reset();
	mock_hid.next_response[0] = 0x40;
	mock_hid.next_response[1] = 0xFF;
	mock_hid.next_response_len = 2;
	ASSERT_EQ(mouse->battery(&dev, &pct, &charging), -1);

	alloy_device_close(&dev);
}
