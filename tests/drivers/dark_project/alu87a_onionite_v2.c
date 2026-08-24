/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Dark Project ALU87A Onionite V2 (342D:E40F) driver tests.
 *
 * Exercises the pure packet builders against the exact byte sequences verified on hardware.
 *
 * Protocol reference: Documentation/protocol/dark-project-alu87a-onionite-v2.rst.
 */
#include <stdlib.h>
#include <string.h>

#include "dark_project/darkproject_common.h"
#include "driver.h"
#include "hid.h"
#include "lib/keyboard.h"
#include "mock_hid.h"
#include "test.h"

/* Pure packet builders from ALU87A driver */
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

static const struct alloy_driver *darkproject(void)
{
	const struct alloy_driver *drv = alloy_driver_find(0x342d, 0xe40f);

	if (!drv) {
		printf("FAIL: Dark Project ALU87A driver not registered\n");
		exit(1);
	}
	return drv;
}

ALLOY_TEST(test_darkproject_registry)
{
	const struct alloy_driver *drv = darkproject();
	const struct alloy_hid_params *hid = drv->transport_data;
	const struct alloy_devinfo *info = alloy_devinfo(drv);
	const struct alloy_keyboard_info *kbd = alloy_keyboard_info(drv);

	ASSERT_EQ(drv->vendor_id, 0x342d);
	ASSERT_EQ(drv->product_id, 0xe40f);
	ASSERT_TRUE(strcmp(drv->kind, "keyboard") == 0);
	ASSERT_EQ(hid->interface, 2);
	ASSERT_EQ(hid->event_interface, 1);
	ASSERT_EQ(hid->report_size, 256);
	ASSERT_EQ(info->num_zones, 1);
	ASSERT_EQ(info->num_fx, 15);
	ASSERT_EQ(kbd->num_profiles, 3);
	ASSERT_EQ(info->num_polling_rates, 4);
	ASSERT_TRUE(info->caps & ALLOY_CAP_COLOR);
	ASSERT_TRUE(info->caps & ALLOY_CAP_BRIGHTNESS);
	ASSERT_TRUE(info->caps & ALLOY_CAP_FX_GLOBAL);
	ASSERT_TRUE(info->caps & ALLOY_CAP_FX_SPEED);
	ASSERT_TRUE(info->caps & ALLOY_CAP_FX_REACTIVE);
	ASSERT_TRUE(info->caps & ALLOY_CAP_SNAP_TAP);
	ASSERT_TRUE(info->caps & ALLOY_CAP_PROFILE);
}

ALLOY_TEST(test_darkproject_read_profile_packets)
{
	uint8_t buf[256];
	size_t len;

	/* Profile 0 (Profile 1 on hardware) */
	len = darkproject_build_read_profile(0, buf);
	ASSERT_EQ(len, 256);
	ASSERT_EQ(buf[0], 0x07);
	ASSERT_EQ(buf[1], 0x82);
	ASSERT_EQ(buf[2], 0x01);

	/* Profile 2 (Profile 3 on hardware) */
	len = darkproject_build_read_profile(2, buf);
	ASSERT_EQ(len, 256);
	ASSERT_EQ(buf[0], 0x07);
	ASSERT_EQ(buf[1], 0x82);
	ASSERT_EQ(buf[2], 0x03);
}

ALLOY_TEST(test_darkproject_switch_profile_packets)
{
	uint8_t buf[256];
	size_t len;

	len = darkproject_build_switch_profile(0, buf);
	ASSERT_EQ(len, 256);
	ASSERT_EQ(buf[0], 0x07);
	ASSERT_EQ(buf[1], 0x01);
	ASSERT_EQ(buf[2], 0x01);

	len = darkproject_build_switch_profile(1, buf);
	ASSERT_EQ(len, 256);
	ASSERT_EQ(buf[0], 0x07);
	ASSERT_EQ(buf[1], 0x01);
	ASSERT_EQ(buf[2], 0x02);
}

