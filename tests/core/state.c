// SPDX-License-Identifier: GPL-2.0-only
/*
 * Core (driver-independent) tests:
 * host-side baseline state round-trip and
 * the driver ops wiring over the mocked HID transport.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "driver.h"
#include "state.h"
#include "mock_hid.h"
#include "test.h"

static const struct alloy_driver *r3g2(void)
{
	const struct alloy_driver *drv = alloy_driver_find(0x1038, 0x1870);

	if (!drv) {
		printf("FAIL: rival 3 gen 2 driver not registered\n");
		exit(1);
	}
	return drv;
}

ALLOY_TEST(test_state_roundtrip)
{
	const struct alloy_driver *drv = r3g2();
	struct alloy_config out;
	struct alloy_config in;
	char tmpl[] = "/tmp/alloyctl-test-XXXXXX";

	if (!mkdtemp(tmpl)) {
		printf("FAIL: mkdtemp\n");
		alloy_test_failures++;
		return;
	}
	setenv("XDG_CONFIG_HOME", tmpl, 1);

	/* nothing stored yet: defaults, return 1 */
	ASSERT_EQ(alloy_state_load(drv, &out), 1);
	ASSERT_EQ(out.mouse.dpi[0][0], 800);
	ASSERT_EQ(out.mouse.dpi_count, 1); /* single preset out of the box */
	ASSERT_EQ(out.mouse.dpi_active, 0);

	drv->config_defaults(drv, &in);
	in.mouse.dpi_count = 2;
	in.mouse.dpi[0][0] = 2300;
	in.mouse.dpi[0][1] = 2300;
	in.mouse.dpi[1][0] = 1600;
	in.mouse.dpi[1][1] = 1600;
	in.mouse.dpi_active = 1;
	in.common.polling_hz = 250;
	in.common.zone_color[2] = (struct alloy_rgb){ 0xAB, 0xCD, 0xEF };
	in.common.zone_fx[1] = 1; /* rainbow on this driver */
	in.common.zone_fx_freq[1] = 8;
	in.common.zone_fx_speed[1] = 2;
	in.mouse.reactive_enabled = 1;
	in.mouse.reactive_color = (struct alloy_rgb){ 0x10, 0x20, 0x30 };
	in.mouse.startup_fx = ALLOY_STARTUP_REACTIVE_RAINBOW;
	in.common.brightness = 42;
	in.mouse.buttons[5].type = ALLOY_ACT_KEYBOARD;
	in.mouse.buttons[5].value = 0x29;
	in.mouse.acceleration = 40;
	in.mouse.deceleration = 15;
	in.mouse.angle_snapping = 12;
	in.mouse.accel_enabled = 1;

	ASSERT_EQ(alloy_state_store(drv, &in), 0);
	ASSERT_EQ(alloy_state_load(drv, &out), 0);

	ASSERT_EQ(out.mouse.dpi[0][0], 2300);
	ASSERT_EQ(out.mouse.dpi_count, 2);
	ASSERT_EQ(out.mouse.dpi_active, 1);
	ASSERT_EQ(out.common.polling_hz, 250);
	ASSERT_EQ(out.common.zone_color[2].r, 0xAB);
	ASSERT_EQ(out.common.zone_color[2].g, 0xCD);
	ASSERT_EQ(out.common.zone_color[2].b, 0xEF);
	ASSERT_EQ(out.common.zone_fx[0], 0);
	ASSERT_EQ(out.common.zone_fx[1], 1);
	ASSERT_EQ(out.common.zone_fx_freq[1], 8);
	ASSERT_EQ(out.common.zone_fx_speed[1], 2);
	ASSERT_EQ(out.mouse.reactive_enabled, 1);
	ASSERT_EQ(out.mouse.reactive_color.g, 0x20);
	ASSERT_EQ(out.mouse.startup_fx, ALLOY_STARTUP_REACTIVE_RAINBOW);
	ASSERT_EQ(out.common.brightness, 42);
	ASSERT_EQ(out.mouse.acceleration, 40);
	ASSERT_EQ(out.mouse.deceleration, 15);
	ASSERT_EQ(out.mouse.angle_snapping, 12);
	ASSERT_EQ(out.mouse.accel_enabled, 1);

	/* reactive=off round-trips to disabled */
	in.mouse.reactive_enabled = 0;
	ASSERT_EQ(alloy_state_store(drv, &in), 0);
	ASSERT_EQ(alloy_state_load(drv, &out), 0);
	ASSERT_EQ(out.mouse.reactive_enabled, 0);
	ASSERT_EQ(out.mouse.buttons[5].type, ALLOY_ACT_KEYBOARD);
	ASSERT_EQ(out.mouse.buttons[5].value, 0x29);
}

