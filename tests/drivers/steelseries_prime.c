// SPDX-License-Identifier: GPL-2.0-only
/*
 * SteelSeries Prime driver unit tests.
 *
 * Exercises the pure packet builders and driver ops against the byte sequences
 * ported from the rivalcfg device specification.
 *
 * Protocol reference: Documentation/protocol/steelseries-prime.rst.
 */
#include <stdlib.h>
#include <string.h>

#include "driver.h"
#include "mock_hid.h"
#include "test.h"

size_t prime_build_dpi(const struct alloy_config *cfg, uint8_t *buf);
size_t prime_build_polling(uint16_t polling_hz, uint8_t *buf);
size_t prime_build_color(const struct alloy_rgb *color, uint8_t *buf);
size_t prime_build_brightness(uint8_t brightness, uint8_t *buf);
size_t prime_build_buttons(const struct alloy_config *cfg, uint8_t *buf);
size_t prime_build_save(uint8_t *buf);

static const struct alloy_driver *drv_prime(void)
{
	const struct alloy_driver *drv = alloy_driver_find(0x1038, 0x182E);

	if (!drv) {
		printf("FAIL: prime driver not registered\n");
		exit(1);
	}
	return drv;
}

ALLOY_TEST(test_prime_registry)
{
	const struct alloy_driver *drv = drv_prime();
	const struct alloy_driver *drv_ice = alloy_driver_find(0x1038, 0x182A);
	const struct alloy_driver *drv_noir = alloy_driver_find(0x1038, 0x1856);

	ASSERT_TRUE(drv_ice != NULL);
	ASSERT_TRUE(drv_noir != NULL);
	ASSERT_TRUE(strcmp(drv->name, "SteelSeries Prime") == 0);
	ASSERT_TRUE(strcmp(drv_ice->name,
			   "SteelSeries Prime Rainbow 6 Siege Black Ice "
			   "Edition") == 0);
	ASSERT_TRUE(strcmp(drv_noir->name,
			   "SteelSeries Prime CS:GO Neo Noir Edition") == 0);

	ASSERT_EQ(drv->vendor_id, 0x1038);
	ASSERT_EQ(drv->product_id, 0x182E);
	ASSERT_EQ(drv->interface, 0);

	ASSERT_EQ(drv->dpi.min, 50);
	ASSERT_EQ(drv->dpi.max, 18000);
	ASSERT_EQ(drv->dpi.step, 50);
	ASSERT_EQ(drv->dpi.max_presets, 5);

	ASSERT_EQ(drv->num_polling_rates, 4);
	ASSERT_EQ(drv->polling_rates[0], 1000);
	ASSERT_EQ(drv->polling_rates[1], 500);
	ASSERT_EQ(drv->polling_rates[2], 250);
	ASSERT_EQ(drv->polling_rates[3], 125);

	ASSERT_EQ(drv->num_zones, 1);
	ASSERT_TRUE(strcmp(drv->zones[0].name, "WHEEL") == 0);
	ASSERT_EQ(drv->zones[0].def_color.r, 0xFF);
	ASSERT_EQ(drv->zones[0].def_color.g, 0x52);
	ASSERT_EQ(drv->zones[0].def_color.b, 0x00);

	ASSERT_EQ(drv->num_buttons, 6);
	ASSERT_EQ(drv->buttons[5].def.type, ALLOY_ACT_DPI_CYCLE);

	ASSERT_TRUE((drv->caps & ALLOY_CAP_BRIGHTNESS) != 0);
	ASSERT_TRUE(!(drv->caps & ALLOY_CAP_BATTERY));
}