ALLOY_TEST(test_darkproject_lighting_packets)
{
	uint8_t buf[256];
	size_t len;

	/* Solid color (Effect index 5 = SOLID):
	 * Cyan (0, 255, 128) at 100% brightness,
	 * speed 4 -> hardware byte 2 */
	len = alu87a_build_lighting(NULL, 5, 100, 4, 0, 0, 0, 0, 255, 128, buf);
	ASSERT_EQ(len, 256);
	ASSERT_EQ(buf[0], 0x07);
	ASSERT_EQ(buf[1], 0x02);
	ASSERT_EQ(buf[1 + 8], 5);
	ASSERT_EQ(buf[1 + 9 + 5], 4);
	ASSERT_EQ(buf[1 + 23 + 5], 2);
	ASSERT_EQ(buf[1 + 37 + 5], 0);
	ASSERT_EQ(buf[1 + 58], 0);
	ASSERT_EQ(buf[1 + 66], 255);
	ASSERT_EQ(buf[1 + 74], 128);

	/* Wave (Effect index 0 = WAVE):
	 * MultiColor enabled, 50% brightness,
	 * speed 5 (Fast) -> hardware byte 1,
	 * direction 2 (down) */
	len = alu87a_build_lighting(NULL, 0, 50, 5, 1, 2, 0, 255, 0, 0, buf);
	ASSERT_EQ(len, 256);
	ASSERT_EQ(buf[0], 0x07);
	ASSERT_EQ(buf[1], 0x02);
	ASSERT_EQ(buf[1 + 8], 0);
	ASSERT_EQ(buf[1 + 9 + 0], 2);
	ASSERT_EQ(buf[1 + 23 + 0], 1);
	ASSERT_EQ(buf[1 + 37 + 0], 8);
	ASSERT_EQ(buf[1 + 51], 2);

	/* Custom Zone (Effect index 14 = CUSTOM):
	 * Cust2 (Letters) -> custom_index 1 -> byte 52 is 2 */
	len = alu87a_build_lighting(NULL, 14, 100, 3, 0, 0, 1, 255, 255, 255,
				    buf);
	ASSERT_EQ(len, 256);
	ASSERT_EQ(buf[0], 0x07);
	ASSERT_EQ(buf[1], 0x02);
	ASSERT_EQ(buf[1 + 8], 14);
	ASSERT_EQ(buf[1 + 52], 2);
}

ALLOY_TEST(test_darkproject_polling_packets)
{
	uint8_t buf[256];
	size_t len;

	len = alu87a_build_polling(1000, NULL, buf);
	ASSERT_EQ(len, 256);
	ASSERT_EQ(buf[0], 0x07);
	ASSERT_EQ(buf[1], 0x02);
	ASSERT_EQ(buf[1 + 7], 4);

	len = alu87a_build_polling(500, NULL, buf);
	ASSERT_EQ(len, 256);
	ASSERT_EQ(buf[0], 0x07);
	ASSERT_EQ(buf[1], 0x02);
	ASSERT_EQ(buf[1 + 7], 3);

	len = alu87a_build_polling(250, NULL, buf);
	ASSERT_EQ(len, 256);
	ASSERT_EQ(buf[0], 0x07);
	ASSERT_EQ(buf[1], 0x02);
	ASSERT_EQ(buf[1 + 7], 2);

	len = alu87a_build_polling(125, NULL, buf);
	ASSERT_EQ(len, 256);
	ASSERT_EQ(buf[0], 0x07);
	ASSERT_EQ(buf[1], 0x02);
	ASSERT_EQ(buf[1 + 7], 1);
}

