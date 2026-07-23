// SPDX-License-Identifier: GPL-2.0-only
/*
 * hidraw transport layer.
 *
 * Device discovery walks /sys/class/hidraw and matches two
 * uevent fields of the parent HID device:
 *
 *   HID_ID=0003:00001038:00001870          -> bus:VID:PID
 *   HID_PHYS=usb-0000:2f:00.3-2/input3    -> USB interface number
 *
 * Mice do not use numbered HID reports, so write() on the hidraw node
 * is the report number 0x00 followed by the 64-byte payload.
 *
 * Recognized commands are echoed back on the interrupt IN endpoint.
 * Silence means the firmware ignored the command.
 */
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "hid.h"

#define ALLOY_HID_ACK_TIMEOUT_MS 400
/* pause between wake attempts, so sleeping link has moment to come back */
#define ALLOY_HID_RETRY_DELAY_MS 40

static int uevent_matches(const char *path, uint16_t vendor_id,
			  uint16_t product_id, int interface)
{
	char buf[512];
	char want_id[32];
	char want_phys[16];
	const char *phys;
	FILE *f;
	int id_ok = 0;
	int phys_ok = 0;

	f = fopen(path, "re");
	if (!f)
		return 0;

	snprintf(want_id, sizeof(want_id), "%04X:%08X:%08X", 3, vendor_id,
		 product_id);
	snprintf(want_phys, sizeof(want_phys), "/input%d", interface);

	while (fgets(buf, sizeof(buf), f)) {
		buf[strcspn(buf, "\n")] = '\0';
		if (!strncmp(buf, "HID_ID=", 7) && !strcmp(buf + 7, want_id))
			id_ok = 1;
		if (!strncmp(buf, "HID_PHYS=", 9)) {
			phys = strrchr(buf + 9, '/');
			if (phys && !strcmp(phys, want_phys))
				phys_ok = 1;
		}
	}
	fclose(f);

	return id_ok && phys_ok;
}

/*
 * Locate the hidraw node matching the VID/PID/interface triple.
 * On a match, copies "/dev/hidrawN" into node (when non-NULL) and returns 1;
 * returns 0 when no connected device matches.
 */
static int hid_find_node(uint16_t vendor_id, uint16_t product_id, int interface,
			 char *node, size_t node_len)
{
	char path[288];
	struct dirent *ent;
	DIR *dir;
	int found = 0;

	dir = opendir("/sys/class/hidraw");
	if (!dir)
		return 0;

	while ((ent = readdir(dir))) {
		if (strncmp(ent->d_name, "hidraw", 6))
			continue;

		snprintf(path, sizeof(path),
			 "/sys/class/hidraw/%s/device/uevent", ent->d_name);
		if (!uevent_matches(path, vendor_id, product_id, interface))
			continue;

		if (node)
			snprintf(node, node_len, "/dev/%s", ent->d_name);
		found = 1;
		break;
	}
	closedir(dir);

	return found;
}

int alloy_hid_present(uint16_t vendor_id, uint16_t product_id, int interface)
{
	return hid_find_node(vendor_id, product_id, interface, NULL, 0);
}

/*
 * Locate the hidraw node device exposes on the given bus type, matched by product id
 * only (vendor and interface ignored - Bluetooth re-brands the vendor and carries every
 * report on one node).
 * On a match, copies "/dev/hidrawN" into node (when non-NULL) and returns 1;
 * returns 0 when no connected device matches.
 */
static int hid_find_node_bus(uint16_t bustype, uint16_t product_id, char *node,
			     size_t node_len)
{
	char path[288];
	struct dirent *ent;
	DIR *dir;
	int found = 0;

	dir = opendir("/sys/class/hidraw");
	if (!dir)
		return 0;

	while ((ent = readdir(dir))) {
		unsigned int bus = 0;
		unsigned int vid = 0;
		unsigned int pid = 0;
		char buf[256];
		FILE *f;

		if (strncmp(ent->d_name, "hidraw", 6))
			continue;

		snprintf(path, sizeof(path),
			 "/sys/class/hidraw/%s/device/uevent", ent->d_name);
		f = fopen(path, "re");
		if (!f)
			continue;
		while (fgets(buf, sizeof(buf), f)) {
			if (strncmp(buf, "HID_ID=", 7))
				continue;
			if (sscanf(buf + 7, "%x:%x:%x", &bus, &vid, &pid) ==
				    3 &&
			    (uint16_t)bus == bustype &&
			    (uint16_t)pid == product_id)
				found = 1;
			break;
		}
		fclose(f);
		if (found) {
			if (node)
				snprintf(node, node_len, "/dev/%s",
					 ent->d_name);
			break;
		}
	}
	closedir(dir);

	return found;
}

