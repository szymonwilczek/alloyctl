// SPDX-License-Identifier: GPL-2.0-only
/*
 * SteelSeries Rival 600 driver unit tests.
 *
 * Exercises the pure packet builders and driver ops against the byte sequences
 * ported from the rivalcfg device specification.
 *
 * Protocol reference: Documentation/protocol/steelseries-rival600.rst.
 */
#include <stdlib.h>
#include <string.h>

#include "driver.h"
#include "mock_hid.h"
#include "test.h"

size_t r600_build_dpi(uint8_t preset_idx, uint16_t dpi, uint8_t *buf);
size_t r600_build_polling(uint16_t polling_hz, uint8_t *buf);
size_t r600_build_zone_color(uint8_t led_id, const struct alloy_rgb *color,
			     uint8_t *buf);
size_t r600_build_buttons(const struct alloy_config *cfg, uint8_t *buf);
size_t r600_build_save(uint8_t *buf);

static const struct alloy_driver *drv_r600(void)
{
	const struct alloy_driver *drv = alloy_driver_find(0x1038, 0x1724);

	if (!drv) {
		printf("FAIL: rival 600 driver not registered\n");
		exit(1);
	}
	return drv;
}

ALLOY_TEST(test_r600_registry)
{
	const struct alloy_driver *drv = drv_r600();
	const struct alloy_driver *drv_dota = alloy_driver_find(0x1038, 0x172E);

	ASSERT_TRUE(drv_dota != NULL);
	ASSERT_TRUE(strcmp(drv->name, "SteelSeries Rival 600") == 0);
	ASSERT_TRUE(strcmp(drv_dota->name,
			   "SteelSeries Rival 600 Dota 2 Edition") == 0);

	ASSERT_EQ(drv->vendor_id, 0x1038);
	ASSERT_EQ(drv->product_id, 0x1724);
	ASSERT_EQ(drv->interface, 0);

	ASSERT_EQ(drv->dpi.min, 100);
	ASSERT_EQ(drv->dpi.max, 12000);
	ASSERT_EQ(drv->dpi.step, 100);
	ASSERT_EQ(drv->dpi.max_presets, 2);

	ASSERT_EQ(drv->num_polling_rates, 4);
	ASSERT_EQ(drv->polling_rates[0], 1000);
	ASSERT_EQ(drv->polling_rates[1], 500);
	ASSERT_EQ(drv->polling_rates[2], 250);
	ASSERT_EQ(drv->polling_rates[3], 125);

	ASSERT_EQ(drv->num_zones, 8);
	ASSERT_TRUE(strcmp(drv->zones[0].name, "WHEEL") == 0);
	ASSERT_TRUE(strcmp(drv->zones[1].name, "LOGO") == 0);
	ASSERT_TRUE(strcmp(drv->zones[2].name, "LEFT STRIP TOP") == 0);
	ASSERT_TRUE(strcmp(drv->zones[3].name, "RIGHT STRIP TOP") == 0);
	ASSERT_TRUE(strcmp(drv->zones[4].name, "LEFT STRIP MID") == 0);
	ASSERT_TRUE(strcmp(drv->zones[5].name, "RIGHT STRIP MID") == 0);
	ASSERT_TRUE(strcmp(drv->zones[6].name, "LEFT STRIP BOT") == 0);
	ASSERT_TRUE(strcmp(drv->zones[7].name, "RIGHT STRIP BOT") == 0);

	ASSERT_EQ(drv->num_buttons, 7);
	ASSERT_EQ(drv->buttons[5].def.type, ALLOY_ACT_DISABLED);
	ASSERT_EQ(drv->buttons[6].def.type, ALLOY_ACT_DPI_CYCLE);

	ASSERT_TRUE(!(drv->caps & ALLOY_CAP_BATTERY));
}

