// SPDX-License-Identifier: GPL-2.0-only
/*
 * SteelSeries Aerox 3 Wireless over Bluetooth, HID product id 1038:183A.
 *
 * Companion to the 2.4 GHz receiver driver (steelseries_aerox3_wireless.c);
 * the same physical mouse, reached over its Bluetooth link instead of the
 * dongle.
 * Over Bluetooth the mouse is a BLE / HID-over-GATT device, so config differs
 * from the receiver path in three ways, all handled by the transport and this
 * thin driver:
 *
 *   - it binds on bus 0x05 by product id alone (Bluetooth re-brands the vendor
 *     id to 0x0111 and carries every report on one hidraw node);
 *   - the vendor channel is the numbered Output report 0x04, not the USB
 *     path's single unnumbered report (see .report_id);
 *   - the opcodes are the plain wired SteelSeries values, without the 0x40
 *     "wireless" flag the receiver firmware needs. The receiver opcode is
 *     wired | 0x40, so clearing bit 6 of the byte the shared builders emit
 *     yields the Bluetooth form (a3wl_bt_wired). Writes are fire-and-forget:
 *     GG uses an ATT Write Command and the mouse never echoes an ACK.
 *
 * GG only exposes four knobs over Bluetooth - CPI, the sleep timer, the LED
 * dim timer and smart illumination - and greys the rest out; the firmware
 * accepts nothing else on this link.
 * This driver mirrors that: it advertises no LED zones, buttons, polling rates
 * or effects, so the TUI offers exactly those four.
 *
 * The packet builders are shared verbatim with the receiver driver (declared below),
 * only the opcode flag and the transport differ.
 *
 * Protocol notes and the reverse-engineering captures live in
 * Documentation/protocol/steelseries-aerox3-wireless-bt.rst.
 * Maintainer: Szymon Wilczek <swilczek.lx@gmail.com>
 */
#include "hid.h"
#include "lib/mouse.h"
#include "art_steelseries_aerox3_wireless.h"

/* Vendor Output report the BLE config channel lives on (report ref 04 02). */
#define A3WL_BT_REPORT_ID 0x04

/* Scratch large enough for every packet this driver builds */
#define A3WL_BT_BUF_SIZE 64

/* Bluetooth HID product id this mouse enumerates as (bus 0x05). */
#define A3WL_BT_PRODUCT_ID 0x183A

/* Sensor range, identical to the receiver path */
#define A3WL_BT_DPI_MIN 100
#define A3WL_BT_DPI_MAX 18000
#define A3WL_BT_DPI_STEP 100

/*
 * Packet builders shared with the 2.4 GHz receiver driver (non-static there).
 * They emit the flagged receiver opcode;
 * a3wl_bt_wired clears the 0x40 flag to get the Bluetooth (wired) form.
 */
size_t a3wl_build_dpi(const struct alloy_config *cfg, uint8_t *buf);
size_t a3wl_build_brightness(const struct alloy_config *cfg, uint8_t *buf);
size_t a3wl_build_sleep(const struct alloy_config *cfg, uint8_t *buf);

/*
 * Rewrite a freshly built receiver packet in place for Bluetooth:
 * drop the 0x40 "wireless" flag from the command byte
 * (receiver opcode = wired | 0x40).
 * Returns the unchanged length so it can wrap a builder call inline.
 */
static size_t a3wl_bt_wired(uint8_t *buf, size_t n)
{
	if (n)
		buf[0] &= (uint8_t)~0x40;
	return n;
}

static int a3wl_bt_apply_dpi(struct alloy_device *dev,
			     const struct alloy_config *cfg)
{
	uint8_t buf[A3WL_BT_BUF_SIZE];

	return alloy_dev_write(dev, buf,
			       a3wl_bt_wired(buf, a3wl_build_dpi(cfg, buf)));
}

/*
 * idle sleep timer, 0x29 <ms LE3>
 */
static int a3wl_bt_apply_sleep(struct alloy_device *dev,
			       const struct alloy_config *cfg)
{
	uint8_t buf[A3WL_BT_BUF_SIZE];

	return alloy_dev_write(dev, buf,
			       a3wl_bt_wired(buf, a3wl_build_sleep(cfg, buf)));
}

