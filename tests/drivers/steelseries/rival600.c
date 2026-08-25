/* SPDX-License-Identifier: GPL-2.0-only */
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
#include "hid.h"
#include "lib/mouse.h"
#include "test.h"

size_t r600_build_dpi_preset(uint8_t preset_idx, uint16_t dpi, uint8_t *buf);
size_t r600_build_polling(const struct alloy_config *cfg, uint8_t *buf);
size_t r600_build_zone_color(const struct alloy_config *cfg, int zone,
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
	const struct alloy_devinfo *info = alloy_devinfo(drv);
	const struct alloy_mouse_info *mouse = alloy_mouse_info(drv);
	const struct alloy_hid_params *hid = drv->transport_data;

	ASSERT_TRUE(drv_dota != NULL);
	ASSERT_TRUE(strcmp(drv->name, "SteelSeries Rival 600") == 0);
	ASSERT_TRUE(strcmp(drv_dota->name,
			   "SteelSeries Rival 600 Dota 2 Edition") == 0);

	ASSERT_EQ(drv->vendor_id, 0x1038);
	ASSERT_EQ(drv->product_id, 0x1724);
	ASSERT_TRUE(hid != NULL);
	ASSERT_EQ(hid->interface, 0);

	ASSERT_TRUE(mouse != NULL);
	ASSERT_EQ(mouse->dpi.min, 100);
	ASSERT_EQ(mouse->dpi.max, 12000);
	ASSERT_EQ(mouse->dpi.step, 100);
	ASSERT_EQ(mouse->dpi.max_presets, 2);

	ASSERT_EQ(info->num_polling_rates, 4);
	ASSERT_EQ(info->polling_rates[0], 1000);
	ASSERT_EQ(info->polling_rates[1], 500);
	ASSERT_EQ(info->polling_rates[2], 250);
	ASSERT_EQ(info->polling_rates[3], 125);

	ASSERT_EQ(info->num_zones, 8);
	ASSERT_TRUE(strcmp(info->zones[0].name, "WHEEL") == 0);
	ASSERT_TRUE(strcmp(info->zones[1].name, "LOGO") == 0);
	ASSERT_TRUE(strcmp(info->zones[2].name, "LEFT STRIP TOP") == 0);
	ASSERT_TRUE(strcmp(info->zones[3].name, "RIGHT STRIP TOP") == 0);
	ASSERT_TRUE(strcmp(info->zones[4].name, "LEFT STRIP MID") == 0);
	ASSERT_TRUE(strcmp(info->zones[5].name, "RIGHT STRIP MID") == 0);
	ASSERT_TRUE(strcmp(info->zones[6].name, "LEFT STRIP BOT") == 0);
	ASSERT_TRUE(strcmp(info->zones[7].name, "RIGHT STRIP BOT") == 0);

	ASSERT_EQ(info->num_fx, 5);
	ASSERT_TRUE(!(info->caps & ALLOY_CAP_FX_GLOBAL));

	ASSERT_EQ(mouse->num_buttons, 7);
	ASSERT_EQ(mouse->buttons[5].def.type, ALLOY_ACT_DISABLED);
	ASSERT_EQ(mouse->buttons[6].def.type, ALLOY_ACT_DPI_CYCLE);

	ASSERT_TRUE(!(info->caps & ALLOY_CAP_BATTERY));
	ASSERT_TRUE(info->caps & ALLOY_CAP_ACCEL);
	ASSERT_TRUE(info->caps & ALLOY_CAP_DECEL);
	ASSERT_TRUE(info->caps & ALLOY_CAP_ANGLE_SNAPPING);
}

