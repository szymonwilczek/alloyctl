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

int alloy_hid_open(struct alloy_hid_dev *dev, uint16_t vendor_id,
		   uint16_t product_id, int interface)
{
	char path[288];
	struct dirent *ent;
	DIR *dir;

	dev->fd = -1;

	dir = opendir("/sys/class/hidraw");
	if (!dir)
		return -1;

	while ((ent = readdir(dir))) {
		if (strncmp(ent->d_name, "hidraw", 6))
			continue;

		snprintf(path, sizeof(path),
			 "/sys/class/hidraw/%s/device/uevent", ent->d_name);
		if (!uevent_matches(path, vendor_id, product_id, interface))
			continue;

		snprintf(path, sizeof(path), "/dev/%s", ent->d_name);
		dev->fd = open(path, O_RDWR | O_CLOEXEC);
		break;
	}
	closedir(dir);

	return dev->fd < 0 ? -1 : 0;
}

void alloy_hid_close(struct alloy_hid_dev *dev)
{
	if (dev->fd >= 0)
		close(dev->fd);
	dev->fd = -1;
}

static int hid_write_report(struct alloy_hid_dev *dev, const uint8_t *payload,
			    size_t len)
{
	uint8_t buf[1 + ALLOY_HID_REPORT_SIZE];
	ssize_t n;

	if (len > ALLOY_HID_REPORT_SIZE)
		return -1;

	memset(buf, 0, sizeof(buf));
	buf[0] = 0x00; /* report number: device uses unnumbered reports */
	memcpy(buf + 1, payload, len);

	n = write(dev->fd, buf, sizeof(buf));
	if (n != (ssize_t)sizeof(buf))
		return -1;
	return 0;
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
