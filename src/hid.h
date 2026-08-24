/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Generic Linux hidraw transport layer.
 *
 * Provides device discovery and raw HID I/O (Output reports, Input reports,
 * and Feature reports) on /dev/hidraw nodes without vendor-specific logic.
 */
#ifndef ALLOY_HID_H
#define ALLOY_HID_H

#include "alloy.h"

#define ALLOY_HID_REPORT_SIZE 64
#define ALLOY_HID_DEFAULT_REPORT_SIZE 64

struct alloy_hid_dev {
	int fd;
	size_t report_size;
	uint16_t vendor_id;
	uint16_t product_id;
	int interface;
	uint8_t report_id;
};

/* Device discovery */
int alloy_hid_present(uint16_t vendor_id, uint16_t product_id, int interface);
int alloy_hid_present_bus(uint16_t bustype, uint16_t product_id);
int alloy_hid_open(struct alloy_hid_dev *dev, uint16_t vendor_id,
		   uint16_t product_id, int interface, size_t report_size);
int alloy_hid_open_bus(struct alloy_hid_dev *dev, uint16_t bustype,
		       uint16_t product_id, uint8_t report_id,
		       size_t report_size);
void alloy_hid_close(struct alloy_hid_dev *dev);
int alloy_hid_reopen(struct alloy_hid_dev *dev);

/*
 * Parameter block for the default transport (src/hid_transport.c).
 * A driver points struct alloy_driver.transport_data at one of these; the core
 * passes it through without looking at it.
 */
struct alloy_hid_params {
	/*
	 * HID bus to bind on:
	 * 0 (default) is USB/2.4 GHz - matched and opened by vendor/product on
	 * the given interface.
	 * 0x05 is Bluetooth - the device speaks HID-over-GATT, so it is matched
	 * and opened by product id alone on the single hidraw node the BLE stack
	 * exposes, and traffic rides the numbered Output report below.
	 */
	uint16_t bustype;
	int interface; /* USB interface carrying config reports */
	int event_interface; /* USB interface streaming unsolicited reports */
	uint8_t report_id; /* number prefixed to every write, 0 for none */
	uint16_t report_size; /* 0 selects the 64-byte default */
};

/* Standard Output / Input Report I/O */
int alloy_hid_write(struct alloy_hid_dev *dev, const uint8_t *payload,
		    size_t len);
int alloy_hid_read(struct alloy_hid_dev *dev, uint8_t *resp, size_t resp_len,
		   int timeout_ms);
int alloy_hid_poll(struct alloy_hid_dev *dev, uint8_t *buf, size_t len);

/* Feature Report I/O (ioctl HIDIOCSFEATURE / HIDIOCGFEATURE) */
int alloy_hid_send_feature(struct alloy_hid_dev *dev, const uint8_t *payload,
			   size_t len);
int alloy_hid_get_feature(struct alloy_hid_dev *dev, uint8_t *buf, size_t len);

/* Fire-and-forget write wrapper */
static inline int alloy_hid_send(struct alloy_hid_dev *dev,
				 const uint8_t *payload, size_t len)
{
	return alloy_hid_write(dev, payload, len);
}

#endif /* ALLOY_HID_H */
