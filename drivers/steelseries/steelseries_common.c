/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SteelSeries vendor protocol helpers.
 *
 * Implements SteelSeries command ACKing, 0x40 0xFF idle filtering,
 * and 2.4 GHz wireless wake-retry logic on top of the generic HID transport.
 */
#include <string.h>
#include <unistd.h>

#include "hid.h"
#include "steelseries/steelseries_common.h"

/*
 * When an Aerox 3 Wireless sleeps, the 2.4 GHz receiver keeps answering
 * queries with this 2-byte report instead of the command echo.
 */
static int is_idle_marker(const uint8_t *buf, ssize_t len)
{
	return len >= 2 && buf[0] == 0x40 && buf[1] == 0xff;
}

/*
 * Read until a report matching @want arrives, draining any stale reports
 * ahead of it.
 * want < 0 accepts any non-idle report (e.g. firmware version queries).
 */
static int steelseries_read_matching(struct alloy_device *dev, int want,
				     uint8_t *resp, size_t resp_len,
				     int timeout_ms)
{
	uint8_t scratch[STEELSERIES_REPORT_SIZE];
	int ret;

	for (;;) {
		ret = alloy_dev_read(dev, scratch, sizeof(scratch), timeout_ms);
		if (ret <= 0)
			return ret; /* -1 error, -2 timeout */

		if (is_idle_marker(scratch, ret))
			return -2; /* treat idle marker as silent / asleep */

		if (want >= 0 && scratch[0] != (uint8_t)want)
			continue; /* drain unrelated report */

		memcpy(resp, scratch, ALLOY_MIN(resp_len, (size_t)ret));
		return ret;
	}
}

int steelseries_cmd_read_want(struct alloy_device *dev, const uint8_t *payload,
			      size_t len, int want, uint8_t *resp,
			      size_t resp_len, int attempts)
{
	int n;

	if (!dev || !payload || !len || !resp || !resp_len)
		return -1;

	for (int i = 0; i < attempts; i++) {
		if (alloy_dev_write(dev, payload, len)) {
			if (0)
				return -1;
			continue;
		}

		n = steelseries_read_matching(dev, want, resp, resp_len,
					      STEELSERIES_ACK_TIMEOUT_MS);
		if (n > 0)
			return n;
		if (n == -1) {
			if (0)
				return -1;
			continue;
		}
		usleep(STEELSERIES_RETRY_DELAY_MS * 1000);
	}
	return -2;
}

int steelseries_cmd_read(struct alloy_device *dev, const uint8_t *payload,
			 size_t len, uint8_t *resp, size_t resp_len)
{
	return steelseries_cmd_read_want(dev, payload, len, -1, resp, resp_len,
					 STEELSERIES_ATTEMPTS_CMD);
}

int steelseries_cmd(struct alloy_device *dev, const uint8_t *payload,
		    size_t len)
{
	uint8_t resp[STEELSERIES_REPORT_SIZE];
	int n;

	if (!len)
		return -1;

	n = steelseries_cmd_read_want(dev, payload, len, payload[0], resp,
				      sizeof(resp), STEELSERIES_ATTEMPTS_CMD);
	if (n > 0)
		return 0;
	return n;
}