ALLOY_TEST(test_r600_dpi_packets)
{
	uint8_t buf[ALLOY_HID_REPORT_SIZE];
	size_t len;

	/* Preset 1 (index 0) */
	len = r600_build_dpi_preset(0, 100, buf);
	ASSERT_EQ(len, 6);
	ASSERT_EQ(buf[0], 0x03);
	ASSERT_EQ(buf[1], 0x00);
	ASSERT_EQ(buf[2], 0x01);
	ASSERT_EQ(buf[3], 0x00); /* 100 DPI -> 0 */
	ASSERT_EQ(buf[4], 0x00);
	ASSERT_EQ(buf[5], 0x42);

	len = r600_build_dpi_preset(0, 800, buf);
	ASSERT_EQ(len, 6);
	ASSERT_EQ(buf[2], 0x01);
	ASSERT_EQ(buf[3], 0x07); /* 800 DPI -> 7 */

	len = r600_build_dpi_preset(0, 1600, buf);
	ASSERT_EQ(len, 6);
	ASSERT_EQ(buf[2], 0x01);
	ASSERT_EQ(buf[3], 0x0F); /* 1600 DPI -> 15 */

	len = r600_build_dpi_preset(0, 12000, buf);
	ASSERT_EQ(len, 6);
	ASSERT_EQ(buf[2], 0x01);
	ASSERT_EQ(buf[3], 0x77); /* 12000 DPI -> 119 = 0x77 */

	/* Preset 2 (index 1) */
	len = r600_build_dpi_preset(1, 3200, buf);
	ASSERT_EQ(len, 6);
	ASSERT_EQ(buf[2], 0x02);
	ASSERT_EQ(buf[3], 0x1F); /* 3200 DPI -> 31 */

	/* Clamping & Rounding */
	len = r600_build_dpi_preset(0, 50, buf);
	ASSERT_EQ(buf[3], 0x00);
	len = r600_build_dpi_preset(1, 20000, buf);
	ASSERT_EQ(buf[3], 0x77);
	len = r600_build_dpi_preset(0, 840, buf);
	ASSERT_EQ(buf[3], 0x07);
	len = r600_build_dpi_preset(0, 860, buf);
	ASSERT_EQ(buf[3], 0x08);
}

ALLOY_TEST(test_r600_polling_packets)
{
	const struct alloy_driver *drv = drv_r600();
	struct alloy_config *cfg = alloy_config_alloc(drv);
	uint8_t buf[ALLOY_HID_REPORT_SIZE];
	size_t len;

	alloy_config_defaults(drv, cfg);

	alloy_devcfg(cfg)->polling_hz = 1000;
	len = r600_build_polling(cfg, buf);
	ASSERT_EQ(len, 3);
	ASSERT_EQ(buf[0], 0x04);
	ASSERT_EQ(buf[1], 0x00);
	ASSERT_EQ(buf[2], 0x01);

	alloy_devcfg(cfg)->polling_hz = 500;
	len = r600_build_polling(cfg, buf);
	ASSERT_EQ(len, 3);
	ASSERT_EQ(buf[2], 0x02);

	alloy_devcfg(cfg)->polling_hz = 250;
	len = r600_build_polling(cfg, buf);
	ASSERT_EQ(len, 3);
	ASSERT_EQ(buf[2], 0x03);

	alloy_devcfg(cfg)->polling_hz = 125;
	len = r600_build_polling(cfg, buf);
	ASSERT_EQ(len, 3);
	ASSERT_EQ(buf[2], 0x04);

	alloy_devcfg(cfg)->polling_hz = 4000;
	len = r600_build_polling(cfg, buf);
	ASSERT_EQ(len, 3);
	ASSERT_EQ(buf[2], 0x01);

	alloy_config_free(cfg);
}

