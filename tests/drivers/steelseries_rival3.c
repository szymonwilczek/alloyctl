// SPDX-License-Identifier: GPL-2.0-only
/*
 * SteelSeries Rival 3 Gen 1 (1038:1824 / 1038:184C) driver tests.
 *
 * Exercises the pure packet builders against the exact byte sequences verified on hardware.
 *
 * Protocol reference: Documentation/protocol/steelseries-rival3.rst.
 */
#include <stdlib.h>

#include "driver.h"
#include "test.h"

size_t r3_build_dpi(const struct alloy_config *cfg, uint8_t *buf);
size_t r3_build_polling(const struct alloy_config *cfg, uint8_t *buf);
size_t r3_build_zone_color(const struct alloy_config *cfg, int zone,
			   uint8_t *buf);
size_t r3_build_effect(const struct alloy_config *cfg, uint8_t *buf);
size_t r3_build_buttons(const struct alloy_config *cfg, uint8_t *buf);

static const struct alloy_driver *gen1(void)
{
	const struct alloy_driver *drv = alloy_driver_find(0x1038, 0x184C);

	if (!drv) {
		printf("FAIL: rival 3 gen 1 driver not registered\n");
		exit(1);
	}
	return drv;
}

ALLOY_TEST(test_gen1_registry)
{
	const struct alloy_driver *drv = gen1();

	ASSERT_TRUE(alloy_driver_find(0x1038, 0x1824) != NULL);
	ASSERT_EQ(drv->interface, 3);
	ASSERT_EQ(drv->report_size, 32);
	ASSERT_EQ(drv->num_zones, 4); /* strip + logo */
	ASSERT_EQ(drv->num_fx, 7);
	ASSERT_TRUE(drv->caps & ALLOY_CAP_FX_GLOBAL);
	ASSERT_TRUE(!(drv->caps & ALLOY_CAP_FX_RAINBOW));
}

ALLOY_TEST(test_gen1_dpi_packet)
{
	struct alloy_config cfg;
	uint8_t buf[ALLOY_HID_REPORT_SIZE];
	size_t len;

	gen1()->config_defaults(gen1(), &cfg);
	cfg.dpi_count = 2;
	cfg.dpi_active = 1;
	cfg.dpi[0][0] = 800;
	cfg.dpi[1][0] = 1600;

	len = r3_build_dpi(&cfg, buf);
	ASSERT_EQ(len, 6);
	ASSERT_EQ(buf[0], 0x0B);
	ASSERT_EQ(buf[1], 0x00);
	ASSERT_EQ(buf[2], 2); /* count */
	ASSERT_EQ(buf[3], 2); /* active, 1-based */
	ASSERT_EQ(buf[4], 0x12); /* 800 dpi, one byte per preset */
	ASSERT_EQ(buf[5], 0x24); /* 1600 dpi */
}

ALLOY_TEST(test_gen1_zone_color_packet)
{
	struct alloy_config cfg;
	uint8_t buf[ALLOY_HID_REPORT_SIZE];

	gen1()->config_defaults(gen1(), &cfg);
	cfg.zone_color[3] = (struct alloy_rgb){ 0x00, 0xFF, 0x88 };
	cfg.brightness = 55;

	/* logo is zone index 3 -> wire id 0x04 */
	ASSERT_EQ(r3_build_zone_color(&cfg, 3, buf), 7);
	ASSERT_EQ(buf[0], 0x05);
	ASSERT_EQ(buf[1], 0x00);
	ASSERT_EQ(buf[2], 0x04);
	ASSERT_EQ(buf[3], 0x00);
	ASSERT_EQ(buf[4], 0xFF);
	ASSERT_EQ(buf[5], 0x88);
	ASSERT_EQ(buf[6], 55); /* brightness rides in every write */
}

ALLOY_TEST(test_gen1_effect_packet)
{
	struct alloy_config cfg;
	uint8_t buf[ALLOY_HID_REPORT_SIZE];
	/* fx_names order -> wire values. */
	static const uint8_t wire[] = {
		0x04, 0x03, 0x02, 0x01, 0x00, 0x05, 0x06
	};
	size_t i;

	gen1()->config_defaults(gen1(), &cfg);
	for (i = 0; i < ALLOY_ARRAY_SIZE(wire); i++) {
		cfg.fx_index = (uint8_t)i;
		ASSERT_EQ(r3_build_effect(&cfg, buf), 3);
		ASSERT_EQ(buf[0], 0x06);
		ASSERT_EQ(buf[1], 0x00);
		ASSERT_EQ(buf[2], wire[i]);
	}

	/* out-of-range index falls back to steady */
	cfg.fx_index = 99;
	r3_build_effect(&cfg, buf);
	ASSERT_EQ(buf[2], 0x04);
}

ALLOY_TEST(test_gen1_buttons_packet)
{
	struct alloy_config cfg;
	uint8_t buf[ALLOY_HID_REPORT_SIZE];

	gen1()->config_defaults(gen1(), &cfg);
	ASSERT_EQ(r3_build_buttons(&cfg, buf), 18);
	ASSERT_EQ(buf[0], 0x07);
	ASSERT_EQ(buf[1], 0x00);
	/* factory mapping: 2-byte fields */
	ASSERT_EQ(buf[2 + 0 * 2], 0x01);
	ASSERT_EQ(buf[2 + 4 * 2], 0x05);
	ASSERT_EQ(buf[2 + 5 * 2], 0x30); /* CPI toggle */
	ASSERT_EQ(buf[2 + 6 * 2], 0x31); /* scroll up */
	ASSERT_EQ(buf[2 + 7 * 2], 0x32); /* scroll down */

	/* keyboard rebind uses 0x33 on this protocol family */
	cfg.buttons[3].type = ALLOY_ACT_KEYBOARD;
	cfg.buttons[3].value = 0x04;
	r3_build_buttons(&cfg, buf);
	ASSERT_EQ(buf[2 + 3 * 2], 0x33);
	ASSERT_EQ(buf[2 + 3 * 2 + 1], 0x04);
}
