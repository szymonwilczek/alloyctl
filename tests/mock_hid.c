// SPDX-License-Identifier: GPL-2.0-only
/*
 * Mock hidraw transport for the unit tests.
 *
 * Captures every payload that driver would have sent to the hardware.
 */
#include <string.h>

#include "hid.h"
#include "mock_hid.h"

struct mock_hid mock_hid;

void mock_hid_reset(void)
{
	memset(&mock_hid, 0, sizeof(mock_hid));
}

int alloy_hid_open(struct alloy_hid_dev *dev, uint16_t vendor_id,
		   uint16_t product_id, int interface, size_t report_size)
{
	(void)vendor_id;
	(void)product_id;
	(void)interface;
	dev->fd = 42;
	dev->report_size = report_size ? report_size : ALLOY_HID_REPORT_SIZE;
	return 0;
}

int alloy_hid_send(struct alloy_hid_dev *dev, const uint8_t *payload,
		   size_t len)
{
	return alloy_hid_cmd(dev, payload, len);
}

void alloy_hid_close(struct alloy_hid_dev *dev)
{
	dev->fd = -1;
}

int alloy_hid_cmd(struct alloy_hid_dev *dev, const uint8_t *payload, size_t len)
{
	(void)dev;
	if (mock_hid.num_cmds < MOCK_HID_MAX_CMDS) {
		struct mock_cmd *cmd = &mock_hid.cmds[mock_hid.num_cmds];

		memcpy(cmd->payload, payload,
		       ALLOY_MIN(len, sizeof(cmd->payload)));
		cmd->len = len;
	}
	mock_hid.num_cmds++;
	return mock_hid.fail_cmds ? -2 : 0;
}

int alloy_hid_cmd_read(struct alloy_hid_dev *dev, const uint8_t *payload,
		       size_t len, uint8_t *resp, size_t resp_len)
{
	alloy_hid_cmd(dev, payload, len);
	memcpy(resp, mock_hid.next_response,
	       ALLOY_MIN(resp_len, sizeof(mock_hid.next_response)));
	return mock_hid.next_response_len;
}
