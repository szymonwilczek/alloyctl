// SPDX-License-Identifier: GPL-2.0-only
/*
 * SteelSeries Rival 3 Gen 2 (1038:1870) driver tests.
 *
 * Exercises the pure packet builders against the exact byte sequences verified on hardware.
 *
 * Protocol reference: Documentation/protocol/steelseries-rival3-gen2.rst.
 */
#include <stdlib.h>

#include "driver.h"
#include "test.h"

size_t r3g2_build_dpi(const struct alloy_config *cfg, uint8_t *buf);
size_t r3g2_build_polling(const struct alloy_config *cfg, uint8_t *buf);
size_t r3g2_build_colors(const struct alloy_config *cfg, uint8_t *buf);
size_t r3g2_build_rainbow(const struct alloy_config *cfg, uint8_t *buf);
size_t r3g2_build_reactive(const struct alloy_config *cfg, uint8_t *buf);
size_t r3g2_build_startup(const struct alloy_config *cfg, uint8_t *buf);
size_t r3g2_build_brightness(const struct alloy_config *cfg, uint8_t *buf);
size_t r3g2_build_buttons(const struct alloy_config *cfg, uint8_t *buf);
int r3g2_parse_event(const uint8_t *buf, size_t len, struct alloy_config *cfg);

static const struct alloy_driver *r3g2(void)
{
	const struct alloy_driver *drv = alloy_driver_find(0x1038, 0x1870);

	if (!drv) {
		printf("FAIL: rival 3 gen 2 driver not registered\n");
		exit(1);
	}
	return drv;
}

ALLOY_TEST(test_registry)
{
	const struct alloy_driver *drv = r3g2();

	ASSERT_EQ(drv->interface, 3);
	ASSERT_EQ(drv->dpi.min, 200);
	ASSERT_EQ(drv->dpi.max, 8500);
	ASSERT_EQ(drv->num_zones, 3);
	ASSERT_EQ(drv->num_buttons, 8);
	ASSERT_EQ(drv->num_fx, 2); /* steady + rainbow, per zone */
	ASSERT_TRUE(alloy_driver_find(0x1038, 0xbaad) == NULL);
}

ALLOY_TEST(test_dpi_packet)
{
	struct alloy_config cfg;
	uint8_t buf[ALLOY_HID_REPORT_SIZE];
	size_t len;

	r3g2()->config_defaults(r3g2(), &cfg);
	cfg.dpi_count = 2;
	cfg.dpi_active = 0;
	cfg.dpi[0][0] = 800;
	cfg.dpi[0][1] = 800;
	cfg.dpi[1][0] = 1600;
	cfg.dpi[1][1] = 1600;

	len = r3g2_build_dpi(&cfg, buf);
	ASSERT_EQ(len, 7);
	ASSERT_EQ(buf[0], 0x34);
	ASSERT_EQ(buf[1], 2); /* preset count */
	ASSERT_EQ(buf[2], 0); /* active, 0-based on the wire */
	ASSERT_EQ(buf[3], 0x12); /* 800 dpi */
	ASSERT_EQ(buf[4], 0x12);
	ASSERT_EQ(buf[5], 0x24); /* 1600 dpi */
	ASSERT_EQ(buf[6], 0x24);

	/* boundaries of TrueMove Core table */
	cfg.dpi_count = 1;
	cfg.dpi[0][0] = 200;
	cfg.dpi[0][1] = 8500;
	len = r3g2_build_dpi(&cfg, buf);
	ASSERT_EQ(len, 5);
	ASSERT_EQ(buf[3], 0x04);
	ASSERT_EQ(buf[4], 0xC5);

	/* out-of-range values clamp instead of overflowing the table */
	cfg.dpi[0][0] = 50;
	cfg.dpi[0][1] = 60000;
	r3g2_build_dpi(&cfg, buf);
	ASSERT_EQ(buf[3], 0x04);
	ASSERT_EQ(buf[4], 0xC5);
}

