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
#include "lib/devcfg.h"
#include "lib/keyboard.h"
#include "lib/mouse.h"
#include "mock_hid.h"
#include "state.h"
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
	struct alloy_config *out = alloy_config_alloc(drv);
	struct alloy_config *in = alloy_config_alloc(drv);
	char tmpl[] = "/tmp/alloyctl-test-XXXXXX";

	if (!mkdtemp(tmpl)) {
		printf("FAIL: mkdtemp\n");
		alloy_test_failures++;
		alloy_config_free(out);
		alloy_config_free(in);
		return;
	}
	setenv("XDG_CONFIG_HOME", tmpl, 1);

	/* nothing stored yet: defaults, return 1 */
	ASSERT_EQ(alloy_state_load(drv, out), 1);
	struct alloy_mouse_config *m_out = alloy_mouse_cfg(out);
	ASSERT_EQ(m_out->dpi[0][0], 800);
	ASSERT_EQ(m_out->dpi_count, 1); /* single preset out of the box */
	ASSERT_EQ(m_out->dpi_active, 0);

	alloy_config_defaults(drv, in);
	struct alloy_mouse_config *m_in = alloy_mouse_cfg(in);
	m_in->dpi_count = 2;
	m_in->dpi[0][0] = 2300;
	m_in->dpi[0][1] = 2300;
	m_in->dpi[1][0] = 1600;
	m_in->dpi[1][1] = 1600;
	m_in->dpi_active = 1;
	m_in->dev.polling_hz = 250;
	m_in->dev.zone_color[2] = (struct alloy_rgb){ 0xAB, 0xCD, 0xEF };
	m_in->dev.zone_fx[1] = 1; /* rainbow on this driver */
	m_in->dev.zone_fx_param[1][ALLOY_FX_P_FREQ] = 8;
	m_in->dev.zone_fx_param[1][ALLOY_FX_P_SPEED] = 2;
	m_in->reactive_enabled = 1;
	m_in->reactive_color = (struct alloy_rgb){ 0x10, 0x20, 0x30 };
	m_in->startup_fx = ALLOY_STARTUP_REACTIVE_RAINBOW;
	m_in->dev.brightness = 42;
	m_in->buttons[5].type = ALLOY_ACT_KEYBOARD;
	m_in->buttons[5].value = 0x29;
	m_in->acceleration = 40;
	m_in->deceleration = 15;
	m_in->angle_snapping = 12;
	m_in->accel_enabled = 1;

	ASSERT_EQ(alloy_state_store(drv, in), 0);
	ASSERT_EQ(alloy_state_load(drv, out), 0);

	ASSERT_EQ(m_out->dpi[0][0], 2300);
	ASSERT_EQ(m_out->dpi_count, 2);
	ASSERT_EQ(m_out->dpi_active, 1);
	ASSERT_EQ(m_out->dev.polling_hz, 250);
	ASSERT_EQ(m_out->dev.zone_color[2].r, 0xAB);
	ASSERT_EQ(m_out->dev.zone_color[2].g, 0xCD);
	ASSERT_EQ(m_out->dev.zone_color[2].b, 0xEF);
	ASSERT_EQ(m_out->dev.zone_fx[0], 0);
	ASSERT_EQ(m_out->dev.zone_fx[1], 1);
	ASSERT_EQ(m_out->dev.zone_fx_param[1][ALLOY_FX_P_FREQ], 8);
	ASSERT_EQ(m_out->dev.zone_fx_param[1][ALLOY_FX_P_SPEED], 2);
	ASSERT_EQ(m_out->reactive_enabled, 1);
	ASSERT_EQ(m_out->reactive_color.g, 0x20);
	ASSERT_EQ(m_out->startup_fx, ALLOY_STARTUP_REACTIVE_RAINBOW);
	ASSERT_EQ(m_out->dev.brightness, 42);
	ASSERT_EQ(m_out->acceleration, 40);
	ASSERT_EQ(m_out->deceleration, 15);
	ASSERT_EQ(m_out->angle_snapping, 12);
	ASSERT_EQ(m_out->accel_enabled, 1);

	/* reactive=off round-trips to disabled */
	m_in->reactive_enabled = 0;
	ASSERT_EQ(alloy_state_store(drv, in), 0);
	ASSERT_EQ(alloy_state_load(drv, out), 0);
	ASSERT_EQ(m_out->reactive_enabled, 0);
	ASSERT_EQ(m_out->buttons[5].type, ALLOY_ACT_KEYBOARD);
	ASSERT_EQ(m_out->buttons[5].value, 0x29);

	alloy_config_free(out);
	alloy_config_free(in);
}