ALLOY_TEST(test_prime_dpi_packets)
{
	struct alloy_config cfg;
	uint8_t buf[ALLOY_HID_REPORT_SIZE];
	size_t len;

	drv_prime()->config_defaults(drv_prime(), &cfg);

	/* 1 preset at 400 DPI */
	cfg.mouse.dpi_count = 1;
	cfg.mouse.dpi_active = 0;
	cfg.mouse.dpi[0][0] = 400;

	len = prime_build_dpi(&cfg, buf);
	ASSERT_EQ(len, 5);
	ASSERT_EQ(buf[0], 0x61);
	ASSERT_EQ(buf[1], 1); /* count */
	ASSERT_EQ(buf[2], 0); /* active index */
	ASSERT_EQ(buf[3], 8); /* 400 / 50 = 8 */
	ASSERT_EQ(buf[4], 0);

	/* 5 presets at 400, 800, 1200, 2400, 3200 with active preset 2 */
	cfg.mouse.dpi_count = 5;
	cfg.mouse.dpi_active = 2;
	cfg.mouse.dpi[0][0] = 400;
	cfg.mouse.dpi[1][0] = 800;
	cfg.mouse.dpi[2][0] = 1200;
	cfg.mouse.dpi[3][0] = 2400;
	cfg.mouse.dpi[4][0] = 3200;

	len = prime_build_dpi(&cfg, buf);
	ASSERT_EQ(len, 13);
	ASSERT_EQ(buf[0], 0x61);
	ASSERT_EQ(buf[1], 5);
	ASSERT_EQ(buf[2], 2);
	ASSERT_EQ(buf[3], 0x08); /* 400 DPI -> 8 */
	ASSERT_EQ(buf[4], 0x00);
	ASSERT_EQ(buf[5], 0x10); /* 800 DPI -> 16 */
	ASSERT_EQ(buf[6], 0x00);
	ASSERT_EQ(buf[7], 0x18); /* 1200 DPI -> 24 */
	ASSERT_EQ(buf[8], 0x00);
	ASSERT_EQ(buf[9], 0x30); /* 2400 DPI -> 48 */
	ASSERT_EQ(buf[10], 0x00);
	ASSERT_EQ(buf[11], 0x40); /* 3200 DPI -> 64 */
	ASSERT_EQ(buf[12], 0x00);

	/* clamping min (20 -> 50) and max (20000 -> 18000) */
	cfg.mouse.dpi_count = 2;
	cfg.mouse.dpi_active = 0;
	cfg.mouse.dpi[0][0] = 20;
	cfg.mouse.dpi[1][0] = 20000;
	len = prime_build_dpi(&cfg, buf);
	ASSERT_EQ(buf[3], 0x01); /* 50 DPI -> 1 */
	ASSERT_EQ(buf[4], 0x00);
	ASSERT_EQ(buf[5], 0x68); /* 18000 DPI -> 360 = 0x0168 */
	ASSERT_EQ(buf[6], 0x01);

	/* rounding to nearest 50 */
	cfg.mouse.dpi[0][0] = 820;
	cfg.mouse.dpi[1][0] = 830;
	len = prime_build_dpi(&cfg, buf);
	ASSERT_EQ(buf[3], 0x10); /* 800 DPI -> 16 */
	ASSERT_EQ(buf[5], 0x11); /* 850 DPI -> 17 */
}

ALLOY_TEST(test_prime_polling_packets)
{
	uint8_t buf[ALLOY_HID_REPORT_SIZE];
	size_t len;

	len = prime_build_polling(1000, buf);
	ASSERT_EQ(len, 2);
	ASSERT_EQ(buf[0], 0x5D);
	ASSERT_EQ(buf[1], 0x01);

	len = prime_build_polling(500, buf);
	ASSERT_EQ(len, 2);
	ASSERT_EQ(buf[1], 0x02);

	len = prime_build_polling(250, buf);
	ASSERT_EQ(len, 2);
	ASSERT_EQ(buf[1], 0x03);

	len = prime_build_polling(125, buf);
	ASSERT_EQ(len, 2);
	ASSERT_EQ(buf[1], 0x04);

	len = prime_build_polling(8000, buf);
	ASSERT_EQ(len, 2);
	ASSERT_EQ(buf[1], 0x01);
}

ALLOY_TEST(test_prime_color_packet)
{
	uint8_t buf[ALLOY_HID_REPORT_SIZE];
	struct alloy_rgb rgb;
	size_t len;

	rgb = (struct alloy_rgb){ 0xFF, 0x52, 0x00 };
	len = prime_build_color(&rgb, buf);
	ASSERT_EQ(len, 21);
	ASSERT_EQ(buf[0], 0x62);
	ASSERT_EQ(buf[1], 0x01);
	ASSERT_EQ(buf[2], 0xFF);
	ASSERT_EQ(buf[3], 0x52);
	ASSERT_EQ(buf[4], 0x00);
	ASSERT_EQ(buf[5], 0x00);
	ASSERT_EQ(buf[19], 0x00);
	ASSERT_EQ(buf[20], 0xFF);
}