ALLOY_TEST(test_polling_packet)
{
	struct alloy_config cfg;
	uint8_t buf[ALLOY_HID_REPORT_SIZE];
	static const struct {
		uint16_t hz;
		uint8_t wire;
	} cases[] = {
		{ 1000, 0x01 },
		{ 500, 0x02 },
		{ 250, 0x03 },
		{ 125, 0x04 },
	};
	size_t i;

	r3g2()->config_defaults(r3g2(), &cfg);
	for (i = 0; i < ALLOY_ARRAY_SIZE(cases); i++) {
		cfg.polling_hz = cases[i].hz;
		ASSERT_EQ(r3g2_build_polling(&cfg, buf), 2);
		ASSERT_EQ(buf[0], 0x2B);
		ASSERT_EQ(buf[1], cases[i].wire);
	}
}

ALLOY_TEST(test_colors_packet)
{
	struct alloy_config cfg;
	uint8_t buf[ALLOY_HID_REPORT_SIZE];

	r3g2()->config_defaults(r3g2(), &cfg);
	cfg.zone_color[0] = (struct alloy_rgb){ 0x11, 0x22, 0x33 };
	cfg.zone_color[1] = (struct alloy_rgb){ 0x44, 0x55, 0x66 };
	cfg.zone_color[2] = (struct alloy_rgb){ 0x77, 0x88, 0x99 };

	ASSERT_EQ(r3g2_build_colors(&cfg, buf), 11);
	ASSERT_EQ(buf[0], 0x21);
	ASSERT_EQ(buf[1], 0x07); /* all three zones static */
	ASSERT_EQ(buf[2], 0x11);
	ASSERT_EQ(buf[3], 0x22);
	ASSERT_EQ(buf[4], 0x33);
	ASSERT_EQ(buf[8], 0x77);
	ASSERT_EQ(buf[10], 0x99);

	/* rainbow on the middle zone drops it from the static mask
	 * but keeps the RGB triplets positional */
	cfg.zone_fx[1] = 1;
	ASSERT_EQ(r3g2_build_colors(&cfg, buf), 11);
	ASSERT_EQ(buf[1], 0x05);
	ASSERT_EQ(buf[8], 0x77); /* zone 3 still in triplet 3 */

	/* all zones on the rainbow: nothing static to send */
	cfg.zone_fx[0] = 1;
	cfg.zone_fx[2] = 1;
	ASSERT_EQ(r3g2_build_colors(&cfg, buf), 0);
}

ALLOY_TEST(test_rainbow_packet)
{
	struct alloy_config cfg;
	uint8_t buf[ALLOY_HID_REPORT_SIZE];

	r3g2()->config_defaults(r3g2(), &cfg);

	/* defaults are all static: no rainbow packet */
	ASSERT_EQ(r3g2_build_rainbow(&cfg, buf), 0);

	cfg.zone_fx[0] = 1;
	cfg.zone_fx[2] = 1;
	ASSERT_EQ(r3g2_build_rainbow(&cfg, buf), 2);
	ASSERT_EQ(buf[0], 0x22);
	ASSERT_EQ(buf[1], 0x05);
}

ALLOY_TEST(test_reactive_packet)
{
	struct alloy_config cfg;
	uint8_t buf[ALLOY_HID_REPORT_SIZE];

	r3g2()->config_defaults(r3g2(), &cfg);

	cfg.reactive_enabled = 1;
	cfg.reactive_color = (struct alloy_rgb){ 0x12, 0x34, 0x56 };
	ASSERT_EQ(r3g2_build_reactive(&cfg, buf), 6);
	ASSERT_EQ(buf[0], 0x26);
	ASSERT_EQ(buf[1], 0x01); /* enable byte, hardware-verified (#24) */
	ASSERT_EQ(buf[2], 0x00);
	ASSERT_EQ(buf[3], 0x12);
	ASSERT_EQ(buf[4], 0x34);
	ASSERT_EQ(buf[5], 0x56);

	/* disabled: all-zero payload turns the effect off */
	cfg.reactive_enabled = 0;
	r3g2_build_reactive(&cfg, buf);
	ASSERT_EQ(buf[1], 0x00);
	ASSERT_EQ(buf[3], 0x00);
	ASSERT_EQ(buf[2], 0x00);
	ASSERT_EQ(buf[3], 0x00);
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

	r3g2()->config_defaults(r3g2(), &cfg);
	for (i = 0; i < ALLOY_ARRAY_SIZE(cases); i++) {
		cfg.startup_fx = cases[i].fx;
		ASSERT_EQ(r3g2_build_startup(&cfg, buf), 3);
		ASSERT_EQ(buf[0], 0x27);
		ASSERT_EQ(buf[1], cases[i].rainbow);
		ASSERT_EQ(buf[2], cases[i].reactive);
	}

	/*
	 * rainbow byte doubles as the live engine switch (#23):
	 * any zone running the rainbow forces it on regardless of
	 * the startup choice, or 0x22 masks would be silently ignored
	 */
	cfg.zone_fx[1] = 1;
	for (i = 0; i < ALLOY_ARRAY_SIZE(cases); i++) {
		cfg.startup_fx = cases[i].fx;
		r3g2_build_startup(&cfg, buf);
		ASSERT_EQ(buf[1], 1);
		ASSERT_EQ(buf[2], cases[i].reactive);
	}
}

