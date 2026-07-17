// SPDX-License-Identifier: GPL-2.0-only
/*
 * Unit tests:
 * driver packet builders, registry and baseline state round-trip.
 *
 * No hardware involved - HID layer is mocked.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver.h"
#include "state.h"
#include "mock_hid.h"

/* pure packet builders exported by the Rival 3 Gen 2 driver */
size_t r3g2_build_dpi(const struct alloy_config *cfg, uint8_t *buf);
size_t r3g2_build_polling(const struct alloy_config *cfg, uint8_t *buf);
size_t r3g2_build_colors(const struct alloy_config *cfg, uint8_t *buf);
size_t r3g2_build_rainbow(const struct alloy_config *cfg, uint8_t *buf);
size_t r3g2_build_reactive(const struct alloy_config *cfg, uint8_t *buf);
size_t r3g2_build_startup(const struct alloy_config *cfg, uint8_t *buf);
size_t r3g2_build_brightness(const struct alloy_config *cfg, uint8_t *buf);
size_t r3g2_build_buttons(const struct alloy_config *cfg, uint8_t *buf);

size_t r3_build_dpi(const struct alloy_config *cfg, uint8_t *buf);
size_t r3_build_polling(const struct alloy_config *cfg, uint8_t *buf);
size_t r3_build_zone_color(const struct alloy_config *cfg, int zone,
			   uint8_t *buf);
size_t r3_build_effect(const struct alloy_config *cfg, uint8_t *buf);
size_t r3_build_buttons(const struct alloy_config *cfg, uint8_t *buf);

static int failures;

#define ASSERT_TRUE(cond)                                                      \
	do {                                                                   \
		if (!(cond)) {                                                 \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
			failures++;                                            \
		}                                                              \
	} while (0)

#define ASSERT_EQ(a, b)                                               \
	do {                                                          \
		long __a = (long)(a);                                 \
		long __b = (long)(b);                                 \
		if (__a != __b) {                                     \
			printf("FAIL %s:%d: %s == %s (%ld != %ld)\n", \
			       __FILE__, __LINE__, #a, #b, __a, __b); \
			failures++;                                   \
		}                                                     \
	} while (0)

static const struct alloy_driver *r3g2(void)
{
	const struct alloy_driver *drv = alloy_driver_find(0x1038, 0x1870);

	if (!drv) {
		printf("FAIL: rival 3 gen 2 driver not registered\n");
		exit(1);
	}
	return drv;
}

static void test_registry(void)
{
	const struct alloy_driver *drv = r3g2();

	ASSERT_EQ(drv->interface, 3);
	ASSERT_EQ(drv->dpi.min, 200);
	ASSERT_EQ(drv->dpi.max, 8500);
	ASSERT_EQ(drv->num_zones, 3);
	ASSERT_EQ(drv->num_buttons, 8);
	ASSERT_TRUE(alloy_driver_find(0x1038, 0xbaad) == NULL);
}

static void test_dpi_packet(void)
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
	ASSERT_EQ(buf[2], 1); /* active, 1-based on the wire */
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

static void test_polling_packet(void)
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

static void test_colors_packet(void)
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
	cfg.zone_mode[1] = ALLOY_LED_RAINBOW;
	ASSERT_EQ(r3g2_build_colors(&cfg, buf), 11);
	ASSERT_EQ(buf[1], 0x05);
	ASSERT_EQ(buf[8], 0x77); /* zone 3 still in triplet 3 */

	/* all zones on the rainbow: nothing static to send */
	cfg.zone_mode[0] = ALLOY_LED_RAINBOW;
	cfg.zone_mode[2] = ALLOY_LED_RAINBOW;
	ASSERT_EQ(r3g2_build_colors(&cfg, buf), 0);
}

static void test_rainbow_packet(void)
{
	struct alloy_config cfg;
	uint8_t buf[ALLOY_HID_REPORT_SIZE];

	r3g2()->config_defaults(r3g2(), &cfg);

	/* defaults are all static: no rainbow packet */
	ASSERT_EQ(r3g2_build_rainbow(&cfg, buf), 0);

	cfg.zone_mode[0] = ALLOY_LED_RAINBOW;
	cfg.zone_mode[2] = ALLOY_LED_RAINBOW;
	ASSERT_EQ(r3g2_build_rainbow(&cfg, buf), 2);
	ASSERT_EQ(buf[0], 0x22);
	ASSERT_EQ(buf[1], 0x05);
}

static void test_reactive_packet(void)
{
	struct alloy_config cfg;
	uint8_t buf[ALLOY_HID_REPORT_SIZE];

	r3g2()->config_defaults(r3g2(), &cfg);

	cfg.reactive_enabled = 1;
	cfg.reactive_color = (struct alloy_rgb){ 0x12, 0x34, 0x56 };
	ASSERT_EQ(r3g2_build_reactive(&cfg, buf), 4);
	ASSERT_EQ(buf[0], 0x26);
	ASSERT_EQ(buf[1], 0x12);
	ASSERT_EQ(buf[2], 0x34);
	ASSERT_EQ(buf[3], 0x56);

	/* disabled: all-zero payload turns the effect off */
	cfg.reactive_enabled = 0;
	r3g2_build_reactive(&cfg, buf);
	ASSERT_EQ(buf[1], 0x00);
	ASSERT_EQ(buf[2], 0x00);
	ASSERT_EQ(buf[3], 0x00);
}

