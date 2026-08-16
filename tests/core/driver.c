/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Unit tests for core driver registry and device type taxonomy.
 */
#include <string.h>

#include "driver.h"
#include "test.h"

ALLOY_TEST(test_device_type_names)
{
	ASSERT_TRUE(!strcmp(alloy_device_type_name(ALLOY_DEV_MOUSE), "mouse"));
	ASSERT_TRUE(!strcmp(alloy_device_type_name(ALLOY_DEV_KEYBOARD),
			    "keyboard"));
	ASSERT_TRUE(!strcmp(alloy_device_type_name((enum alloy_device_type)99),
			    "mouse"));
}

ALLOY_TEST(test_device_type_helpers)
{
	struct alloy_driver mouse_drv = {
		.name = "Mock Mouse",
		.type = ALLOY_DEV_MOUSE,
	};
	struct alloy_driver kbd_drv = {
		.name = "Mock Keyboard",
		.type = ALLOY_DEV_KEYBOARD,
	};

	/* NULL safely defaults to mouse */
	ASSERT_TRUE(alloy_driver_is_mouse(NULL));
	ASSERT_TRUE(!alloy_driver_is_keyboard(NULL));

	/* Mouse driver check */
	ASSERT_TRUE(alloy_driver_is_mouse(&mouse_drv));
	ASSERT_TRUE(!alloy_driver_is_keyboard(&mouse_drv));

	/* Keyboard driver check */
	ASSERT_TRUE(!alloy_driver_is_mouse(&kbd_drv));
	ASSERT_TRUE(alloy_driver_is_keyboard(&kbd_drv));
}

ALLOY_TEST(test_modular_config_defaults)
{
	struct alloy_driver mouse_drv = {
		.name = "Mock Mouse",
		.type = ALLOY_DEV_MOUSE,
		.caps = ALLOY_CAP_FX_RAINBOW,
		.num_zones = 2,
		.zones =
			(const struct alloy_led_zone[]){
				{ .name = "Z1",
				  .def_color = { 0x11, 0x22, 0x33 } },
				{ .name = "Z2",
				  .def_color = { 0x44, 0x55, 0x66 } },
			},
	};
	struct alloy_driver kbd_drv = {
		.name = "Mock Keyboard",
		.type = ALLOY_DEV_KEYBOARD,
	};
	struct alloy_config cfg;

	alloy_config_generic_defaults(&mouse_drv, &cfg);
	ASSERT_EQ(cfg.common.brightness, 100);
	ASSERT_EQ(cfg.common.zone_color[0].r, 0x11);
	ASSERT_EQ(cfg.common.zone_color[1].g, 0x55);
	ASSERT_EQ(cfg.mouse.dpi_count, 1);
	ASSERT_EQ(cfg.mouse.dpi[0][0], 800);
	ASSERT_EQ(cfg.mouse.startup_fx, ALLOY_STARTUP_RAINBOW);

	alloy_config_generic_defaults(&kbd_drv, &cfg);
	ASSERT_EQ(cfg.common.brightness, 100);
	ASSERT_EQ(cfg.kbd.win_lock, 0);
}

ALLOY_TEST(test_registered_drivers_have_valid_type)
{
	const struct alloy_driver *const *iter;
	int count = 0;

	alloy_for_each_driver(iter)
	{
		const struct alloy_driver *drv = *iter;
		ASSERT_TRUE(drv != NULL);
		ASSERT_TRUE(drv->name != NULL);

		if (drv->type == ALLOY_DEV_KEYBOARD) {
			ASSERT_TRUE(alloy_driver_is_keyboard(drv));
			ASSERT_TRUE(!alloy_driver_is_mouse(drv));
			ASSERT_TRUE(!strcmp(alloy_device_type_name(drv->type),
					    "keyboard"));
		} else {
			ASSERT_EQ(drv->type, ALLOY_DEV_MOUSE);
			ASSERT_TRUE(alloy_driver_is_mouse(drv));
			ASSERT_TRUE(!alloy_driver_is_keyboard(drv));
			ASSERT_TRUE(!strcmp(alloy_device_type_name(drv->type),
					    "mouse"));
		}
		count++;
	}

	ASSERT_TRUE(count > 0);
}

ALLOY_TEST(test_keyboard_hid_key_helpers)
{
	ASSERT_TRUE(!strcmp(alloy_hid_key_name(0x04), "A"));
	ASSERT_TRUE(!strcmp(alloy_hid_key_name(0x16), "S"));
	ASSERT_TRUE(!strcmp(alloy_hid_key_name(0x07), "D"));
	ASSERT_TRUE(!strcmp(alloy_hid_key_name(0x1A), "W"));

	ASSERT_EQ(alloy_hid_key_from_name("A"), 0x04);
	ASSERT_EQ(alloy_hid_key_from_name("a"), 0x04);
	ASSERT_EQ(alloy_hid_key_from_name("D"), 0x07);
	ASSERT_EQ(alloy_hid_key_from_name("0x04"), 0x04);
	ASSERT_EQ(alloy_hid_key_from_name(""), 0);
	ASSERT_EQ(alloy_hid_key_from_name(NULL), 0);
}
