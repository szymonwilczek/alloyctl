// SPDX-License-Identifier: GPL-2.0-only
/*
 * Generic Linux hidraw transport layer.
 *
 * Device discovery walks /sys/class/hidraw and matches two
 * uevent fields of the parent HID device:
 *
 *   HID_ID=0003:00001038:00001870          -> bus:VID:PID
 *   HID_PHYS=usb-0000:2f:00.3-2/input3    -> USB interface number
 */
#include <dirent.h>
#include <fcntl.h>
#include <linux/hidraw.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include "hid.h"

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

static int uevent_matches_bus(const char *path, uint16_t bustype,
			      uint16_t product_id)
{
	char buf[512];
	char want_prefix[16];
	char want_suffix[16];
	FILE *f;
	int match = 0;

	f = fopen(path, "re");
	if (!f)
		return 0;

	snprintf(want_prefix, sizeof(want_prefix), "%04X:", bustype);
	snprintf(want_suffix, sizeof(want_suffix), ":%08X", product_id);

	while (fgets(buf, sizeof(buf), f)) {
		buf[strcspn(buf, "\n")] = '\0';
		if (!strncmp(buf, "HID_ID=", 7)) {
			const char *val = buf + 7;
			if (!strncmp(val, want_prefix, strlen(want_prefix))) {
				const char *colon = strrchr(val, ':');
				if (colon && !strcmp(colon, want_suffix))
					match = 1;
			}
		}
	}
	fclose(f);
	return match;
}

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
		if (strncmp(ent->d_name, "hidraw", 6))
			continue;

		snprintf(path, sizeof(path),
			 "/sys/class/hidraw/%s/device/uevent", ent->d_name);
		if (!uevent_matches_bus(path, bustype, product_id))
			continue;

		if (node)
			snprintf(node, node_len, "/dev/%s", ent->d_name);
		found = 1;
		break;
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
	dev->vendor_id = vendor_id;
	dev->product_id = product_id;
	dev->interface = interface;
	dev->report_id = 0;
	dev->report_size = report_size ? report_size :
					 ALLOY_HID_DEFAULT_REPORT_SIZE;

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
	dev->vendor_id = 0;
	dev->product_id = product_id;
	dev->interface = -1;
	dev->report_id = report_id;
	dev->report_size = report_size ? report_size :
					 ALLOY_HID_DEFAULT_REPORT_SIZE;

	if (!hid_find_node_bus(bustype, product_id, node, sizeof(node)))
		return -1;

	dev->fd = open(node, O_RDWR | O_CLOEXEC);
	return dev->fd < 0 ? -1 : 0;
}

int alloy_hid_reopen(struct alloy_hid_dev *dev)
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

int alloy_hid_write(struct alloy_hid_dev *dev, const uint8_t *payload,
		    size_t len)
{
	uint8_t buf[512];
	size_t total;
	ssize_t n;

	if (!dev || dev->fd < 0 || !payload || !len)
		return -1;

	total = 1 + dev->report_size;
	if (total > sizeof(buf) || len > dev->report_size)
		return -1;

	memset(buf, 0, total);
	buf[0] = dev->report_id;
	memcpy(buf + 1, payload, len);

	n = write(dev->fd, buf, total);
	return (n == (ssize_t)total) ? 0 : -1;
}

int alloy_hid_read(struct alloy_hid_dev *dev, uint8_t *resp, size_t resp_len,
		   int timeout_ms)
{
	struct pollfd pfd;
	ssize_t n;
	int ret;

	if (!dev || dev->fd < 0 || !resp || !resp_len)
		return -1;

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

int alloy_hid_send_feature(struct alloy_hid_dev *dev, const uint8_t *payload,
			   size_t len)
{
	if (!dev || dev->fd < 0 || !payload || !len)
		return -1;

	if (ioctl(dev->fd, HIDIOCSFEATURE(len), payload) < 0)
		return -1;
	return 0;
}

int alloy_hid_get_feature(struct alloy_hid_dev *dev, uint8_t *buf, size_t len)
{
	if (!dev || dev->fd < 0 || !buf || !len)
		return -1;

	if (ioctl(dev->fd, HIDIOCGFEATURE(len), buf) < 0)
		return -1;
	return 0;
}
