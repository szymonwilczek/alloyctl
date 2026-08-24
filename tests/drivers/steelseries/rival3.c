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
#include "hid.h"
#include "lib/mouse.h"
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
	const struct alloy_hid_params *hid = drv->transport_data;
	const struct alloy_devinfo *info = alloy_devinfo(drv);

	ASSERT_TRUE(alloy_driver_find(0x1038, 0x1824) != NULL);
	ASSERT_EQ(hid->interface, 3);
	ASSERT_EQ(hid->report_size, 32);
	ASSERT_EQ(info->num_zones, 4); /* strip + logo */
	ASSERT_EQ(info->num_fx, 7);
	ASSERT_TRUE(info->caps & ALLOY_CAP_FX_GLOBAL);
	ASSERT_TRUE(info->caps & ALLOY_CAP_ACCEL);
}

ALLOY_TEST(test_gen1_dpi_packet)
{
	const struct alloy_driver *drv = gen1();
	struct alloy_config *cfg = alloy_config_alloc(drv);
	uint8_t buf[ALLOY_HID_REPORT_SIZE];
	size_t len;

	alloy_config_defaults(drv, cfg);
	struct alloy_mouse_config *m = alloy_mouse_cfg(cfg);
	m->dpi_count = 2;
	m->dpi_active = 1;
	m->dpi[0][0] = 800;
	m->dpi[1][0] = 1600;

	len = r3_build_dpi(cfg, buf);
	ASSERT_EQ(len, 6);
	ASSERT_EQ(buf[0], 0x0B);
	ASSERT_EQ(buf[1], 0x00);
	ASSERT_EQ(buf[2], 2); /* count */
	ASSERT_EQ(buf[3], 1); /* active, 0-based on wire */
	ASSERT_EQ(buf[4], 0x12); /* 800 dpi, one byte per preset */
	ASSERT_EQ(buf[5], 0x24); /* 1600 dpi */

	alloy_config_free(cfg);
}

ALLOY_TEST(test_gen1_zone_color_packet)
{
	const struct alloy_driver *drv = gen1();
	struct alloy_config *cfg = alloy_config_alloc(drv);
	uint8_t buf[ALLOY_HID_REPORT_SIZE];

	alloy_config_defaults(drv, cfg);
	struct alloy_mouse_config *m = alloy_mouse_cfg(cfg);
	m->dev.zone_color[3] = (struct alloy_rgb){ 0x00, 0xFF, 0x88 };
	m->dev.brightness = 55;

	/* logo is zone index 3 -> wire id 0x04 */
	ASSERT_EQ(r3_build_zone_color(cfg, 3, buf), 7);
	ASSERT_EQ(buf[0], 0x05);
	ASSERT_EQ(buf[1], 0x00);
	ASSERT_EQ(buf[2], 0x04);
	ASSERT_EQ(buf[3], 0x00);
	ASSERT_EQ(buf[4], 0xFF);
	ASSERT_EQ(buf[5], 0x88);
	ASSERT_EQ(buf[6], 55); /* brightness rides in every write */

	alloy_config_free(cfg);
}

ALLOY_TEST(test_gen1_effect_packet)
{
	const struct alloy_driver *drv = gen1();
	struct alloy_config *cfg = alloy_config_alloc(drv);
	uint8_t buf[ALLOY_HID_REPORT_SIZE];
	/* fx_names order -> wire values. */
	static const uint8_t wire[] = {
		0x04, 0x03, 0x02, 0x01, 0x00, 0x05, 0x06
	};
	size_t i;

	alloy_config_defaults(drv, cfg);
	struct alloy_mouse_config *m = alloy_mouse_cfg(cfg);
	for (i = 0; i < ALLOY_ARRAY_SIZE(wire); i++) {
		m->dev.zone_fx[0] = (uint8_t)i;
		ASSERT_EQ(r3_build_effect(cfg, buf), 3);
		ASSERT_EQ(buf[0], 0x06);
		ASSERT_EQ(buf[1], 0x00);
		ASSERT_EQ(buf[2], wire[i]);
	}

	/* device-wide selector: first zone not running steady wins */
	m->dev.zone_fx[0] = 0;
	m->dev.zone_fx[2] = 4; /* rainbow shift */
	r3_build_effect(cfg, buf);
	ASSERT_EQ(buf[2], 0x00);

	/* out-of-range index falls back to steady */
	m->dev.zone_fx[0] = 99;
	r3_build_effect(cfg, buf);
	ASSERT_EQ(buf[2], 0x04);

	alloy_config_free(cfg);
}

ALLOY_TEST(test_gen1_buttons_packet)
{
	const struct alloy_driver *drv = gen1();
	struct alloy_config *cfg = alloy_config_alloc(drv);
	uint8_t buf[ALLOY_HID_REPORT_SIZE];

	alloy_config_defaults(drv, cfg);
	struct alloy_mouse_config *m = alloy_mouse_cfg(cfg);
	ASSERT_EQ(r3_build_buttons(cfg, buf), 18);
	ASSERT_EQ(buf[0], 0x07);
	ASSERT_EQ(buf[1], 0x00);
	/* factory mapping: 2-byte fields */
	ASSERT_EQ(buf[2 + 0 * 2], 0x01);
	ASSERT_EQ(buf[2 + 4 * 2], 0x05);
	ASSERT_EQ(buf[2 + 5 * 2], 0x30); /* CPI toggle */
	ASSERT_EQ(buf[2 + 6 * 2], 0x31); /* scroll up */
	ASSERT_EQ(buf[2 + 7 * 2], 0x32); /* scroll down */

	/* keyboard rebind uses 0x33 on this protocol family */
	m->buttons[3].type = ALLOY_ACT_KEYBOARD;
	m->buttons[3].value = 0x04;
	r3_build_buttons(cfg, buf);
	ASSERT_EQ(buf[2 + 3 * 2], 0x33);
	ASSERT_EQ(buf[2 + 3 * 2 + 1], 0x04);

	alloy_config_free(cfg);
}
