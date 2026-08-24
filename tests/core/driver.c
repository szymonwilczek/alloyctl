/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Unit tests for core driver registry and universal driver architecture.
 */
#include <string.h>

#include "driver.h"
#include "lib/keyboard.h"
#include "lib/mouse.h"
#include "test.h"

ALLOY_TEST(test_registered_drivers_have_valid_metadata)
{
	const struct alloy_driver *const *iter;
	int count = 0;

	alloy_for_each_driver(iter)
	{
		const struct alloy_driver *drv = *iter;
		ASSERT_TRUE(drv != NULL);
		ASSERT_TRUE(drv->name != NULL);
		ASSERT_TRUE(drv->kind != NULL);
		ASSERT_TRUE(drv->config_size > 0);
		count++;
	}

	ASSERT_TRUE(count > 0);
}

ALLOY_TEST(test_driver_config_lifecycle)
{
	const struct alloy_driver *drv = alloy_driver_find(0x1038, 0x1824);
	ASSERT_TRUE(drv != NULL);

	struct alloy_config *a = alloy_config_alloc(drv);
	struct alloy_config *b = alloy_config_alloc(drv);
	ASSERT_TRUE(a != NULL);
	ASSERT_TRUE(b != NULL);

	alloy_config_defaults(drv, a);
	alloy_config_defaults(drv, b);
	ASSERT_TRUE(alloy_config_equal(a, b));

	struct alloy_mouse_config *m = alloy_mouse_cfg(a);
	m->dpi[0][0] = 3200;
	ASSERT_TRUE(!alloy_config_equal(a, b));

	alloy_config_copy(b, a);
	ASSERT_TRUE(alloy_config_equal(a, b));

	alloy_config_free(a);
	alloy_config_free(b);
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