ALLOY_TEST(test_prime_brightness_packet)
{
	uint8_t buf[ALLOY_HID_REPORT_SIZE];
	size_t len;

	/* 100% -> 256 (0x0100) */
	len = prime_build_brightness(100, buf);
	ASSERT_EQ(len, 3);
	ASSERT_EQ(buf[0], 0x5F);
	ASSERT_EQ(buf[1], 0x00);
	ASSERT_EQ(buf[2], 0x01);

	/* 50% -> 128 (0x0080) */
	len = prime_build_brightness(50, buf);
	ASSERT_EQ(len, 3);
	ASSERT_EQ(buf[1], 0x80);
	ASSERT_EQ(buf[2], 0x00);

	/* 0% -> 0 */
	len = prime_build_brightness(0, buf);
	ASSERT_EQ(len, 3);
	ASSERT_EQ(buf[1], 0x00);
	ASSERT_EQ(buf[2], 0x00);
}

ALLOY_TEST(test_prime_button_packets)
{
	struct alloy_config cfg;
	uint8_t buf[ALLOY_HID_REPORT_SIZE];
	size_t len;

	drv_prime()->config_defaults(drv_prime(), &cfg);

	/* default mappings */
	len = prime_build_buttons(&cfg, buf);
	ASSERT_EQ(len, 31);
	ASSERT_EQ(buf[0], 0x5B);
	ASSERT_EQ(buf[1], 0x01); /* Button 1 */
	ASSERT_EQ(buf[6], 0x02); /* Button 2 */
	ASSERT_EQ(buf[11], 0x03); /* Button 3 */
	ASSERT_EQ(buf[16], 0x04); /* Button 4 */
	ASSERT_EQ(buf[21], 0x05); /* Button 5 */
	ASSERT_EQ(buf[26], 0x30); /* Button 6 (CPI) */

	/* custom mapping */
	cfg.mouse.buttons[0] = (struct alloy_action){ ALLOY_ACT_SCROLL_UP, 0 };
	cfg.mouse.buttons[1] = (struct alloy_action){ ALLOY_ACT_DISABLED, 0 };
	cfg.mouse.buttons[2] = (struct alloy_action){ ALLOY_ACT_MOUSE, 1 };
	cfg.mouse.buttons[3] =
		(struct alloy_action){ ALLOY_ACT_KEYBOARD, 0x04 }; /* key 'a' */
	cfg.mouse.buttons[4] = (struct alloy_action){ ALLOY_ACT_MEDIA, 0xCD };
	cfg.mouse.buttons[5] = (struct alloy_action){ ALLOY_ACT_DPI_CYCLE, 0 };
	len = prime_build_buttons(&cfg, buf);
	ASSERT_EQ(len, 31);
	ASSERT_EQ(buf[0], 0x5B);
	ASSERT_EQ(buf[1], 0x31); /* Button 1 -> Scroll Up */
	ASSERT_EQ(buf[6], 0x00); /* Button 2 -> Disabled */
	ASSERT_EQ(buf[11], 0x01); /* Button 3 -> Left Click */
	ASSERT_EQ(buf[16], 0x51); /* Button 4 -> Keyboard */
	ASSERT_EQ(buf[17], 0x04); /* 'a' */
	ASSERT_EQ(buf[21], 0x61); /* Button 5 -> Media */
	ASSERT_EQ(buf[22], 0xCD);
	ASSERT_EQ(buf[26], 0x30); /* Button 6 -> CPI Cycle */

	/* test remapping right click (Button 2) to Middle click */
	cfg.mouse.buttons[1] = (struct alloy_action){ ALLOY_ACT_MOUSE, 3 };
	len = prime_build_buttons(&cfg, buf);
	ASSERT_EQ(buf[6], 0x03); /* Button 2 -> Middle Click */
}