/*
 * Unified illumination command, 0x23 <level> 01 <smart> 00 <dim LE3>.
 * Over Bluetooth brightness is not user-editable (no brightness slider is offered),
 * so the level byte rides at its default.
 * This command is driven only for the dim timer and smart-illumination fields it
 * also carries, which are the two illumination knobs Bluetooth does expose.
 */
static int a3wl_bt_apply_illum(struct alloy_device *dev,
			       const struct alloy_config *cfg)
{
	uint8_t buf[A3WL_BT_BUF_SIZE];

	return alloy_dev_write(
		dev, buf, a3wl_bt_wired(buf, a3wl_build_brightness(cfg, buf)));
}

/*
 * No commit step over Bluetooth:
 * GG sends no save opcode on this link and the writes persist on their own.
 * save is called unconditionally by the TUI, so provide no-op rather than leaving it NULL.
 */
static int a3wl_bt_save(struct alloy_device *dev)
{
	(void)dev;
	return 0;
}

static const struct alloy_mouse_info a3wl_bt_mouse = {
	.dpi = {
		.min = A3WL_BT_DPI_MIN,
		.max = A3WL_BT_DPI_MAX,
		.step = A3WL_BT_DPI_STEP,
		.max_presets = 5,
	},
	.bt_product_id = A3WL_BT_PRODUCT_ID, /* light the BT link indicator */
	/*
	 * BLE node exists only while the mouse is actually connected,
	 * so there is no bare-receiver state to wait out before talking to it.
	 */
	.link_implicit = 1,
};

/*
 * No polling rates, LED zones, buttons or effects:
 * Bluetooth locks all of them out, so declaring none of them makes the interface
 * offer exactly the four knobs the firmware accepts on this link.
 */
static const struct alloy_devinfo a3wl_bt_info = {
	.caps = ALLOY_CAP_BATTERY | ALLOY_CAP_DPI,
	.ext = &a3wl_bt_mouse,
};

static const struct alloy_apply_step a3wl_bt_steps[] = {
	{ ALLOY_STEP_DPI, ALLOY_APPLY_SKIP_SYNC, a3wl_bt_apply_dpi },
	{ ALLOY_STEP_SLEEP, 0, a3wl_bt_apply_sleep },
	{ ALLOY_STEP_BRIGHTNESS, 0, a3wl_bt_apply_illum },
};

static const struct alloy_driver_ops a3wl_bt_ops = {
	.config_defaults = alloy_mouse_defaults,
	.state_save = alloy_mouse_state_save,
	.state_load = alloy_mouse_state_load,
	.state_done = alloy_mouse_state_done,
	.save = a3wl_bt_save,
};

static const struct alloy_cli_table a3wl_bt_cli[] = {
	{ alloy_devcfg_cli_options, ALLOY_DEVCFG_CLI_COUNT },
	{ alloy_mouse_cli_options, ALLOY_MOUSE_CLI_COUNT },
};

static const struct alloy_hid_params a3wl_bt_hid = {
	.bustype = 0x05, /* Bluetooth: match and open by product id */
	.report_id = A3WL_BT_REPORT_ID,
};

static const struct alloy_driver steelseries_aerox3_wireless_bt = {
	.name = "SteelSeries Aerox 3 Wireless (Bluetooth)",
	.kind = "mouse",
	.vendor_id = 0x1038,
	.product_id = A3WL_BT_PRODUCT_ID,
	.transport_data = &a3wl_bt_hid,
	.config_size = sizeof(struct alloy_mouse_config),
	.data = &a3wl_bt_info,
	.ascii_art = alloy_art_steelseries_aerox3_wireless,
	.cli_tables = a3wl_bt_cli,
	.num_cli_tables = ALLOY_ARRAY_SIZE(a3wl_bt_cli),
	.apply_steps = a3wl_bt_steps,
	.num_apply_steps = ALLOY_ARRAY_SIZE(a3wl_bt_steps),
	.ui = &alloy_mouse_ui,
	.ops = &a3wl_bt_ops,
};

ALLOY_DRIVER_REGISTER(steelseries_aerox3_wireless_bt);
