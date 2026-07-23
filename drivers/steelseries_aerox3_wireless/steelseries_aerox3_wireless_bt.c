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
#include "driver.h"
#include "art_steelseries_aerox3_wireless.h"

/* Vendor Output report the BLE config channel lives on (report ref 04 02). */
#define A3WL_BT_REPORT_ID 0x04

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
	uint8_t buf[ALLOY_HID_REPORT_SIZE];

	return alloy_hid_send(&dev->hid, buf,
			      a3wl_bt_wired(buf, a3wl_build_dpi(cfg, buf)));
}

/*
 * idle sleep timer, 0x29 <ms LE3>
 */
static int a3wl_bt_apply_sleep(struct alloy_device *dev,
			       const struct alloy_config *cfg)
{
	uint8_t buf[ALLOY_HID_REPORT_SIZE];

	return alloy_hid_send(&dev->hid, buf,
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
	uint8_t buf[ALLOY_HID_REPORT_SIZE];

	return alloy_hid_send(&dev->hid, buf,
			      a3wl_bt_wired(buf,
					    a3wl_build_brightness(cfg, buf)));
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

static const struct alloy_driver_ops a3wl_bt_ops = {
	.apply_dpi = a3wl_bt_apply_dpi,
	.apply_sleep = a3wl_bt_apply_sleep,
	.apply_brightness = a3wl_bt_apply_illum,
	.save = a3wl_bt_save,
};

static const struct alloy_driver steelseries_aerox3_wireless_bt = {
	.name = "SteelSeries Aerox 3 Wireless (Bluetooth)",
	.vendor_id = 0x1038,
	.product_id = A3WL_BT_PRODUCT_ID,
	.bustype = 0x05, /* Bluetooth: match/open by product id on bus 0x05 */
	.report_id = A3WL_BT_REPORT_ID,
	.bt_product_id = A3WL_BT_PRODUCT_ID, /* light the BT link indicator */
	.dpi = {
		.min = A3WL_BT_DPI_MIN,
		.max = A3WL_BT_DPI_MAX,
		.step = A3WL_BT_DPI_STEP,
		.max_presets = 5,
	},
	/*
	 * No polling rates, LED zones, buttons or effects:
	 * Bluetooth locks all of them out, so leaving these empty makes the TUI
	 * offer only CPI and the wireless power knobs (sleep / dim / smart).
	 */
	.caps = ALLOY_CAP_BATTERY,
	.ascii_art = alloy_art_steelseries_aerox3_wireless,
	.ops = &a3wl_bt_ops,
	.config_defaults = alloy_config_generic_defaults,
};

ALLOY_DRIVER_REGISTER(steelseries_aerox3_wireless_bt);
