/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef ALLOY_TESTS_MOCK_HID_H
#define ALLOY_TESTS_MOCK_HID_H

#include "hid.h"

#define MOCK_HID_MAX_CMDS 16

struct mock_cmd {
	uint8_t payload[ALLOY_HID_REPORT_SIZE];
	size_t len;
};

struct mock_hid {
	struct mock_cmd cmds[MOCK_HID_MAX_CMDS];
	int num_cmds;
	int fail_cmds; /* make alloy_hid_cmd() report missing ACK */
	uint8_t next_response[ALLOY_HID_REPORT_SIZE];
	int next_response_len;
};

extern struct mock_hid mock_hid;

void mock_hid_reset(void);

#endif /* ALLOY_TESTS_MOCK_HID_H */