ALLOY_TEST(test_prime_save_packet)
{
	uint8_t buf[ALLOY_HID_REPORT_SIZE];
	size_t len;

	len = prime_build_save(buf);
	ASSERT_EQ(len, 1);
	ASSERT_EQ(buf[0], 0x59);
}

ALLOY_TEST(test_prime_ops_execution)
{
	struct alloy_device dev;
	struct alloy_config cfg;
	const struct alloy_driver *drv = drv_prime();

	memset(&dev, 0, sizeof(dev));
	dev.hid.fd = 42;
	dev.drv = drv;

	drv->config_defaults(drv, &cfg);

	/* Apply DPI */
	cfg.mouse.dpi_count = 2;
	cfg.mouse.dpi[0][0] = 800;
	cfg.mouse.dpi[1][0] = 1600;
	mock_hid_reset();
	ASSERT_EQ(drv->ops->apply_dpi(&dev, &cfg), 0);
	ASSERT_EQ(mock_hid.num_cmds, 1);
	ASSERT_EQ(mock_hid.cmds[0].payload[0], 0x61);
	ASSERT_EQ(mock_hid.cmds[0].payload[1], 2);

	/* Apply Polling */
	cfg.common.polling_hz = 500;
	mock_hid_reset();
	ASSERT_EQ(drv->ops->apply_polling(&dev, &cfg), 0);
	ASSERT_EQ(mock_hid.num_cmds, 1);
	ASSERT_EQ(mock_hid.cmds[0].payload[0], 0x5D);
	ASSERT_EQ(mock_hid.cmds[0].payload[1], 0x02);

	/* Apply Colors */
	cfg.common.zone_color[0] = (struct alloy_rgb){ 0x12, 0x34, 0x56 };
	mock_hid_reset();
	ASSERT_EQ(drv->ops->apply_colors(&dev, &cfg), 0);
	ASSERT_EQ(mock_hid.num_cmds, 1);
	ASSERT_EQ(mock_hid.cmds[0].payload[0], 0x62);
	ASSERT_EQ(mock_hid.cmds[0].payload[2], 0x12);
	ASSERT_EQ(mock_hid.cmds[0].payload[3], 0x34);
	ASSERT_EQ(mock_hid.cmds[0].payload[4], 0x56);

	/* Apply Brightness */
	cfg.common.brightness = 75;
	mock_hid_reset();
	ASSERT_EQ(drv->ops->apply_brightness(&dev, &cfg), 0);
	ASSERT_EQ(mock_hid.num_cmds, 1);
	ASSERT_EQ(mock_hid.cmds[0].payload[0], 0x5F);

	/* Apply Buttons */
	mock_hid_reset();
	ASSERT_EQ(drv->ops->apply_buttons(&dev, &cfg), 0);
	ASSERT_EQ(mock_hid.num_cmds, 1);
	ASSERT_EQ(mock_hid.cmds[0].payload[0], 0x5B);

	/* Save */
	mock_hid_reset();
	ASSERT_EQ(drv->ops->save(&dev), 0);
	ASSERT_EQ(mock_hid.num_cmds, 1);
	ASSERT_EQ(mock_hid.cmds[0].payload[0], 0x59);

	/* Error propagation */
	mock_hid_reset();
	mock_hid.fail_cmds = 1;
	ASSERT_TRUE(drv->ops->apply_dpi(&dev, &cfg) < 0);
	mock_hid_reset();
	mock_hid.fail_cmds = 1;
	ASSERT_TRUE(drv->ops->apply_polling(&dev, &cfg) < 0);
	mock_hid_reset();
	mock_hid.fail_cmds = 1;
	ASSERT_TRUE(drv->ops->apply_colors(&dev, &cfg) < 0);
	mock_hid_reset();
	mock_hid.fail_cmds = 1;
	ASSERT_TRUE(drv->ops->apply_brightness(&dev, &cfg) < 0);
	mock_hid_reset();
	mock_hid.fail_cmds = 1;
	ASSERT_TRUE(drv->ops->apply_buttons(&dev, &cfg) < 0);
	mock_hid_reset();
	mock_hid.fail_cmds = 1;
	ASSERT_TRUE(drv->ops->save(&dev) < 0);
}
