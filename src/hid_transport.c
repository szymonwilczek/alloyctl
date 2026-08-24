/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * The default transport: Linux hidraw.
 *
 * It is configured entirely by the driver's struct alloy_hid_params,
 * which the core carries as an opaque pointer.
 *
 * Driver whose device does not fit this shape - bridge, bulk endpoint,
 * different report discipline - supplies its own struct alloy_transport instead,
 * and nothing in the core changes.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver.h"
#include "hid.h"

/* Both hidraw channels of one bound device */
struct hid_tr_state {
	struct alloy_hid_dev cfg;
	struct alloy_hid_dev ev;
};

static const struct alloy_hid_params *params(const struct alloy_driver *drv)
{
	static const struct alloy_hid_params defaults;

	return drv->transport_data ?
		       (const struct alloy_hid_params *)drv->transport_data :
		       &defaults;
}

static struct hid_tr_state *state(struct alloy_device *dev)
{
	return (struct hid_tr_state *)dev->tr_data;
}

static int hid_tr_present(const struct alloy_driver *drv)
{
	const struct alloy_hid_params *p = params(drv);

	if (p->bustype)
		return alloy_hid_present_bus(p->bustype, drv->product_id);
	return alloy_hid_present(drv->vendor_id, drv->product_id, p->interface);
}

static int hid_tr_open(struct alloy_device *dev, const struct alloy_driver *drv)
{
	const struct alloy_hid_params *p = params(drv);
	struct hid_tr_state *st;
	int ret;

	st = calloc(1, sizeof(*st));
	if (!st)
		return -1;
	st->cfg.fd = -1;
	st->ev.fd = -1;
	dev->tr_data = st;

	if (p->bustype)
		ret = alloy_hid_open_bus(&st->cfg, p->bustype, drv->product_id,
					 p->report_id, p->report_size);
	else
		ret = alloy_hid_open(&st->cfg, drv->vendor_id, drv->product_id,
				     p->interface, p->report_size);
	if (ret) {
		free(st);
		dev->tr_data = NULL;
		return -1;
	}
	return 0;
}

static int hid_tr_open_events(struct alloy_device *dev,
			      const struct alloy_driver *drv)
{
	const struct alloy_hid_params *p = params(drv);
	struct hid_tr_state *st = state(dev);

	/* the BLE node carries no separate event channel */
	if (!st || p->bustype)
		return -1;

	if (alloy_hid_open(&st->ev, drv->vendor_id, drv->product_id,
			   p->event_interface, p->report_size)) {
		st->ev.fd = -1;
		return -1;
	}
	return 0;
}

static void hid_tr_close(struct alloy_device *dev)
{
	struct hid_tr_state *st = state(dev);

	if (!st)
		return;
	alloy_hid_close(&st->ev);
	alloy_hid_close(&st->cfg);
	free(st);
	dev->tr_data = NULL;
}

static int hid_tr_write(struct alloy_device *dev, const uint8_t *buf,
			size_t len)
{
	return state(dev) ? alloy_hid_write(&state(dev)->cfg, buf, len) : -1;
}

static int hid_tr_read(struct alloy_device *dev, uint8_t *buf, size_t len,
		       int timeout_ms)
{
	return state(dev) ?
		       alloy_hid_read(&state(dev)->cfg, buf, len, timeout_ms) :
		       -1;
}

static int hid_tr_poll_event(struct alloy_device *dev, uint8_t *buf, size_t len)
{
	struct hid_tr_state *st = state(dev);

	if (!st || st->ev.fd < 0)
		return -1;
	return alloy_hid_poll(&st->ev, buf, len);
}

static int hid_tr_send_feature(struct alloy_device *dev, const uint8_t *buf,
			       size_t len)
{
	return state(dev) ? alloy_hid_send_feature(&state(dev)->cfg, buf, len) :
			    -1;
}

static int hid_tr_get_feature(struct alloy_device *dev, uint8_t *buf,
			      size_t len)
{
	return state(dev) ? alloy_hid_get_feature(&state(dev)->cfg, buf, len) :
			    -1;
}

/*
 * udev rules granting the invoking user access to this device's hidraw nodes.
 * Matching is on the USB ids alone, so one rule covers every interface the driver
 * might open.
 */
static size_t hid_tr_udev_rules(const struct alloy_driver *drv, char *buf,
				size_t len)
{
	int n = snprintf(
		buf, len,
		"# %s\n"
		"KERNEL==\"hidraw*\", SUBSYSTEM==\"hidraw\", "
		"ATTRS{idVendor}==\"%04x\", ATTRS{idProduct}==\"%04x\", "
		"MODE=\"0660\", TAG+=\"uaccess\"\n",
		drv->name, drv->vendor_id, drv->product_id);

	return (n < 0 || (size_t)n >= len) ? 0 : (size_t)n;
}

const struct alloy_transport alloy_hid_transport = {
	.name = "hidraw",
	.present = hid_tr_present,
	.open = hid_tr_open,
	.open_events = hid_tr_open_events,
	.close = hid_tr_close,
	.write = hid_tr_write,
	.read = hid_tr_read,
	.poll_event = hid_tr_poll_event,
	.send_feature = hid_tr_send_feature,
	.get_feature = hid_tr_get_feature,
	.udev_rules = hid_tr_udev_rules,
};