int alloy_hid_present_bus(uint16_t bustype, uint16_t product_id)
{
	return hid_find_node_bus(bustype, product_id, NULL, 0);
}

int alloy_hid_open(struct alloy_hid_dev *dev, uint16_t vendor_id,
		   uint16_t product_id, int interface, size_t report_size)
{
	char node[288];

	dev->fd = -1;
	dev->report_size = report_size ? report_size : ALLOY_HID_REPORT_SIZE;
	dev->vendor_id = vendor_id;
	dev->product_id = product_id;
	dev->interface = interface;
	dev->report_id = 0; /* USB path: single unnumbered report */

	if (!hid_find_node(vendor_id, product_id, interface, node,
			   sizeof(node)))
		return -1;

	dev->fd = open(node, O_RDWR | O_CLOEXEC);
	return dev->fd < 0 ? -1 : 0;
}

int alloy_hid_open_bus(struct alloy_hid_dev *dev, uint16_t bustype,
		       uint16_t product_id, uint8_t report_id,
		       size_t report_size)
{
	char node[288];

	dev->fd = -1;
	dev->report_size = report_size ? report_size : ALLOY_HID_REPORT_SIZE;
	dev->vendor_id = 0; /* bus re-brands it; unused on this path */
	dev->product_id = product_id;
	dev->interface = -1; /* one node carries every report */
	dev->report_id = report_id;

	if (!hid_find_node_bus(bustype, product_id, node, sizeof(node)))
		return -1;

	dev->fd = open(node, O_RDWR | O_CLOEXEC);
	return dev->fd < 0 ? -1 : 0;
}

/*
 * Re-open hidraw node after the receiver re-enumerates and the old fd goes stale.
 * Re-runs discovery because the node number typically changes.
 * Returns 0 on fresh fd, -1 when the device is gone.
 */
static int hid_reopen(struct alloy_hid_dev *dev)
{
	char node[288];

	if (dev->fd >= 0) {
		close(dev->fd);
		dev->fd = -1;
	}
	if (!hid_find_node(dev->vendor_id, dev->product_id, dev->interface,
			   node, sizeof(node)))
		return -1;
	dev->fd = open(node, O_RDWR | O_CLOEXEC);
	return dev->fd < 0 ? -1 : 0;
}

void alloy_hid_close(struct alloy_hid_dev *dev)
{
	if (dev->fd >= 0)
		close(dev->fd);
	dev->fd = -1;
}

int alloy_hid_poll(struct alloy_hid_dev *dev, uint8_t *buf, size_t len)
{
	struct pollfd pfd = { .fd = dev->fd, .events = POLLIN };
	ssize_t n;
	int r;

	if (dev->fd < 0)
		return 0;
	r = poll(&pfd, 1, 0);
	if (r <= 0)
		return r < 0 ? -1 : 0;
	if (!(pfd.revents & POLLIN))
		return 0;
	n = read(dev->fd, buf, len);
	return n < 0 ? -1 : (int)n;
}

static int hid_write_report(struct alloy_hid_dev *dev, const uint8_t *payload,
			    size_t len)
{
	uint8_t buf[1 + ALLOY_HID_REPORT_SIZE];
	size_t total = 1 + dev->report_size;
	ssize_t n;

	if (len > dev->report_size)
		return -1;

	memset(buf, 0, sizeof(buf));
	buf[0] = dev->report_id; /* report number; 0 = unnumbered (USB path) */
	memcpy(buf + 1, payload, len);

	n = write(dev->fd, buf, total);
	if (n != (ssize_t)total)
		return -1;
	return 0;
}

int alloy_hid_send(struct alloy_hid_dev *dev, const uint8_t *payload,
		   size_t len)
{
	if (!len)
		return -1;
	return hid_write_report(dev, payload, len);
}

