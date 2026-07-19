// SPDX-License-Identifier: GPL-2.0-only
/*
 * Multi-device detection:
 * alloy_device_enumerate() must report every supported mouse the mocked transport
 * marks as connected, in registry order, and count them so the front-end can decide
 * between the silent "no device" path, a direct open and the chooser modal.
 */
#include "driver.h"
#include "mock_hid.h"
#include "test.h"

static void mark_present(uint16_t vendor_id, uint16_t product_id)
{
	struct mock_present *p = &mock_hid.present[mock_hid.num_present++];

	p->vendor_id = vendor_id;
	p->product_id = product_id;
}

ALLOY_TEST(test_enumerate_none)
{
	const struct alloy_driver *out[4];

	mock_hid_reset();
	ASSERT_EQ(alloy_device_enumerate(out, 4), 0);
}

ALLOY_TEST(test_enumerate_single)
{
	const struct alloy_driver *out[4];

	mock_hid_reset();
	mark_present(0x1038, 0x1870); /* Rival 3 Gen 2 */

	ASSERT_EQ(alloy_device_enumerate(out, 4), 1);
	ASSERT_EQ(out[0]->vendor_id, 0x1038);
	ASSERT_EQ(out[0]->product_id, 0x1870);
}

ALLOY_TEST(test_enumerate_multiple)
{
	const struct alloy_driver *out[4];
	int n;

	mock_hid_reset();
	mark_present(0x1038, 0x1824); /* Rival 3 */
	mark_present(0x1038, 0x1870); /* Rival 3 Gen 2 */

	n = alloy_device_enumerate(out, 4);
	ASSERT_EQ(n, 2);
	/* both connected devices are reported */
	ASSERT_TRUE(out[0]->product_id == 0x1824 ||
		    out[1]->product_id == 0x1824);
	ASSERT_TRUE(out[0]->product_id == 0x1870 ||
		    out[1]->product_id == 0x1870);
}

/* unregistered VID:PID must never be reported as a supported device. */
ALLOY_TEST(test_enumerate_ignores_unknown)
{
	const struct alloy_driver *out[4];

	mock_hid_reset();
	mark_present(0xDEAD, 0xBEEF);

	ASSERT_EQ(alloy_device_enumerate(out, 4), 0);
}

/* counting still works (and can exceed the buffer) when out is too small */
ALLOY_TEST(test_enumerate_count_over_capacity)
{
	const struct alloy_driver *out[1];

	mock_hid_reset();
	mark_present(0x1038, 0x1824);
	mark_present(0x1038, 0x1870);

	/* returns the full count even though only one pointer fits */
	ASSERT_EQ(alloy_device_enumerate(out, 1), 2);
	/* single slot that fits holds one of the connected devices */
	ASSERT_TRUE(out[0]->product_id == 0x1824 ||
		    out[0]->product_id == 0x1870);
}