ALLOY_TEST(test_r600_dpi_packets)
{
	uint8_t buf[ALLOY_HID_REPORT_SIZE];
	size_t len;

	/* Preset 1 (index 0) */
	len = r600_build_dpi(0, 100, buf);
	ASSERT_EQ(len, 6);
	ASSERT_EQ(buf[0], 0x03);
	ASSERT_EQ(buf[1], 0x00);
	ASSERT_EQ(buf[2], 0x01);
	ASSERT_EQ(buf[3], 0x00); /* 100 DPI -> 0 */
	ASSERT_EQ(buf[4], 0x00);
	ASSERT_EQ(buf[5], 0x42);

	len = r600_build_dpi(0, 800, buf);
	ASSERT_EQ(len, 6);
	ASSERT_EQ(buf[2], 0x01);
	ASSERT_EQ(buf[3], 0x07); /* 800 DPI -> 7 */

	len = r600_build_dpi(0, 1600, buf);
	ASSERT_EQ(len, 6);
	ASSERT_EQ(buf[2], 0x01);
	ASSERT_EQ(buf[3], 0x0F); /* 1600 DPI -> 15 */

	len = r600_build_dpi(0, 12000, buf);
	ASSERT_EQ(len, 6);
	ASSERT_EQ(buf[2], 0x01);
	ASSERT_EQ(buf[3], 0x77); /* 12000 DPI -> 119 = 0x77 */

	/* Preset 2 (index 1) */
	len = r600_build_dpi(1, 3200, buf);
	ASSERT_EQ(len, 6);
	ASSERT_EQ(buf[2], 0x02);
	ASSERT_EQ(buf[3], 0x1F); /* 3200 DPI -> 31 */

	/* Clamping & Rounding */
	len = r600_build_dpi(0, 50, buf);
	ASSERT_EQ(buf[3], 0x00);
	len = r600_build_dpi(1, 20000, buf);
	ASSERT_EQ(buf[3], 0x77);
	len = r600_build_dpi(0, 840, buf);
	ASSERT_EQ(buf[3], 0x07);
	len = r600_build_dpi(0, 860, buf);
	ASSERT_EQ(buf[3], 0x08);
}

ALLOY_TEST(test_r600_polling_packets)
{
	uint8_t buf[ALLOY_HID_REPORT_SIZE];
	size_t len;

	len = r600_build_polling(1000, buf);
	ASSERT_EQ(len, 3);
	ASSERT_EQ(buf[0], 0x04);
	ASSERT_EQ(buf[1], 0x00);
	ASSERT_EQ(buf[2], 0x01);

	len = r600_build_polling(500, buf);
	ASSERT_EQ(len, 3);
	ASSERT_EQ(buf[2], 0x02);

	len = r600_build_polling(250, buf);
	ASSERT_EQ(len, 3);
	ASSERT_EQ(buf[2], 0x03);

	len = r600_build_polling(125, buf);
	ASSERT_EQ(len, 3);
	ASSERT_EQ(buf[2], 0x04);

	len = r600_build_polling(4000, buf);
	ASSERT_EQ(len, 3);
	ASSERT_EQ(buf[2], 0x01);
}

ALLOY_TEST(test_r600_zone_color_packets)
{
	uint8_t buf[ALLOY_HID_REPORT_SIZE];
	struct alloy_rgb rgb;
	size_t len;

	/* Zone 2: Left Strip Top (led_id 0x02) */
	rgb = (struct alloy_rgb){ 0xFF, 0x00, 0x80 };
	len = r600_build_zone_color(0x02, &rgb, buf);
	ASSERT_EQ(len, 33);
	ASSERT_EQ(buf[0], 0x05);
	ASSERT_EQ(buf[1], 0x00);
	ASSERT_EQ(buf[2], 0x02); /* led_id offset 0 */
	ASSERT_EQ(buf[7], 0x02); /* led_id offset 5 */
	ASSERT_EQ(buf[8], 0xE8); /* duration 1000ms low byte */
	ASSERT_EQ(buf[9], 0x03); /* duration 1000ms high byte */
	ASSERT_EQ(buf[24], 0x01); /* repeat flag */
	ASSERT_EQ(buf[29], 0x01); /* color count */
	ASSERT_EQ(buf[30], 0xFF); /* R */
	ASSERT_EQ(buf[31], 0x00); /* G */
	ASSERT_EQ(buf[32], 0x80); /* B */

	/* Zone 7: Right Strip Bot (led_id 0x07) */
	rgb = (struct alloy_rgb){ 0x12, 0x34, 0x56 };
	len = r600_build_zone_color(0x07, &rgb, buf);
	ASSERT_EQ(len, 33);
	ASSERT_EQ(buf[2], 0x07);
	ASSERT_EQ(buf[7], 0x07);
	ASSERT_EQ(buf[30], 0x12);
	ASSERT_EQ(buf[31], 0x34);
	ASSERT_EQ(buf[32], 0x56);
}

