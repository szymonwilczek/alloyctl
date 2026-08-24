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
#include "hid.h"
#include "lib/mouse.h"
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

ALLOY_TEST(test_bt_registry)
{
	const struct alloy_driver *drv = a3wl_bt();
	const struct alloy_hid_params *hid = drv->transport_data;
	const struct alloy_devinfo *info = alloy_devinfo(drv);
	const struct alloy_mouse_info *mouse = alloy_mouse_info(drv);

	/* binds on the Bluetooth bus by product id, config on Output report 0x04 */
	ASSERT_EQ(hid->bustype, 0x05);
	ASSERT_EQ(hid->report_id, 0x04);
	ASSERT_EQ(drv->product_id, 0x183A);

	/* same sensor range as the receiver path */
	ASSERT_EQ(mouse->dpi.min, 100);
	ASSERT_EQ(mouse->dpi.max, 18000);

	/* Bluetooth locks everything but CPI + the wireless power knobs */
	ASSERT_EQ(info->caps, (uint64_t)(ALLOY_CAP_BATTERY | ALLOY_CAP_DPI));
	ASSERT_EQ(info->num_zones, 0);
	ASSERT_EQ(info->num_polling_rates, 0);
	ASSERT_EQ(info->num_fx, 0);

	/* driver apply steps */
	ASSERT_TRUE(alloy_driver_step(drv, ALLOY_STEP_DPI) != NULL);
	ASSERT_TRUE(alloy_driver_step(drv, ALLOY_STEP_SLEEP) != NULL);
	ASSERT_TRUE(alloy_driver_step(drv, ALLOY_STEP_BRIGHTNESS) != NULL);
	ASSERT_TRUE(drv->ops->save != NULL);

	/* nothing else is reachable over Bluetooth */
	ASSERT_TRUE(alloy_driver_step(drv, ALLOY_STEP_POLLING) == NULL);
	ASSERT_TRUE(alloy_driver_step(drv, ALLOY_STEP_COLORS) == NULL);
	ASSERT_TRUE(alloy_driver_step(drv, ALLOY_STEP_BUTTONS) == NULL);
}

/* CPI: 0x2d <count> <active> <wire...>,
 * the wired form of the receiver's 0x6d */
ALLOY_TEST(test_bt_dpi_unflagged)
{
	const struct alloy_driver *drv = a3wl_bt();
	struct alloy_device dev = { 0 };
	struct alloy_config *cfg = alloy_config_alloc(drv);

	alloy_device_open_id(&dev, drv->vendor_id, drv->product_id);
	alloy_config_defaults(drv, cfg);
	struct alloy_mouse_config *m = alloy_mouse_cfg(cfg);
	m->dpi_count = 1;
	m->dpi_active = 0;
	m->dpi[0][0] = 400; /* wire 0x04 in the TrueMove Air table */

	mock_hid_reset();
	ASSERT_EQ(alloy_driver_apply(&dev, cfg, ALLOY_STEP_DPI), 0);
	ASSERT_EQ(mock_hid.num_cmds, 1);
	ASSERT_EQ(mock_hid.cmds[0].len, 4);
	ASSERT_EQ(mock_hid.cmds[0].payload[0],
		  0x2D); /* wired opcode, no 0x40 */
	ASSERT_EQ(mock_hid.cmds[0].payload[0] & 0x40, 0);
	ASSERT_EQ(mock_hid.cmds[0].payload[1], 0x01); /* count */
	ASSERT_EQ(mock_hid.cmds[0].payload[2], 0x00); /* active */
	ASSERT_EQ(mock_hid.cmds[0].payload[3], 0x04); /* 400 DPI */

	alloy_device_close(&dev);
	alloy_config_free(cfg);
}

/*
 * Sleep timer: 0x29 <ms LE3>
 * 5 min -> 0x0493E0
 * Exact bytes from the s2_sleep_timer capture (29 e0 93 04)
 */
ALLOY_TEST(test_bt_sleep_timer)
{
	const struct alloy_driver *drv = a3wl_bt();
	struct alloy_device dev = { 0 };
	struct alloy_config *cfg = alloy_config_alloc(drv);

	alloy_device_open_id(&dev, drv->vendor_id, drv->product_id);
	alloy_config_defaults(drv, cfg);
	struct alloy_mouse_config *m = alloy_mouse_cfg(cfg);
	m->sleep_min = 5;

	mock_hid_reset();
	ASSERT_EQ(alloy_driver_apply(&dev, cfg, ALLOY_STEP_SLEEP), 0);
	ASSERT_EQ(mock_hid.num_cmds, 1);
	ASSERT_EQ(mock_hid.cmds[0].len, 4);
	ASSERT_EQ(mock_hid.cmds[0].payload[0], 0x29); /* wired 0x69 unflagged */
	ASSERT_EQ(mock_hid.cmds[0].payload[1], 0xE0);
	ASSERT_EQ(mock_hid.cmds[0].payload[2], 0x93);
	ASSERT_EQ(mock_hid.cmds[0].payload[3], 0x04);

	alloy_device_close(&dev);
	alloy_config_free(cfg);
}

/*
 * Dim + smart ride one command: 0x23 <level> 01 <smart> 00 <dim LE3>.
 * 30 s dim, smart on -> 23 0f 01 01 00 30 75 (s3/s4 captures).
 */
ALLOY_TEST(test_bt_dim_and_smart)
{
	const struct alloy_driver *drv = a3wl_bt();
	struct alloy_device dev = { 0 };
	struct alloy_config *cfg = alloy_config_alloc(drv);

	alloy_device_open_id(&dev, drv->vendor_id, drv->product_id);
	alloy_config_defaults(drv, cfg);
	struct alloy_mouse_config *m = alloy_mouse_cfg(cfg);
	m->dev.brightness = 100; /* pinned to full, as GG sends over BLE */
	m->illum_dim_s = 30;
	m->illum_smart = 1;

	mock_hid_reset();
	ASSERT_EQ(alloy_driver_apply(&dev, cfg, ALLOY_STEP_BRIGHTNESS), 0);
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
	m->illum_smart = 0;
	mock_hid_reset();
	ASSERT_EQ(alloy_driver_apply(&dev, cfg, ALLOY_STEP_BRIGHTNESS), 0);
	ASSERT_EQ(mock_hid.cmds[0].payload[3], 0x00);

	alloy_device_close(&dev);
	alloy_config_free(cfg);
}