static int hid_read_report(struct alloy_hid_dev *dev, uint8_t *resp,
			   size_t resp_len, int timeout_ms)
{
	struct pollfd pfd;
	ssize_t n;
	int ret;

	pfd.fd = dev->fd;
	pfd.events = POLLIN;

	ret = poll(&pfd, 1, timeout_ms);
	if (ret < 0)
		return -1;
	if (ret == 0)
		return -2;

	n = read(dev->fd, resp, resp_len);
	if (n < 0)
		return -1;
	return (int)n;
}

static void hid_drain(struct alloy_hid_dev *dev)
{
	uint8_t scratch[ALLOY_HID_REPORT_SIZE];

	while (hid_read_report(dev, scratch, sizeof(scratch), 0) > 0)
		;
}

static long hid_now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

/*
 * Receiver's "mouse asleep / not linked" reply.
 * On the config interface it arrives instead of the command echo whenever
 * the 2.4 GHz link has idled, and is also pushed unsolicited while idle -
 * so it must be skipped, not mistaken for rejected command.
 */
static int hid_is_idle_marker(const uint8_t *resp, int n)
{
	return n >= 2 && resp[0] == 0x40 && resp[1] == 0xFF;
}

/*
 * Read reports until one whose first byte is @want arrives
 * (want < 0 accepts any report that is not the idle marker),
 * discarding the idle marker and any unrelated report (e.g. a stray event)
 * that lands first, within an overall deadline.
 * Reading single report is not enough:
 * config interface interleaves idle markers with echoes, so the first report
 * back is not necessarily the ACK.
 * Returns the matching report length, -1 on I/O error, -2 on deadline.
 */
static int hid_read_matching(struct alloy_hid_dev *dev, int want, uint8_t *resp,
			     size_t resp_len, int deadline_ms)
{
	long deadline = hid_now_ms() + deadline_ms;
	long remaining;

	while ((remaining = deadline - hid_now_ms()) > 0) {
		int n = hid_read_report(dev, resp, resp_len, (int)remaining);

		if (n == -2)
			return -2; /* poll timed out */
		if (n < 0)
			return -1; /* I/O error */
		if (hid_is_idle_marker(resp, n))
			continue; /* mouse asleep/unlinked, keep waiting */
		if (want >= 0 && (n < 1 || resp[0] != (uint8_t)want))
			continue; /* not our echo, skip stray report */
		return n;
	}
	return -2;
}

int alloy_hid_cmd_read_want(struct alloy_hid_dev *dev, const uint8_t *payload,
			    size_t len, int want, uint8_t *resp,
			    size_t resp_len, int attempts)
{
	int i;

	if (!len)
		return -1;
	if (attempts < 1)
		attempts = 1;

	for (i = 0; i < attempts; i++) {
		int n;

		if (dev->fd < 0 && hid_reopen(dev))
			return -1;

		hid_drain(dev);

		if (hid_write_report(dev, payload, len)) {
			/* stale node after re-enumeration: re-open and retry */
			if (hid_reopen(dev))
				return -1;
			continue;
		}

		n = hid_read_matching(dev, want, resp, resp_len,
				      ALLOY_HID_ACK_TIMEOUT_MS);
		if (n > 0)
			return n; /* got the echo / reply */
		if (n == -1) {
			/* node likely went away mid-exchange: re-open and retry */
			if (hid_reopen(dev))
				return -1;
			continue;
		}
		/* n == -2: idle marker / silence, the mouse is asleep - nudge again */
		usleep(ALLOY_HID_RETRY_DELAY_MS * 1000);
	}
	return -2;
}

int alloy_hid_cmd_read(struct alloy_hid_dev *dev, const uint8_t *payload,
		       size_t len, uint8_t *resp, size_t resp_len)
{
	return alloy_hid_cmd_read_want(dev, payload, len, -1, resp, resp_len,
				       ALLOY_HID_ATTEMPTS_CMD);
}

int alloy_hid_cmd(struct alloy_hid_dev *dev, const uint8_t *payload, size_t len)
{
	uint8_t resp[ALLOY_HID_REPORT_SIZE];
	int n;

	if (!len)
		return -1;

	/* ACK echoes the command byte; wake the link hard for config writes */
	n = alloy_hid_cmd_read_want(dev, payload, len, payload[0], resp,
				    sizeof(resp), ALLOY_HID_ATTEMPTS_CMD);
	if (n > 0)
		return 0;
	return n; /* -1 I/O error, -2 no ACK */
}
