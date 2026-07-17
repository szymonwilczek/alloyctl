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

/* Payload size of single vendor report on the config interface */
#define ALLOY_HID_REPORT_SIZE 64

struct alloy_hid_dev {
	int fd;
};

int alloy_hid_open(struct alloy_hid_dev *dev, uint16_t vendor_id,
		   uint16_t product_id, int interface);
void alloy_hid_close(struct alloy_hid_dev *dev);

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
