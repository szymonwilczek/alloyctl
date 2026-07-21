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
#include <unistd.h>

#include "hid.h"

#define ALLOY_HID_ACK_TIMEOUT_MS 400

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

int alloy_hid_present_bus(uint16_t bustype, uint16_t product_id)
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
		if (found)
			break;
	}
	closedir(dir);

	return found;
}

int alloy_hid_open(struct alloy_hid_dev *dev, uint16_t vendor_id,
		   uint16_t product_id, int interface, size_t report_size)
{
	char node[288];

	dev->fd = -1;
	dev->report_size = report_size ? report_size : ALLOY_HID_REPORT_SIZE;

	if (!hid_find_node(vendor_id, product_id, interface, node,
			   sizeof(node)))
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
	buf[0] = 0x00; /* report number: device uses unnumbered reports */
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

int alloy_hid_cmd_read(struct alloy_hid_dev *dev, const uint8_t *payload,
		       size_t len, uint8_t *resp, size_t resp_len)
{
	int ret;

	if (!len)
		return -1;

	hid_drain(dev);

	ret = hid_write_report(dev, payload, len);
	if (ret)
		return ret;

	return hid_read_report(dev, resp, resp_len, ALLOY_HID_ACK_TIMEOUT_MS);
}

int alloy_hid_cmd(struct alloy_hid_dev *dev, const uint8_t *payload, size_t len)
{
	uint8_t resp[ALLOY_HID_REPORT_SIZE];
	int ret;

	ret = alloy_hid_cmd_read(dev, payload, len, resp, sizeof(resp));
	if (ret < 0)
		return ret;

	/* ACK echoes the command byte */
	if (ret < 1 || resp[0] != payload[0])
		return -2;
	return 0;
}