ALLOY_TEST(test_state_legacy_fx_keys)
{
	const struct alloy_driver *drv = r3g2();
	struct alloy_config *out = alloy_config_alloc(drv);
	char tmpl[] = "/tmp/alloyctl-test-XXXXXX";
	char path[128];
	FILE *f;

	if (!mkdtemp(tmpl)) {
		printf("FAIL: mkdtemp\n");
		alloy_test_failures++;
		alloy_config_free(out);
		return;
	}
	setenv("XDG_CONFIG_HOME", tmpl, 1);

	snprintf(path, sizeof(path), "%s/alloyctl", tmpl);
	if (mkdir(path, 0755)) {
		printf("FAIL: mkdir\n");
		alloy_test_failures++;
		alloy_config_free(out);
		return;
	}
	snprintf(path, sizeof(path), "%s/alloyctl/1038-1870.conf", tmpl);
	f = fopen(path, "w");
	if (!f) {
		printf("FAIL: fopen\n");
		alloy_test_failures++;
		alloy_config_free(out);
		return;
	}
	fprintf(f, "zone_fx0=1\n");
	fprintf(f, "zone_fx1=1\n");
	fprintf(f, "zone_fx2=0\n");
	/* active preset pointing past the count clamps on load */
	fprintf(f, "dpi_count=1\n");
	fprintf(f, "dpi_active=3\n");
	fclose(f);

	ASSERT_EQ(alloy_state_load(drv, out), 0);
	struct alloy_mouse_config *m_out = alloy_mouse_cfg(out);
	ASSERT_EQ(m_out->dev.zone_fx[0], 1);
	ASSERT_EQ(m_out->dev.zone_fx[1], 1);
	ASSERT_EQ(m_out->dev.zone_fx[2], 0);
	ASSERT_EQ(m_out->dpi_count, 1);
	ASSERT_EQ(m_out->dpi_active, 0);

	alloy_config_free(out);
}

ALLOY_TEST(test_keyboard_state_roundtrip)
{
	const struct alloy_driver *kbd_drv = alloy_driver_find(0x1038, 0x160E);
	ASSERT_TRUE(kbd_drv != NULL);

	struct alloy_config *in = alloy_config_alloc(kbd_drv);
	struct alloy_config *out = alloy_config_alloc(kbd_drv);
	char tmpl[] = "/tmp/alloyctl-test-XXXXXX";

	if (!mkdtemp(tmpl)) {
		printf("FAIL: mkdtemp\n");
		alloy_test_failures++;
		alloy_config_free(in);
		alloy_config_free(out);
		return;
	}
	setenv("XDG_CONFIG_HOME", tmpl, 1);

	/* defaults when file is absent */
	ASSERT_EQ(alloy_state_load(kbd_drv, out), 1);
	struct alloy_keyboard_config *k_out = alloy_kbd_cfg(out);
	ASSERT_EQ(k_out->dev.brightness, 100);

	/* set keyboard state */
	alloy_config_defaults(kbd_drv, in);
	struct alloy_keyboard_config *k_in = alloy_kbd_cfg(in);
	k_in->dev.brightness = 75;
	k_in->dev.polling_hz = 500;
	k_in->dev.zone_color[0] = (struct alloy_rgb){ 0x11, 0x22, 0x33 };

	ASSERT_EQ(alloy_state_store(kbd_drv, in), 0);
	ASSERT_EQ(alloy_state_load(kbd_drv, out), 0);

	ASSERT_EQ(k_out->dev.brightness, 75);
	ASSERT_EQ(k_out->dev.polling_hz, 500);
	ASSERT_EQ(k_out->dev.zone_color[0].r, 0x11);
	ASSERT_EQ(k_out->dev.zone_color[0].g, 0x22);
	ASSERT_EQ(k_out->dev.zone_color[0].b, 0x33);

	alloy_config_free(in);
	alloy_config_free(out);
}

ALLOY_TEST(test_ops_use_mock)
{
	const struct alloy_driver *drv = r3g2();
	struct alloy_device dev = { 0 };
	struct alloy_config *cfg = alloy_config_alloc(drv);

	mock_hid_reset();
	alloy_device_open_id(&dev, drv->vendor_id, drv->product_id);
	alloy_config_defaults(drv, cfg);

	ASSERT_EQ(alloy_driver_apply(&dev, cfg, ALLOY_STEP_DPI), 0);
	ASSERT_EQ(mock_hid.num_cmds, 1);
	ASSERT_EQ(mock_hid.cmds[0].payload[0], 0x34);

	ASSERT_EQ(drv->ops->save(&dev), 0);
	ASSERT_EQ(mock_hid.cmds[1].payload[0], 0x11);
	ASSERT_EQ(mock_hid.cmds[1].len, 2);

	alloy_device_close(&dev);
	alloy_config_free(cfg);
}
