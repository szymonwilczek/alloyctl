/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Registries, device binding, opaque configuration storage
 * and the dispatch of the extension points a driver declares.
 *
 * Nothing here inspects what a driver actually does:
 * apply steps are looked up by the names their author chose, the configuration
 * is a block of bytes whose layout only the driver knows, and the transport
 * is whatever the driver named.
 */
#include <stdlib.h>
#include <string.h>

#include "driver.h"

/* Section bounds emitted by the linker for the registries */
extern const struct alloy_driver *const __start_alloy_drivers[];
extern const struct alloy_driver *const __stop_alloy_drivers[];
extern const struct alloy_cli_command *const __start_alloy_commands[];
extern const struct alloy_cli_command *const __stop_alloy_commands[];

const struct alloy_driver *const *alloy_driver_first(void)
{
	return __start_alloy_drivers;
}

const struct alloy_driver *const *alloy_driver_last(void)
{
	return __stop_alloy_drivers;
}

const struct alloy_cli_command *const *alloy_command_first(void)
{
	return __start_alloy_commands;
}

const struct alloy_cli_command *const *alloy_command_last(void)
{
	return __stop_alloy_commands;
}

const struct alloy_driver *alloy_driver_find(uint16_t vendor_id,
					     uint16_t product_id)
{
	const struct alloy_driver *const *iter;

	alloy_for_each_driver(iter)
	{
		if ((*iter)->vendor_id == vendor_id &&
		    (*iter)->product_id == product_id)
			return *iter;
	}
	return NULL;
}

const char *alloy_driver_kind(const struct alloy_driver *drv)
{
	if (drv && drv->kind && *drv->kind)
		return drv->kind;
	return "device";
}

/*
 * Flags are declared in groups, so this flattens them:
 * index 0 is the first flag of the first table, and NULL ends the walk.
 */
const struct alloy_cli_option *
alloy_driver_cli_at(const struct alloy_driver *drv, size_t idx)
{
	uint8_t t;

	if (!drv)
		return NULL;
	for (t = 0; t < drv->num_cli_tables; t++) {
		const struct alloy_cli_table *tab = &drv->cli_tables[t];

		if (idx < tab->count)
			return &tab->options[idx];
		idx -= tab->count;
	}
	return NULL;
}

struct alloy_config {
	const struct alloy_driver *drv;
	size_t size;
	uint8_t data[];
};

struct alloy_config *alloy_config_alloc(const struct alloy_driver *drv)
{
	size_t size = drv->config_size;
	struct alloy_config *cfg;

	cfg = calloc(1, sizeof(*cfg) + size);
	if (!cfg)
		return NULL;
	cfg->drv = drv;
	cfg->size = size;
	return cfg;
}

void alloy_config_free(struct alloy_config *cfg)
{
	free(cfg);
}

void alloy_config_copy(struct alloy_config *dst, const struct alloy_config *src)
{
	if (!dst || !src || dst->size != src->size)
		return;
	memcpy(dst->data, src->data, dst->size);
}

int alloy_config_equal(const struct alloy_config *a,
		       const struct alloy_config *b)
{
	if (!a || !b || a->size != b->size)
		return 0;
	return memcmp(a->data, b->data, a->size) == 0;
}

void *alloy_config_data(struct alloy_config *cfg)
{
	return cfg ? cfg->data : NULL;
}

const void *alloy_config_data_c(const struct alloy_config *cfg)
{
	return cfg ? cfg->data : NULL;
}

const struct alloy_driver *alloy_config_driver(const struct alloy_config *cfg)
{
	return cfg ? cfg->drv : NULL;
}

void alloy_config_defaults(const struct alloy_driver *drv,
			   struct alloy_config *cfg)
{
	if (!cfg)
		return;
	memset(cfg->data, 0, cfg->size);
	if (drv->ops && drv->ops->config_defaults)
		drv->ops->config_defaults(drv, cfg);
}

