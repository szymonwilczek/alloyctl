/* SPDX-License-Identifier: GPL-2.0-only */
#include <string.h>

#include "mock_hid.h"

struct mock_hid mock_hid;

void mock_hid_reset(void)
{
	memset(&mock_hid, 0, sizeof(mock_hid));
}

int alloy_hid_present(uint16_t vendor_id, uint16_t product_id, int interface)
{
	(void)interface;
	for (int i = 0; i < mock_hid.num_present; i++) {
		if (mock_hid.present[i].vendor_id == vendor_id &&
		    mock_hid.present[i].product_id == product_id)
			return 1;
	}
	return 0;
}

int alloy_hid_present_bus(uint16_t bustype, uint16_t product_id)
{
	(void)bustype;
	for (int i = 0; i < mock_hid.num_present; i++) {
		if (mock_hid.present[i].product_id == product_id)
			return 1;
	}
	return 0;
}

int alloy_hid_open(struct alloy_hid_dev *dev, uint16_t vendor_id,
		   uint16_t product_id, int interface, size_t report_size)
{
	(void)interface;
	dev->fd = 42;
	dev->vendor_id = vendor_id;
	dev->product_id = product_id;
	dev->interface = interface;
	dev->report_id = 0;
	dev->report_size = report_size ? report_size :
					 ALLOY_HID_DEFAULT_REPORT_SIZE;
	return 0;
}

int alloy_hid_open_bus(struct alloy_hid_dev *dev, uint16_t bustype,
		       uint16_t product_id, uint8_t report_id,
		       size_t report_size)
{
	(void)bustype;
	(void)product_id;
	dev->fd = 42;
	dev->report_id = report_id;
	dev->report_size = report_size ? report_size :
					 ALLOY_HID_DEFAULT_REPORT_SIZE;
	return 0;
}

void alloy_hid_close(struct alloy_hid_dev *dev)
{
	dev->fd = -1;
}

int alloy_hid_reopen(struct alloy_hid_dev *dev)
{
	(void)dev;
	return 0;
}

int alloy_hid_write(struct alloy_hid_dev *dev, const uint8_t *payload,
		    size_t len)
{
	(void)dev;
	if (mock_hid.num_cmds < MOCK_HID_MAX_CMDS) {
		struct mock_cmd *cmd = &mock_hid.cmds[mock_hid.num_cmds];

		memcpy(cmd->payload, payload,
		       ALLOY_MIN(len, sizeof(cmd->payload)));
		cmd->len = len;
	}
	mock_hid.num_cmds++;
	if (mock_hid.fail_cmds)
		return -1;

	/* default mock behavior:
	 * if no custom response queued, echo command byte */
	if (mock_hid.next_response_len == 0 && len > 0) {
		mock_hid.next_response[0] = payload[0];
		mock_hid.next_response_len = 1;
	}
	return 0;
}

int alloy_hid_read(struct alloy_hid_dev *dev, uint8_t *resp, size_t resp_len,
		   int timeout_ms)
{
	(void)dev;
	(void)timeout_ms;
	if (mock_hid.fail_cmds)
		return -2;
	if (mock_hid.next_response_len > 0) {
		size_t n =
			ALLOY_MIN(resp_len, (size_t)mock_hid.next_response_len);
		memcpy(resp, mock_hid.next_response, n);
		mock_hid.next_response_len = 0;
		return (int)n;
	}
	return -2; /* timeout */
}

int alloy_hid_poll(struct alloy_hid_dev *dev, uint8_t *buf, size_t len)
{
	(void)dev;
	(void)buf;
	(void)len;
	return 0; /* no unsolicited events in tests */
}

int alloy_hid_send_feature(struct alloy_hid_dev *dev, const uint8_t *payload,
			   size_t len)
{
	return alloy_hid_write(dev, payload, len);
}

int alloy_hid_get_feature(struct alloy_hid_dev *dev, uint8_t *buf, size_t len)
{
	return alloy_hid_read(dev, buf, len, 0);
}