ALLOY_TEST(test_state_legacy_fx_keys)
{
	const struct alloy_driver *drv = r3g2();
	struct alloy_config out;
	char tmpl[] = "/tmp/alloyctl-test-XXXXXX";
	char path[128];
	FILE *f;

	if (!mkdtemp(tmpl)) {
		printf("FAIL: mkdtemp\n");
		alloy_test_failures++;
		return;
	}
	setenv("XDG_CONFIG_HOME", tmpl, 1);

	snprintf(path, sizeof(path), "%s/alloyctl", tmpl);
	if (mkdir(path, 0755)) {
		printf("FAIL: mkdir\n");
		alloy_test_failures++;
		return;
	}
	snprintf(path, sizeof(path), "%s/alloyctl/1038-1870.conf", tmpl);
	f = fopen(path, "w");
	if (!f) {
		printf("FAIL: fopen\n");
		alloy_test_failures++;
		return;
	}
	/* global fx seeds every zone; explicit zone keys override it */
	fprintf(f, "fx=1\n");
	fprintf(f, "zone_fx1=rainbow\n");
	fprintf(f, "zone_fx2=static\n");
	/* active preset pointing past the count clamps on load */
	fprintf(f, "dpi_count=1\n");
	fprintf(f, "dpi_active=3\n");
	fclose(f);

	ASSERT_EQ(alloy_state_load(drv, &out), 0);
	ASSERT_EQ(out.common.zone_fx[0], 1);
	ASSERT_EQ(out.common.zone_fx[1], 1);
	ASSERT_EQ(out.common.zone_fx[2], 0);
	ASSERT_EQ(out.mouse.dpi_count, 1);
	ASSERT_EQ(out.mouse.dpi_active, 0);
}

ALLOY_TEST(test_keyboard_state_roundtrip)
{
	struct alloy_driver kbd_drv = {
		.name = "Mock Apex Keyboard",
		.vendor_id = 0x1038,
		.product_id = 0x1234,
		.type = ALLOY_DEV_KEYBOARD,
		.caps = ALLOY_CAP_WIN_LOCK | ALLOY_CAP_BRIGHTNESS,
		.num_zones = 1,
		.zones =
			(const struct alloy_led_zone[]){
				{ .name = "MAIN",
				  .def_color = { 0x00, 0x80, 0xFF } } },
		.config_defaults = alloy_config_generic_defaults,
	};
	struct alloy_config in;
	struct alloy_config out;
	char tmpl[] = "/tmp/alloyctl-test-XXXXXX";
	char path[128];
	char line[128];
	int found_win_lock = 0;
	int found_mouse_dpi = 0;
	FILE *f;

	if (!mkdtemp(tmpl)) {
		printf("FAIL: mkdtemp\n");
		alloy_test_failures++;
		return;
	}
	setenv("XDG_CONFIG_HOME", tmpl, 1);

	/* defaults when file is absent */
	ASSERT_EQ(alloy_state_load(&kbd_drv, &out), 1);
	ASSERT_EQ(out.kbd.win_lock, 0);
	ASSERT_EQ(out.common.brightness, 100);

	/* set keyboard state */
	kbd_drv.config_defaults(&kbd_drv, &in);
	in.kbd.win_lock = 1;
	in.common.brightness = 75;
	in.common.polling_hz = 500;
	in.common.zone_color[0] = (struct alloy_rgb){ 0x11, 0x22, 0x33 };

	ASSERT_EQ(alloy_state_store(&kbd_drv, &in), 0);
	ASSERT_EQ(alloy_state_load(&kbd_drv, &out), 0);

	ASSERT_EQ(out.kbd.win_lock, 1);
	ASSERT_EQ(out.common.brightness, 75);
	ASSERT_EQ(out.common.polling_hz, 500);
	ASSERT_EQ(out.common.zone_color[0].r, 0x11);
	ASSERT_EQ(out.common.zone_color[0].g, 0x22);
	ASSERT_EQ(out.common.zone_color[0].b, 0x33);

	/* verify file format contains keyboard keys and no mouse keys */
	snprintf(path, sizeof(path), "%s/alloyctl/1038-1234.conf", tmpl);
	f = fopen(path, "r");
	ASSERT_TRUE(f != NULL);
	while (fgets(line, sizeof(line), f)) {
		if (strstr(line, "win_lock=1"))
			found_win_lock = 1;
		if (strstr(line, "dpi"))
			found_mouse_dpi = 1;
	}
	fclose(f);
	ASSERT_TRUE(found_win_lock);
	ASSERT_TRUE(!found_mouse_dpi);
}

ALLOY_TEST(test_keyboard_state_isolation_and_aliases)
{
	struct alloy_driver kbd_drv = {
		.name = "Mock Apex Keyboard",
		.vendor_id = 0x1038,
		.product_id = 0x5678,
		.type = ALLOY_DEV_KEYBOARD,
		.caps = ALLOY_CAP_WIN_LOCK,
		.config_defaults = alloy_config_generic_defaults,
	};
	struct alloy_config out;
	char tmpl[] = "/tmp/alloyctl-test-XXXXXX";
	char path[128];
	FILE *f;

	if (!mkdtemp(tmpl)) {
		printf("FAIL: mkdtemp\n");
		alloy_test_failures++;
		return;
	}
	setenv("XDG_CONFIG_HOME", tmpl, 1);

	snprintf(path, sizeof(path), "%s/alloyctl", tmpl);
	mkdir(path, 0755);
	snprintf(path, sizeof(path), "%s/alloyctl/1038-5678.conf", tmpl);
	f = fopen(path, "w");
	ASSERT_TRUE(f != NULL);
	/* meta_lock alias for win_lock */
	fprintf(f, "meta_lock=1\n");
	/* mouse-only keys that should be safely ignored */
	fprintf(f, "dpi_count=5\n");
	fprintf(f, "dpi0=1600:1600\n");
	fprintf(f, "acceleration=40\n");
	fclose(f);

	ASSERT_EQ(alloy_state_load(&kbd_drv, &out), 0);
	ASSERT_EQ(out.kbd.win_lock, 1);
}

ALLOY_TEST(test_ops_use_mock)
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