static const struct alloy_transport *
driver_transport(const struct alloy_driver *drv)
{
	return (drv && drv->transport) ? drv->transport : &alloy_hid_transport;
}

int alloy_driver_present(const struct alloy_driver *drv)
{
	const struct alloy_transport *tr = driver_transport(drv);

	return tr->present ? tr->present(drv) : 0;
}

int alloy_dev_write(struct alloy_device *dev, const uint8_t *buf, size_t len)
{
	return dev->tr->write ? dev->tr->write(dev, buf, len) : -1;
}

int alloy_dev_read(struct alloy_device *dev, uint8_t *buf, size_t len,
		   int timeout_ms)
{
	return dev->tr->read ? dev->tr->read(dev, buf, len, timeout_ms) : -1;
}

int alloy_dev_poll_event(struct alloy_device *dev, uint8_t *buf, size_t len)
{
	return dev->tr->poll_event ? dev->tr->poll_event(dev, buf, len) : -1;
}

int alloy_dev_send_feature(struct alloy_device *dev, const uint8_t *buf,
			   size_t len)
{
	return dev->tr->send_feature ? dev->tr->send_feature(dev, buf, len) :
				       -1;
}

int alloy_dev_get_feature(struct alloy_device *dev, uint8_t *buf, size_t len)
{
	return dev->tr->get_feature ? dev->tr->get_feature(dev, buf, len) : -1;
}

int alloy_device_enumerate(const struct alloy_driver **out, int max)
{
	const struct alloy_driver *const *iter;
	int n = 0;

	alloy_for_each_driver(iter)
	{
		if (!alloy_driver_present(*iter))
			continue;
		if (out && n < max)
			out[n] = *iter;
		n++;
	}
	return n;
}

int alloy_device_open_id(struct alloy_device *dev, uint16_t vendor_id,
			 uint16_t product_id)
{
	const struct alloy_driver *drv;

	memset(dev, 0, sizeof(*dev));

	drv = alloy_driver_find(vendor_id, product_id);
	if (!drv)
		return -1;
	dev->tr = driver_transport(drv);

	if (!dev->tr->open || dev->tr->open(dev, drv))
		return -1;

	/*
	 * event channel is best-effort:
	 * without it the device still configures fine,
	 * only device-initiated changes go unnoticed
	 */
	if (drv->ops && drv->ops->parse_event && dev->tr->open_events)
		dev->tr->open_events(dev, drv);

	dev->drv = drv;
	return 0;
}

void alloy_device_close(struct alloy_device *dev)
{
	if (dev->tr && dev->tr->close)
		dev->tr->close(dev);
	dev->drv = NULL;
}

const struct alloy_apply_step *alloy_driver_step(const struct alloy_driver *drv,
						 const char *name)
{
	uint8_t i;

	if (!drv || !name || !drv->apply_steps)
		return NULL;
	for (i = 0; i < drv->num_apply_steps; i++) {
		if (!strcmp(drv->apply_steps[i].name, name))
			return &drv->apply_steps[i];
	}
	return NULL;
}

int alloy_driver_apply(struct alloy_device *dev, const struct alloy_config *cfg,
		       const char *name)
{
	const struct alloy_apply_step *step = alloy_driver_step(dev->drv, name);

	if (!step || !step->fn)
		return 0;
	return step->fn(dev, cfg);
}

void alloy_driver_apply_all(struct alloy_device *dev,
			    const struct alloy_config *cfg, uint32_t skip_flags,
			    void (*report)(void *ctx, const char *what,
					   int err),
			    void *ctx)
{
	const struct alloy_driver *drv = dev->drv;
	uint8_t i;

	for (i = 0; i < drv->num_apply_steps; i++) {
		const struct alloy_apply_step *step = &drv->apply_steps[i];
		int ret;

		if (!step->fn)
			continue;
		if (step->flags & (ALLOY_APPLY_MANUAL | skip_flags))
			continue;

		ret = step->fn(dev, cfg);
		if (report)
			report(ctx, step->name, ret);
	}
}