ALLOY_TEST(test_r600_button_packets)
{
	struct alloy_config cfg;
	uint8_t buf[ALLOY_HID_REPORT_SIZE];
	size_t len;

	drv_r600()->config_defaults(drv_r600(), &cfg);

	/* Default mappings (7 buttons) */
	len = r600_build_buttons(&cfg, buf);
	ASSERT_EQ(len, 37);
	ASSERT_EQ(buf[0], 0x31);
	ASSERT_EQ(buf[1], 0x00);
	ASSERT_EQ(buf[2], 0x01); /* Button 1 */
	ASSERT_EQ(buf[7], 0x02); /* Button 2 */
	ASSERT_EQ(buf[12], 0x03); /* Button 3 */
	ASSERT_EQ(buf[17], 0x04); /* Button 4 */
	ASSERT_EQ(buf[22], 0x05); /* Button 5 */
	ASSERT_EQ(buf[27], 0x00); /* Button 6 (Disabled) */
	ASSERT_EQ(buf[32], 0x30); /* Button 7 (CPI) */

	/* Custom mapping */
	cfg.buttons[5] = (struct alloy_action){ ALLOY_ACT_MOUSE, 6 };
	cfg.buttons[6] = (struct alloy_action){ ALLOY_ACT_KEYBOARD, 0x04 };
	len = r600_build_buttons(&cfg, buf);
	ASSERT_EQ(buf[27], 0x06); /* Button 6 mouse button */
	ASSERT_EQ(buf[32], 0x51); /* Keyboard action */
	ASSERT_EQ(buf[33], 0x04);
}

ALLOY_TEST(test_r600_save_packet)
{
	uint8_t buf[ALLOY_HID_REPORT_SIZE];
	size_t len;

	len = r600_build_save(buf);
	ASSERT_EQ(len, 2);
	ASSERT_EQ(buf[0], 0x09);
	ASSERT_EQ(buf[1], 0x00);
}

ALLOY_TEST(test_r600_ops_execution)
{
	struct alloy_device dev;
	struct alloy_config cfg;
	const struct alloy_driver *drv = drv_r600();

	memset(&dev, 0, sizeof(dev));
	dev.hid.fd = 42;
	dev.drv = drv;

	drv->config_defaults(drv, &cfg);

	/* Apply DPI */
	cfg.dpi_count = 2;
	cfg.dpi[0][0] = 800;
	cfg.dpi[1][0] = 1600;
	mock_hid_reset();
	ASSERT_EQ(drv->ops->apply_dpi(&dev, &cfg), 0);
	ASSERT_EQ(mock_hid.num_cmds, 2);
	ASSERT_EQ(mock_hid.cmds[0].payload[0], 0x03);
	ASSERT_EQ(mock_hid.cmds[0].payload[2], 0x01);
	ASSERT_EQ(mock_hid.cmds[0].payload[3], 0x07);
	ASSERT_EQ(mock_hid.cmds[1].payload[0], 0x03);
	ASSERT_EQ(mock_hid.cmds[1].payload[2], 0x02);
	ASSERT_EQ(mock_hid.cmds[1].payload[3], 0x0F);

	/* Single DPI preset fallback */
	cfg.dpi_count = 1;
	cfg.dpi[0][0] = 3200;
	mock_hid_reset();
	ASSERT_EQ(drv->ops->apply_dpi(&dev, &cfg), 0);
	ASSERT_EQ(mock_hid.num_cmds, 2);
	ASSERT_EQ(mock_hid.cmds[0].payload[3], 0x1F);
	ASSERT_EQ(mock_hid.cmds[1].payload[3], 0x1F);

	/* Apply Polling */
	cfg.polling_hz = 500;
	mock_hid_reset();
	ASSERT_EQ(drv->ops->apply_polling(&dev, &cfg), 0);
	ASSERT_EQ(mock_hid.num_cmds, 1);
	ASSERT_EQ(mock_hid.cmds[0].payload[0], 0x04);
	ASSERT_EQ(mock_hid.cmds[0].payload[2], 0x02);

	/* Apply Colors (8 zones -> 8 commands) */
	mock_hid_reset();
	ASSERT_EQ(drv->ops->apply_colors(&dev, &cfg), 0);
	ASSERT_EQ(mock_hid.num_cmds, 8);
	ASSERT_EQ(mock_hid.cmds[0].payload[0], 0x05);
	ASSERT_EQ(mock_hid.cmds[0].payload[2], 0x00); /* Zone 0 */
	ASSERT_EQ(mock_hid.cmds[7].payload[0], 0x05);
	ASSERT_EQ(mock_hid.cmds[7].payload[2], 0x07); /* Zone 7 */

	/* Apply Buttons */
	mock_hid_reset();
	ASSERT_EQ(drv->ops->apply_buttons(&dev, &cfg), 0);
	ASSERT_EQ(mock_hid.num_cmds, 1);
	ASSERT_EQ(mock_hid.cmds[0].payload[0], 0x31);

	/* Save */
	mock_hid_reset();
	ASSERT_EQ(drv->ops->save(&dev), 0);
	ASSERT_EQ(mock_hid.num_cmds, 1);
	ASSERT_EQ(mock_hid.cmds[0].payload[0], 0x09);

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
	ASSERT_TRUE(drv->ops->apply_buttons(&dev, &cfg) < 0);
	mock_hid_reset();
	mock_hid.fail_cmds = 1;
	ASSERT_TRUE(drv->ops->save(&dev) < 0);
}
