// SPDX-License-Identifier: GPL-2.0-only
/*
 * SteelSeries Apex 100 Gaming Keyboard (1038:160E) driver tests.
 *
 * Exercises the pure packet builders against the exact byte sequences verified on hardware.
 *
 * Protocol reference: Documentation/protocol/steelseries-apex100.rst.
 */
#include <stdlib.h>
#include <string.h>

#include "driver.h"
#include "test.h"

size_t apex100_build_polling(uint16_t polling_hz, uint8_t *buf);
size_t apex100_build_brightness(uint8_t brightness, uint8_t *buf);
size_t apex100_build_effect(uint8_t fx_mode, uint8_t fx_speed, uint8_t *buf);
size_t apex100_build_save(uint8_t *buf);
size_t apex100_build_firmware_query(uint8_t *buf);

static const struct alloy_driver *apex100(void)
{
	const struct alloy_driver *drv = alloy_driver_find(0x1038, 0x160E);

	if (!drv) {
		printf("FAIL: Apex 100 driver not registered\n");
		exit(1);
	}
	return drv;
}

ALLOY_TEST(test_apex100_registry)
{
	const struct alloy_driver *drv = apex100();

	ASSERT_EQ(drv->vendor_id, 0x1038);
	ASSERT_EQ(drv->product_id, 0x160E);
	ASSERT_EQ(drv->type, ALLOY_DEV_KEYBOARD);
	ASSERT_EQ(drv->interface, 1);
	ASSERT_EQ(drv->report_size, 32);
	ASSERT_EQ(drv->num_zones, 1);
	ASSERT_EQ(drv->num_fx, 2);
	ASSERT_TRUE(drv->caps & ALLOY_CAP_BRIGHTNESS);
	ASSERT_TRUE(drv->caps & ALLOY_CAP_FIRMWARE_VERSION);
	ASSERT_TRUE(drv->caps & ALLOY_CAP_FX_GLOBAL);
	ASSERT_TRUE(drv->caps & ALLOY_CAP_FX_SPEED);
	ASSERT_TRUE(!(drv->caps & ALLOY_CAP_COLOR));
	ASSERT_TRUE(!(drv->caps & ALLOY_CAP_FX_FREQ));
	ASSERT_TRUE(!(drv->caps & ALLOY_CAP_WIN_LOCK));
}

ALLOY_TEST(test_apex100_polling_packets)
{
	uint8_t buf[32];
	size_t len;

	len = apex100_build_polling(1000, buf);
	ASSERT_EQ(len, 32);
	ASSERT_EQ(buf[0], 0x04);
	ASSERT_EQ(buf[1], 0x00);
	ASSERT_EQ(buf[2], 0x01);

	len = apex100_build_polling(500, buf);
	ASSERT_EQ(len, 32);
	ASSERT_EQ(buf[0], 0x04);
	ASSERT_EQ(buf[1], 0x00);
	ASSERT_EQ(buf[2], 0x02);

	len = apex100_build_polling(250, buf);
	ASSERT_EQ(len, 32);
	ASSERT_EQ(buf[0], 0x04);
	ASSERT_EQ(buf[1], 0x00);
	ASSERT_EQ(buf[2], 0x03);

	len = apex100_build_polling(125, buf);
	ASSERT_EQ(len, 32);
	ASSERT_EQ(buf[0], 0x04);
	ASSERT_EQ(buf[1], 0x00);
	ASSERT_EQ(buf[2], 0x04);
}

ALLOY_TEST(test_apex100_brightness_packets)
{
	uint8_t buf[32];
	size_t len;

	/* 0% -> 0x00 */
	len = apex100_build_brightness(0, buf);
	ASSERT_EQ(len, 32);
	ASSERT_EQ(buf[0], 0x05);
	ASSERT_EQ(buf[1], 0x00);
	ASSERT_EQ(buf[2], 0x00);

	/* 50% -> 50 (0x32) */
	len = apex100_build_brightness(50, buf);
	ASSERT_EQ(len, 32);
	ASSERT_EQ(buf[0], 0x05);
	ASSERT_EQ(buf[1], 0x00);
	ASSERT_EQ(buf[2], 50);

	/* 100% -> 100 (0x64) */
	len = apex100_build_brightness(100, buf);
	ASSERT_EQ(len, 32);
	ASSERT_EQ(buf[0], 0x05);
	ASSERT_EQ(buf[1], 0x00);
	ASSERT_EQ(buf[2], 100);
}

ALLOY_TEST(test_apex100_effect_packets)
{
	uint8_t buf[32];
	size_t len;

	/* Steady (mode 0) -> 0x07 0x00 0x01 */
	len = apex100_build_effect(0, 1, buf);
	ASSERT_EQ(len, 32);
	ASSERT_EQ(buf[0], 0x07);
	ASSERT_EQ(buf[1], 0x00);
	ASSERT_EQ(buf[2], 0x01);

	/* Breath (mode 1), speed 1 (slow) -> 0x07 0x00 0x02 */
	len = apex100_build_effect(1, 1, buf);
	ASSERT_EQ(len, 32);
	ASSERT_EQ(buf[0], 0x07);
	ASSERT_EQ(buf[1], 0x00);
	ASSERT_EQ(buf[2], 0x02);

	/* Breath (mode 1), speed 2 (med) -> 0x07 0x00 0x03 */
	len = apex100_build_effect(1, 2, buf);
	ASSERT_EQ(len, 32);
	ASSERT_EQ(buf[0], 0x07);
	ASSERT_EQ(buf[1], 0x00);
	ASSERT_EQ(buf[2], 0x03);

	/* Breath (mode 1), speed 3 (fast) -> 0x07 0x00 0x04 */
	len = apex100_build_effect(1, 3, buf);
	ASSERT_EQ(len, 32);
	ASSERT_EQ(buf[0], 0x07);
	ASSERT_EQ(buf[1], 0x00);
	ASSERT_EQ(buf[2], 0x04);
}

ALLOY_TEST(test_apex100_save_and_firmware_packets)
{
	uint8_t buf[32];
	size_t len;

	len = apex100_build_save(buf);
	ASSERT_EQ(len, 32);
	ASSERT_EQ(buf[0], 0x09);
	ASSERT_EQ(buf[1], 0x00);

	len = apex100_build_firmware_query(buf);
	ASSERT_EQ(len, 32);
	ASSERT_EQ(buf[0], 0x10);
	ASSERT_EQ(buf[1], 0x00);
}
