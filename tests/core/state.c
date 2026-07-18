// SPDX-License-Identifier: GPL-2.0-only
/*
 * Core (driver-independent) tests:
 * host-side baseline state round-trip and
 * the driver ops wiring over the mocked HID transport.
 */
#include <stdio.h>
#include <stdlib.h>
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
	ASSERT_EQ(out.dpi[0][0], 800);

	drv->config_defaults(drv, &in);
	in.dpi[0][0] = 2300;
	in.dpi[0][1] = 2300;
	in.dpi_active = 1;
	in.polling_hz = 250;
	in.zone_color[2] = (struct alloy_rgb){ 0xAB, 0xCD, 0xEF };
	in.zone_fx[1] = 1; /* rainbow on this driver */
	in.zone_fx_freq[1] = 8;
	in.zone_fx_speed[1] = 2;
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
	ASSERT_EQ(out.zone_fx[0], 0);
	ASSERT_EQ(out.zone_fx[1], 1);
	ASSERT_EQ(out.zone_fx_freq[1], 8);
	ASSERT_EQ(out.zone_fx_speed[1], 2);
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
	fclose(f);

	ASSERT_EQ(alloy_state_load(drv, &out), 0);
	ASSERT_EQ(out.zone_fx[0], 1);
	ASSERT_EQ(out.zone_fx[1], 1);
	ASSERT_EQ(out.zone_fx[2], 0);
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