static void test_startup_packet(void)
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
}

static void test_buttons_packet(void)
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

static void test_brightness_packet(void)
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

static void test_state_roundtrip(void)
{
	const struct alloy_driver *drv = r3g2();
	struct alloy_config out;
	struct alloy_config in;
	char tmpl[] = "/tmp/alloyctl-test-XXXXXX";

	if (!mkdtemp(tmpl)) {
		printf("FAIL: mkdtemp\n");
		failures++;
		return;
	}
	setenv("XDG_CONFIG_HOME", tmpl, 1);

	/* nothing stored yet: defaults, return 1 */
	ASSERT_EQ(alloy_state_load(drv, &out), 1);
	ASSERT_EQ(out.dpi[0][0], 800);

	drv->config_defaults(drv, &in);
	in.dpi[0][0] = 2300;
	in.dpi[0][1] = 2300;
	in.dpi_active = 1;
	in.polling_hz = 250;
	in.zone_color[2] = (struct alloy_rgb){ 0xAB, 0xCD, 0xEF };
	in.zone_mode[1] = ALLOY_LED_RAINBOW;
	in.reactive_enabled = 1;
	in.reactive_color = (struct alloy_rgb){ 0x10, 0x20, 0x30 };
	in.startup_fx = ALLOY_STARTUP_REACTIVE_RAINBOW;
	in.brightness = 42;
	in.buttons[5].type = ALLOY_ACT_KEYBOARD;
	in.buttons[5].value = 0x29;

	ASSERT_EQ(alloy_state_store(drv, &in), 0);
	ASSERT_EQ(alloy_state_load(drv, &out), 0);

	ASSERT_EQ(out.dpi[0][0], 2300);
	ASSERT_EQ(out.dpi_active, 1);
	ASSERT_EQ(out.polling_hz, 250);
	ASSERT_EQ(out.zone_color[2].r, 0xAB);
	ASSERT_EQ(out.zone_color[2].g, 0xCD);
	ASSERT_EQ(out.zone_color[2].b, 0xEF);
	ASSERT_EQ(out.zone_mode[0], ALLOY_LED_STATIC);
	ASSERT_EQ(out.zone_mode[1], ALLOY_LED_RAINBOW);
	ASSERT_EQ(out.reactive_enabled, 1);
	ASSERT_EQ(out.reactive_color.g, 0x20);
	ASSERT_EQ(out.startup_fx, ALLOY_STARTUP_REACTIVE_RAINBOW);
	ASSERT_EQ(out.brightness, 42);

	/* reactive=off round-trips to disabled */
	in.reactive_enabled = 0;
	ASSERT_EQ(alloy_state_store(drv, &in), 0);
	ASSERT_EQ(alloy_state_load(drv, &out), 0);
	ASSERT_EQ(out.reactive_enabled, 0);
	ASSERT_EQ(out.buttons[5].type, ALLOY_ACT_KEYBOARD);
	ASSERT_EQ(out.buttons[5].value, 0x29);
}

static const struct alloy_driver *gen1(void)
{
	const struct alloy_driver *drv = alloy_driver_find(0x1038, 0x184C);

	if (!drv) {
		printf("FAIL: rival 3 gen 1 driver not registered\n");
		exit(1);
	}
	return drv;
}

static void test_gen1_registry(void)
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

static void test_gen1_dpi_packet(void)
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

static void test_gen1_zone_color_packet(void)
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

static void test_gen1_effect_packet(void)
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

static void test_gen1_buttons_packet(void)
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

static void test_ops_use_mock(void)
{
	const struct alloy_driver *drv = r3g2();
	struct alloy_device dev;
	struct alloy_config cfg;

	mock_hid_reset();
	dev.drv = drv;
	dev.hid.fd = 42;
	drv->config_defaults(drv, &cfg);

	ASSERT_EQ(drv->ops->apply_dpi(&dev, &cfg), 0);
	ASSERT_EQ(mock_hid.num_cmds, 1);
	ASSERT_EQ(mock_hid.cmds[0].payload[0], 0x34);

	ASSERT_EQ(drv->ops->save(&dev), 0);
	ASSERT_EQ(mock_hid.cmds[1].payload[0], 0x11);
	ASSERT_EQ(mock_hid.cmds[1].len, 2);
}

int main(void)
{
	test_registry();
	test_dpi_packet();
	test_polling_packet();
	test_colors_packet();
	test_rainbow_packet();
	test_reactive_packet();
	test_startup_packet();
	test_buttons_packet();
	test_brightness_packet();
	test_gen1_registry();
	test_gen1_dpi_packet();
	test_gen1_zone_color_packet();
	test_gen1_effect_packet();
	test_gen1_buttons_packet();
	test_state_roundtrip();
	test_ops_use_mock();

	if (failures) {
		printf("%d failure(s)\n", failures);
		return 1;
	}
	printf("all tests passed\n");
	return 0;
}