ALLOY_TEST(test_buttons_packet)
{
	struct alloy_config cfg;
	uint8_t buf[ALLOY_HID_REPORT_SIZE];
	size_t len;

	r3g2()->config_defaults(r3g2(), &cfg);
	len = r3g2_build_buttons(&cfg, buf);
	ASSERT_EQ(len, 41);
	ASSERT_EQ(buf[0], 0x2A);
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
	r3g2_build_buttons(&cfg, buf);
	ASSERT_EQ(buf[1 + 0x0F], 0x51);
	ASSERT_EQ(buf[1 + 0x0F + 1], 0x04);

	/* disable button 5 */
	cfg.buttons[4].type = ALLOY_ACT_DISABLED;
	r3g2_build_buttons(&cfg, buf);
	ASSERT_EQ(buf[1 + 0x14], 0x00);
}

ALLOY_TEST(test_brightness_packet)
{
	struct alloy_config cfg;
	uint8_t buf[ALLOY_HID_REPORT_SIZE];

	r3g2()->config_defaults(r3g2(), &cfg);
	cfg.brightness = 55;
	ASSERT_EQ(r3g2_build_brightness(&cfg, buf), 2);
	ASSERT_EQ(buf[0], 0x23);
	ASSERT_EQ(buf[1], 55);

	cfg.brightness = 255; /* clamps to 100 */
	r3g2_build_brightness(&cfg, buf);
	ASSERT_EQ(buf[1], 100);
}

ALLOY_TEST(test_cpi_level_event)
{
	/* exact notification captured on hardware: levels 800/900/1800 */
	uint8_t evt[64] = { 0xAD, 0x03, 0x01, 0x12, 0x14, 0x29 };
	struct alloy_config cfg;

	r3g2()->config_defaults(r3g2(), &cfg);
	cfg.dpi_count = 3;
	cfg.dpi_active = 0;

	/* hardware switched to level 2 (0-based 1) */
	ASSERT_EQ(r3g2_parse_event(evt, sizeof(evt), &cfg), 1);
	ASSERT_EQ(cfg.dpi_active, 1);

	/* same level again: no change to report */
	ASSERT_EQ(r3g2_parse_event(evt, sizeof(evt), &cfg), 0);
	ASSERT_EQ(cfg.dpi_active, 1);

	/* not the CPI notification */
	evt[0] = 0x21;
	ASSERT_EQ(r3g2_parse_event(evt, sizeof(evt), &cfg), 0);
	evt[0] = 0xAD;

	/* truncated report */
	ASSERT_EQ(r3g2_parse_event(evt, 2, &cfg), 0);

	/* active out of the report's own range */
	evt[2] = 0x03;
	ASSERT_EQ(r3g2_parse_event(evt, sizeof(evt), &cfg), 0);

	/* active beyond what the host config knows: ignored, not clamped */
	evt[1] = 0x05;
	evt[2] = 0x04;
	ASSERT_EQ(r3g2_parse_event(evt, sizeof(evt), &cfg), 0);
	ASSERT_EQ(cfg.dpi_active, 1);
}
