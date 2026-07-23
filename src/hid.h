/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * hidraw transport layer.
 *
 * SteelSeries mice are configured through vendor HID output reports on
 * dedicated USB interface.
 *
 * Kernel exposes each HID interface as /dev/hidrawN node.
 * This module locates the node matching given VID/PID/interface triple
 * and provides framed command I/O on it.
 */
#ifndef ALLOY_HID_H
#define ALLOY_HID_H

#include "alloy.h"

/*
 * Largest vendor report payload across supported devices;
 * the actual per-device size (64 B on recent firmwares, 32 B on older ones)
 * is chosen at open time.
 */
#define ALLOY_HID_REPORT_SIZE 64

/*
 * Wake-retry budgets.
 * Wireless receivers let the 2.4 GHz link idle after second or two of no mouse motion;
 * while idle the config interface answers every vendor query with the 0x40 0xFF idle
 * marker instead of the command echo.
 * Command is not rejected in that state - the mouse is merely asleep - so the transport
 * re-sends it a few times, which keeps the link hot long enough for the firmware to answer.
 * Config writes wake hard; periodic background poll stays light so it never freezes
 * the render loop for long.
 */
#define ALLOY_HID_ATTEMPTS_CMD 8
#define ALLOY_HID_ATTEMPTS_POLL 2

struct alloy_hid_dev {
	int fd;
	size_t report_size;
	uint16_t vendor_id;
	uint16_t product_id;
	int interface;
	/*
	 * First byte written on every report: the HID report number.
	 * 0 (unnumbered) for the USB/2.4 GHz path where mice use a single report.
	 * Bluetooth speaks HID-over-GATT, where the vendor channel is a numbered
	 * Output report, so alloy_hid_open_bus sets the real report id here.
	 */
	uint8_t report_id;
};

/*
 * Report whether a device with the given VID/PID/interface is currently
 * connected, without opening it.
 * Returns 1 if present, 0 otherwise.
 */
int alloy_hid_present(uint16_t vendor_id, uint16_t product_id, int interface);

/*
 * Report whether any hidraw node on the given bus type (e.g. 0x05 Bluetooth)
 * exposes the given product id, regardless of vendor or interface.
 * Bluetooth re-brands the vendor id, so only bus and product are matched.
 * Returns 1 if present, 0 otherwise.
 */
int alloy_hid_present_bus(uint16_t bustype, uint16_t product_id);

int alloy_hid_open(struct alloy_hid_dev *dev, uint16_t vendor_id,
		   uint16_t product_id, int interface, size_t report_size);

/*
 * Open the single hidraw node a device exposes on the given bus type
 * (e.g. 0x05 Bluetooth), matched by product id only - Bluetooth re-brands
 * the vendor id and multiplexes every report through one node, so there is
 * no interface to match.
 * @report_id is written as the report number on each send
 * (the numbered Output report the vendor channel lives on over HID-over-GATT).
 * Returns 0 on success, -1 when no matching node is present or cannot be opened.
 */
int alloy_hid_open_bus(struct alloy_hid_dev *dev, uint16_t bustype,
		       uint16_t product_id, uint8_t report_id,
		       size_t report_size);
void alloy_hid_close(struct alloy_hid_dev *dev);

/*
 * Fire-and-forget write for firmwares that do not acknowledge
 * commands (e.g. Rival 3 Gen 1).
 * Returns 0 on a complete write.
 */
int alloy_hid_send(struct alloy_hid_dev *dev, const uint8_t *payload,
		   size_t len);

/*
 * Send command payload (padded to ALLOY_HID_REPORT_SIZE) and wait for the device
 * to acknowledge it by echoing the command byte on the interrupt IN endpoint.
 * On wireless receiver whose mouse is asleep the send is re-tried to wake the
 * link (see ALLOY_HID_ATTEMPTS_CMD).
 * Returns 0 on ACK, -1 on I/O error and -2 when the device stayed silent
 * (unknown command, or a mouse that never woke).
 */
int alloy_hid_cmd(struct alloy_hid_dev *dev, const uint8_t *payload,
		  size_t len);

/*
 * Send command and read back the first matching response report into resp,
 * skipping the receiver idle marker and any unrelated report that arrives
 * first.
 * @want is the expected first byte (the command echo); pass negative value
 * to accept any non-idle report (e.g. the firmware-version reply, which does not echo).
 * @attempts caps the wake-retry count.
 * Returns the byte count of the matching report, -1 on I/O error, -2 when the
 * device stayed idle/silent for every attempt.
 */
int alloy_hid_cmd_read_want(struct alloy_hid_dev *dev, const uint8_t *payload,
			    size_t len, int want, uint8_t *resp,
			    size_t resp_len, int attempts);

/*
 * Convenience wrapper over alloy_hid_cmd_read_want that accepts any non-idle
 * report (want < 0) with the full config-write retry budget.
 * Returns the number of bytes read, or negative error as above.
 */
int alloy_hid_cmd_read(struct alloy_hid_dev *dev, const uint8_t *payload,
		       size_t len, uint8_t *resp, size_t resp_len);

/*
 * Non-blocking read of one pending unsolicited input report.
 * Device-initiated events, e.g. hardware CPI level switches on dedicated event interface.
 * Returns the byte count, 0 when nothing is pending, -1 on I/O error.
 */
int alloy_hid_poll(struct alloy_hid_dev *dev, uint8_t *buf, size_t len);

#endif /* ALLOY_HID_H */