ALLOY_TEST(test_darkproject_snap_tap_packets)
{
	struct alloy_snap_tap_group groups[2] = {
		{ .mode = 0, .key1 = 0x04, .key2 = 0x07 }, /* A / D */
		{ .mode = 2,
		  .key1 = 0x1A,
		  .key2 = 0x16 }, /* W / S (Key 2 Priority) */
	};
	uint8_t buf[256];
	size_t len;

	/* Enable Snap Tap on Profile 1 with 2 groups */
	len = darkproject_build_snap_tap(1, 1, 2, groups, buf);
	ASSERT_EQ(len, 256);
	ASSERT_EQ(buf[0], 0x07);
	ASSERT_EQ(buf[1], 0x09);
	ASSERT_EQ(buf[2], 1);
	ASSERT_EQ(buf[3], 1);
	ASSERT_EQ(buf[8 + 0], 0);
	ASSERT_EQ(buf[8 + 1], 0x04);
	ASSERT_EQ(buf[8 + 2], 0x07);
	ASSERT_EQ(buf[8 + 3], 2);
	ASSERT_EQ(buf[8 + 4], 0x1A);
	ASSERT_EQ(buf[8 + 5], 0x16);

	/* Disable Snap Tap on Profile 2 */
	len = darkproject_build_snap_tap(2, 0, 1, groups, buf);
	ASSERT_EQ(len, 256);
	ASSERT_EQ(buf[0], 0x07);
	ASSERT_EQ(buf[1], 0x09);
	ASSERT_EQ(buf[2], 2);
	ASSERT_EQ(buf[3], 0);
}

ALLOY_TEST(test_darkproject_parse_event)
{
	const struct alloy_driver *drv = darkproject();
	struct alloy_device dev = { 0 };
	struct alloy_config *cfg = alloy_config_alloc(drv);
	uint8_t snap_tap_iface1_event[] = { 0x04, 0xf0, 0x00, 0x00,
					    0x00, 0x00, 0x01 };
	uint8_t profile_iface1_event[] = { 0x04, 0xf1, 0x03 };
	uint8_t snap_tap_event[] = { 0x05, 0xa4, 0xf0, 0x00,
				     0x00, 0x00, 0x00, 0x01 };
	uint8_t profile_event[] = { 0x05, 0xa4, 0xf1, 0x02,
				    0x00, 0x00, 0x00, 0x00 };
	int ret;

	alloy_device_open_id(&dev, drv->vendor_id, drv->product_id);
	alloy_config_defaults(drv, cfg);
	struct alloy_keyboard_config *k = alloy_kbd_cfg(cfg);
	k->snap_tap = 0;
	k->profile_active = 1;

	/* Parse Snap Tap ON event from Interface 1 */
	ret = alu87a_parse_event(&dev, snap_tap_iface1_event,
				 sizeof(snap_tap_iface1_event), cfg);
	ASSERT_EQ(ret, 1);
	ASSERT_EQ(k->snap_tap, 1);

	/* Same state yields 0 */
	ret = alu87a_parse_event(&dev, snap_tap_iface1_event,
				 sizeof(snap_tap_iface1_event), cfg);
	ASSERT_EQ(ret, 0);

	/* Parse Profile switch to 3 from Interface 1 */
	ret = alu87a_parse_event(&dev, profile_iface1_event,
				 sizeof(profile_iface1_event), cfg);
	ASSERT_EQ(ret, 1);
	ASSERT_EQ(k->profile_active, 3);

	/* Parse variant Snap Tap event */
	k->snap_tap = 0;
	ret = alu87a_parse_event(&dev, snap_tap_event, sizeof(snap_tap_event),
				 cfg);
	ASSERT_EQ(ret, 1);
	ASSERT_EQ(k->snap_tap, 1);

	/* Parse variant Profile switch to 2 */
	ret = alu87a_parse_event(&dev, profile_event, sizeof(profile_event),
				 cfg);
	ASSERT_EQ(ret, 1);
	ASSERT_EQ(k->profile_active, 2);

	alloy_device_close(&dev);
	alloy_config_free(cfg);
}