ALLOY_TEST(test_r600_zone_color_packets)
{
	const struct alloy_driver *drv = drv_r600();
	struct alloy_config *cfg = alloy_config_alloc(drv);
	uint8_t buf[ALLOY_HID_REPORT_SIZE];
	size_t len;

	alloy_config_defaults(drv, cfg);

	/* Zone 0 (Wheel): Red, Steady */
	alloy_devcfg(cfg)->zone_fx[0] = 0;
	alloy_devcfg(cfg)->zone_color[0] =
		(struct alloy_rgb){ 0xFF, 0x00, 0x00 };
	len = r600_build_zone_color(cfg, 0, buf);
	ASSERT_EQ(len, 37);
	ASSERT_EQ(buf[0], 0x05);
	ASSERT_EQ(buf[1], 0x00);
	ASSERT_EQ(buf[2], 0x00); /* zone index 0 */
	ASSERT_EQ(buf[7], 0x00);
	ASSERT_EQ(buf[8], 0xE8);
	ASSERT_EQ(buf[9], 0x03);
	ASSERT_EQ(buf[24], 0x01);
	ASSERT_EQ(buf[29], 0x01);
	ASSERT_EQ(buf[30], 0xFF); /* Initial color */
	ASSERT_EQ(buf[31], 0x00);
	ASSERT_EQ(buf[32], 0x00);
	ASSERT_EQ(buf[33], 0xFF); /* Stop 0 color */
	ASSERT_EQ(buf[34], 0x00);
	ASSERT_EQ(buf[35], 0x00);
	ASSERT_EQ(buf[36], 0x00); /* Stop 0 delta */

	/* Zone 7 (Right Strip Bottom): Cyan */
	alloy_devcfg(cfg)->zone_color[7] =
		(struct alloy_rgb){ 0x00, 0xFF, 0xFF };
	len = r600_build_zone_color(cfg, 7, buf);
	ASSERT_EQ(len, 37);
	ASSERT_EQ(buf[2], 0x07);
	ASSERT_EQ(buf[7], 0x07);
	ASSERT_EQ(buf[30], 0x00);
	ASSERT_EQ(buf[31], 0xFF);
	ASSERT_EQ(buf[32], 0xFF);
	ASSERT_EQ(buf[33], 0x00);
	ASSERT_EQ(buf[34], 0xFF);
	ASSERT_EQ(buf[35], 0xFF);
	ASSERT_EQ(buf[36], 0x00);

	/* Effects: Breathe (3 stops: color -> black -> color, 45 bytes) */
	alloy_devcfg(cfg)->zone_fx[0] = 1;
	len = r600_build_zone_color(cfg, 0, buf);
	ASSERT_EQ(len, 45);
	ASSERT_EQ(buf[24], 0x00); /* repeat continuous */
	ASSERT_EQ(buf[29], 0x03); /* 3 stops */
	ASSERT_EQ(buf[30], 0xFF); /* Initial color: Red */
	ASSERT_EQ(buf[33], 0xFF); /* Stop 0: Red */
	ASSERT_EQ(buf[36], 0x00); /* Stop 0 delta: 0 */
	ASSERT_EQ(buf[37], 0x00); /* Stop 1: Black */
	ASSERT_EQ(buf[38], 0x00);
	ASSERT_EQ(buf[39], 0x00);
	ASSERT_EQ(buf[40], 127); /* Stop 1 delta: 50% = 127 */
	ASSERT_EQ(buf[41], 0xFF); /* Stop 2: Red */
	ASSERT_EQ(buf[44], 128); /* Stop 2 delta: 100% = 128 */

	/* Effects: Color Shift / Rainbow (4 stops, 49 bytes) */
	alloy_devcfg(cfg)->zone_fx[0] = 3;
	len = r600_build_zone_color(cfg, 0, buf);
	ASSERT_EQ(len, 49);
	ASSERT_EQ(buf[29], 0x04);
	ASSERT_EQ(buf[30], 0xFF); /* Initial Red */
	ASSERT_EQ(buf[33], 0xFF); /* Stop 0 Red */
	ASSERT_EQ(buf[38], 0xFF); /* Stop 1 Green */
	ASSERT_EQ(buf[43], 0xFF); /* Stop 2 Blue */
	ASSERT_EQ(buf[45], 0xFF); /* Stop 3 Red */

	alloy_config_free(cfg);
}

ALLOY_TEST(test_r600_buttons_packets)
{
	const struct alloy_driver *drv = drv_r600();
	struct alloy_config *cfg = alloy_config_alloc(drv);
	uint8_t buf[ALLOY_HID_REPORT_SIZE];
	size_t len;

	alloy_config_defaults(drv, cfg);
	struct alloy_mouse_config *m = alloy_mouse_cfg(cfg);

	/* Set button 0..6 actions */
	m->buttons[0] = (struct alloy_action){ ALLOY_ACT_MOUSE, 1 };
	m->buttons[1] = (struct alloy_action){ ALLOY_ACT_MOUSE, 2 };
	m->buttons[2] = (struct alloy_action){ ALLOY_ACT_MOUSE, 3 };
	m->buttons[3] = (struct alloy_action){ ALLOY_ACT_MOUSE, 4 };
	m->buttons[4] = (struct alloy_action){ ALLOY_ACT_MOUSE, 5 };
	m->buttons[5] =
		(struct alloy_action){ ALLOY_ACT_KEYBOARD, 0x04 }; /* Key 'A' */
	m->buttons[6] = (struct alloy_action){ ALLOY_ACT_DPI_CYCLE, 0 };

	len = r600_build_buttons(cfg, buf);
	ASSERT_EQ(len, 37);
	ASSERT_EQ(buf[0], 0x31);
	ASSERT_EQ(buf[1], 0x00);

	/* Button 1 (Left): offset 2 */
	ASSERT_EQ(buf[2], 0x01);
	/* Button 5 (Side Forward): offset 2 + 4*5 = 22 */
	ASSERT_EQ(buf[22], 0x05);
	/* Button 6 (Keyboard): offset 2 + 5*5 = 27 */
	ASSERT_EQ(buf[27], 0x51);
	ASSERT_EQ(buf[28], 0x04);
	/* Button 7 (CPI): offset 2 + 6*5 = 32 */
	ASSERT_EQ(buf[32], 0x30);

	alloy_config_free(cfg);
}

ALLOY_TEST(test_r600_save_packet)
{
	uint8_t buf[ALLOY_HID_REPORT_SIZE];
	size_t len = r600_build_save(buf);

	ASSERT_EQ(len, 2);
	ASSERT_EQ(buf[0], 0x09);
	ASSERT_EQ(buf[1], 0x00);
}
