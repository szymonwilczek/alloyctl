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

struct alloy_hid_dev {
	int fd;
	size_t report_size;
};

/*
 * Report whether a device with the given VID/PID/interface is currently
 * connected, without opening it.
 * Returns 1 if present, 0 otherwise.
 */
int alloy_hid_present(uint16_t vendor_id, uint16_t product_id, int interface);

int alloy_hid_open(struct alloy_hid_dev *dev, uint16_t vendor_id,
		   uint16_t product_id, int interface, size_t report_size);
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
 * Returns 0 on ACK, -1 on I/O error and -2 when the device stayed silent (unknown command).
 */
int alloy_hid_cmd(struct alloy_hid_dev *dev, const uint8_t *payload,
		  size_t len);

/*
 * Send command and read back the raw 64-byte response into resp.
 * Returns the number of bytes read, or negative error as above.
 */
int alloy_hid_cmd_read(struct alloy_hid_dev *dev, const uint8_t *payload,
		       size_t len, uint8_t *resp, size_t resp_len);

#endif /* ALLOY_HID_H */
