// SPDX-License-Identifier: GPL-2.0-only
/*
 * SteelSeries Aerox 3 Wireless over Bluetooth (1038:183A) driver tests.
 *
 * Bluetooth path reuses the receiver driver's packet builders but drops the 0x40
 * "wireless" flag from the opcode.
 * These cases pin the exact bytes GG was captured writing over the BLE vendor Output report
 * (btvs + Wireshark), so the wired opcodes and the shared encodings stay locked down.
 *
 * Protocol reference: Documentation/protocol/steelseries-aerox3-wireless-bt.rst.
 */
#include <stdlib.h>
#include <string.h>

#include "driver.h"
#include "mock_hid.h"
#include "test.h"

static const struct alloy_driver *a3wl_bt(void)
{
	const struct alloy_driver *drv = alloy_driver_find(0x1038, 0x183A);

	if (!drv) {
		printf("FAIL: aerox 3 wireless bluetooth driver not registered\n");
		exit(1);
	}
	return drv;
}

static void bt_dev(struct alloy_device *dev, const struct alloy_driver *drv)
{
	memset(dev, 0, sizeof(*dev));
	dev->hid.fd = 42;
	dev->ev.fd = -1;
	dev->drv = drv;
}

ALLOY_TEST(test_bt_registry)
{
	const struct alloy_driver *drv = a3wl_bt();

	/* binds on the Bluetooth bus by product id, config on Output report 0x04 */
	ASSERT_EQ(drv->bustype, 0x05);
	ASSERT_EQ(drv->report_id, 0x04);
	ASSERT_EQ(drv->product_id, 0x183A);

	/* same sensor range as the receiver path */
	ASSERT_EQ(drv->dpi.min, 100);
	ASSERT_EQ(drv->dpi.max, 18000);

	/* Bluetooth locks everything but CPI + the wireless power knobs */
	ASSERT_EQ(drv->caps, (uint32_t)ALLOY_CAP_BATTERY);
	ASSERT_EQ(drv->num_zones, 0);
	ASSERT_EQ(drv->num_buttons, 0);
	ASSERT_EQ(drv->num_polling_rates, 0);
	ASSERT_EQ(drv->num_fx, 0);

	/* four driven knobs, plus the no-op save the TUI calls unconditionally */
	ASSERT_TRUE(drv->ops->apply_dpi != NULL);
	ASSERT_TRUE(drv->ops->apply_sleep != NULL);
	ASSERT_TRUE(drv->ops->apply_brightness != NULL);
	ASSERT_TRUE(drv->ops->save != NULL);

	/* nothing else is reachable over Bluetooth */
	ASSERT_TRUE(drv->ops->apply_polling == NULL);
	ASSERT_TRUE(drv->ops->apply_colors == NULL);
	ASSERT_TRUE(drv->ops->apply_buttons == NULL);
	ASSERT_TRUE(drv->ops->apply_high_efficiency == NULL);
	ASSERT_TRUE(drv->ops->pair == NULL);
	ASSERT_TRUE(drv->ops->battery == NULL);
	ASSERT_TRUE(drv->ops->firmware_version == NULL);
	ASSERT_TRUE(drv->ops->parse_event == NULL);
}

/* CPI: 0x2d <count> <active> <wire...>,
 * the wired form of the receiver's 0x6d */
ALLOY_TEST(test_bt_dpi_unflagged)
{
	struct alloy_device dev;
	const struct alloy_driver *drv = a3wl_bt();
	struct alloy_config cfg;

	bt_dev(&dev, drv);
	drv->config_defaults(drv, &cfg);
	cfg.dpi_count = 1;
	cfg.dpi_active = 0;
	cfg.dpi[0][0] = 400; /* wire 0x04 in the TrueMove Air table */

	mock_hid_reset();
	ASSERT_EQ(drv->ops->apply_dpi(&dev, &cfg), 0);
	ASSERT_EQ(mock_hid.num_cmds, 1);
	ASSERT_EQ(mock_hid.cmds[0].len, 4);
	ASSERT_EQ(mock_hid.cmds[0].payload[0],
		  0x2D); /* wired opcode, no 0x40 */
	ASSERT_EQ(mock_hid.cmds[0].payload[0] & 0x40, 0);
	ASSERT_EQ(mock_hid.cmds[0].payload[1], 0x01); /* count */
	ASSERT_EQ(mock_hid.cmds[0].payload[2], 0x00); /* active */
	ASSERT_EQ(mock_hid.cmds[0].payload[3], 0x04); /* 400 DPI */
}

/*
 * Sleep timer: 0x29 <ms LE3>
 * 5 min -> 0x0493E0
 * Exact bytes from the s2_sleep_timer capture (29 e0 93 04)
 */
ALLOY_TEST(test_bt_sleep_timer)
{
	struct alloy_device dev;
	const struct alloy_driver *drv = a3wl_bt();
	struct alloy_config cfg;

	bt_dev(&dev, drv);
	drv->config_defaults(drv, &cfg);
	cfg.sleep_min = 5;

	mock_hid_reset();
	ASSERT_EQ(drv->ops->apply_sleep(&dev, &cfg), 0);
	ASSERT_EQ(mock_hid.num_cmds, 1);
	ASSERT_EQ(mock_hid.cmds[0].len, 4);
	ASSERT_EQ(mock_hid.cmds[0].payload[0], 0x29); /* wired 0x69 unflagged */
	ASSERT_EQ(mock_hid.cmds[0].payload[1], 0xE0);
	ASSERT_EQ(mock_hid.cmds[0].payload[2], 0x93);
	ASSERT_EQ(mock_hid.cmds[0].payload[3], 0x04);
}

/*
 * Dim + smart ride one command: 0x23 <level> 01 <smart> 00 <dim LE3>.
 * 30 s dim, smart on -> 23 0f 01 01 00 30 75 (s3/s4 captures).
 */
ALLOY_TEST(test_bt_dim_and_smart)
{
	struct alloy_device dev;
	const struct alloy_driver *drv = a3wl_bt();
	struct alloy_config cfg;

	bt_dev(&dev, drv);
	drv->config_defaults(drv, &cfg);
	cfg.brightness = 100; /* pinned to full, as GG sends over BLE */
	cfg.illum_dim_s = 30;
	cfg.illum_smart = 1;

	mock_hid_reset();
	ASSERT_EQ(drv->ops->apply_brightness(&dev, &cfg), 0);
	ASSERT_EQ(mock_hid.num_cmds, 1);
	ASSERT_EQ(mock_hid.cmds[0].len, 8);
	ASSERT_EQ(mock_hid.cmds[0].payload[0], 0x23); /* wired 0x63 unflagged */
	ASSERT_EQ(mock_hid.cmds[0].payload[1], 0x0F); /* full brightness */
	ASSERT_EQ(mock_hid.cmds[0].payload[2], 0x01);
	ASSERT_EQ(mock_hid.cmds[0].payload[3], 0x01); /* smart on */
	ASSERT_EQ(mock_hid.cmds[0].payload[4], 0x00);
	ASSERT_EQ(mock_hid.cmds[0].payload[5], 0x30); /* 30000 ms LE */
	ASSERT_EQ(mock_hid.cmds[0].payload[6], 0x75);
	ASSERT_EQ(mock_hid.cmds[0].payload[7], 0x00);

	/* smart off flips only byte 3 */
	cfg.illum_smart = 0;
	mock_hid_reset();
	ASSERT_EQ(drv->ops->apply_brightness(&dev, &cfg), 0);
	ASSERT_EQ(mock_hid.cmds[0].payload[3], 0x00);
}