ALLOY_TEST(test_darkproject_custom_lighting)
{
	uint8_t planar_rgb[512];
	uint8_t chunk_buf[256];
	size_t len;

	memset(planar_rgb, 0, sizeof(planar_rgb));
	/* Set KeyW (index 14) to Red (255) */
	planar_rgb[14] = 255;

	/* build Chunk 0 */
	len = darkproject_build_custom_lighting_chunk(1, 0, 0, planar_rgb,
						      chunk_buf);
	ASSERT_EQ(len, 256);
	ASSERT_EQ(chunk_buf[0], 0x07);
	ASSERT_EQ(chunk_buf[1], 0x0A);
	ASSERT_EQ(chunk_buf[2], 1);
	ASSERT_EQ(chunk_buf[3], 1); /* Cust1 */
	ASSERT_EQ(chunk_buf[4], 1); /* Chunk 1 */
	ASSERT_EQ(chunk_buf[8 + 14], 255);

	/* build Chunk 1 */
	len = darkproject_build_custom_lighting_chunk(1, 0, 1, planar_rgb,
						      chunk_buf);
	ASSERT_EQ(len, 256);
	ASSERT_EQ(chunk_buf[0], 0x07);
	ASSERT_EQ(chunk_buf[1], 0x0A);
	ASSERT_EQ(chunk_buf[4], 2); /* Chunk 2 */
}

ALLOY_TEST(test_darkproject_parse_profile_and_snap_tap)
{
	const struct alloy_driver *drv = darkproject();
	struct alloy_config *cfg = alloy_config_alloc(drv);
	uint8_t profile_resp[256];
	uint8_t snap_tap_resp[256];

	alloy_config_defaults(drv, cfg);
	memset(profile_resp, 0, sizeof(profile_resp));
	memset(snap_tap_resp, 0, sizeof(snap_tap_resp));

	/* Simulate hardware Profile response (Cmd 0x82):
	 * Polling 1000Hz (resp[8]=4),
	 * Wave (resp[9]=0),
	 * 100% bright (resp[10]=4),
	 * Speed Fast (resp[24]=1 -> 5),
	 * MultiColor (resp[38]=8),
	 * Dir down (resp[52]=2),
	 * Color (255, 128, 64) at resp[59], resp[67], resp[75]
	 */
	profile_resp[8] = 4;
	profile_resp[9] = 0; /* Wave */
	profile_resp[10 + 0] = 4;
	profile_resp[24 + 0] = 1;
	profile_resp[38 + 0] = 8;
	profile_resp[52] = 2;
	profile_resp[59] = 255;
	profile_resp[67] = 128;
	profile_resp[75] = 64;

	ASSERT_EQ(alu87a_parse_profile_data(profile_resp, sizeof(profile_resp),
					    cfg),
		  0);
	struct alloy_devcfg *d = alloy_devcfg(cfg);
	struct alloy_keyboard_config *k = alloy_kbd_cfg(cfg);

	ASSERT_EQ(d->polling_hz, 1000);
	ASSERT_EQ(d->zone_fx[0], 1); /* Wave is effect index 1 */
	ASSERT_EQ(d->brightness, 100);
	ASSERT_EQ(d->zone_fx_param[0][ALLOY_FX_P_SPEED], 5);
	ASSERT_EQ(d->zone_fx_param[0][ALLOY_FX_P_MULTICOLOR], 1);
	ASSERT_EQ(d->zone_fx_param[0][ALLOY_FX_P_DIRECTION], 2);
	ASSERT_EQ(d->zone_color[0].r, 255);
	ASSERT_EQ(d->zone_color[0].g, 128);
	ASSERT_EQ(d->zone_color[0].b, 64);

	/* Simulate hardware Snap Tap response (Cmd 0x89):
	 * Snap Tap ON (resp[8]=1),
	 * Group 0: Mode 2, A (0x04) / D (0x07)
	 */
	snap_tap_resp[8] = 1;
	snap_tap_resp[9 + 0] = 2;
	snap_tap_resp[9 + 1] = 0x04;
	snap_tap_resp[9 + 2] = 0x07;

	ASSERT_EQ(darkproject_parse_snap_tap_data(snap_tap_resp,
						  sizeof(snap_tap_resp), cfg),
		  0);
	ASSERT_EQ(k->snap_tap, 1);
	ASSERT_EQ(k->snap_tap_group_count, 1);
	ASSERT_EQ(k->snap_tap_groups[0].mode, 2);
	ASSERT_EQ(k->snap_tap_groups[0].key1, 0x04);
	ASSERT_EQ(k->snap_tap_groups[0].key2, 0x07);

	alloy_config_free(cfg);
}
