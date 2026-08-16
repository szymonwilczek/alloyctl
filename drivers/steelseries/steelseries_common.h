/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SteelSeries vendor protocol helpers.
 *
 * Encapsulates SteelSeries-specific packet framing, ACK validation,
 * and 2.4 GHz wireless wake-retry logic.
 */
#ifndef STEELSERIES_COMMON_H
#define STEELSERIES_COMMON_H

#include "lib/mouse.h"

#define STEELSERIES_VENDOR_ID 0x1038
#define STEELSERIES_REPORT_SIZE 64
#define STEELSERIES_ACK_TIMEOUT_MS 400
#define STEELSERIES_RETRY_DELAY_MS 40
#define STEELSERIES_ATTEMPTS_CMD 8
#define STEELSERIES_ATTEMPTS_POLL 2

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
int steelseries_cmd_read_want(struct alloy_device *dev, const uint8_t *payload,
			      size_t len, int want, uint8_t *resp,
			      size_t resp_len, int attempts);

/*
 * Convenience wrapper over steelseries_cmd_read_want that accepts any non-idle
 * report (want < 0) with the full config-write retry budget.
 * Returns the number of bytes read, or negative error as above.
 */
int steelseries_cmd_read(struct alloy_device *dev, const uint8_t *payload,
			 size_t len, uint8_t *resp, size_t resp_len);

/*
 * Send command payload and wait for the device to acknowledge it by echoing
 * the command byte on the interrupt IN endpoint.
 * On wireless receiver whose mouse is asleep the send is re-tried to wake the link.
 * Returns 0 on ACK, -1 on I/O error and -2 when the device stayed silent.
 */
int steelseries_cmd(struct alloy_device *dev, const uint8_t *payload,
		    size_t len);

#endif /* STEELSERIES_COMMON_H */
