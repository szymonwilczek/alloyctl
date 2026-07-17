// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver registry and device binding.
 */
#include <string.h>

#include "driver.h"

/* Section bounds emitted by the linker for the alloy_drivers section */
extern const struct alloy_driver *const __start_alloy_drivers[];
extern const struct alloy_driver *const __stop_alloy_drivers[];

const struct alloy_driver *const *alloy_driver_first(void)
{
	return __start_alloy_drivers;
}

const struct alloy_driver *const *alloy_driver_last(void)
{
	return __stop_alloy_drivers;
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

int alloy_device_open(struct alloy_device *dev)
{
	const struct alloy_driver *const *iter;

	memset(dev, 0, sizeof(*dev));
	dev->hid.fd = -1;

	alloy_for_each_driver(iter)
	{
		if (alloy_hid_open(&dev->hid, (*iter)->vendor_id,
				   (*iter)->product_id,
				   (*iter)->interface) == 0) {
			dev->drv = *iter;
			return 0;
		}
	}
	return -1;
}

void alloy_device_close(struct alloy_device *dev)
{
	alloy_hid_close(&dev->hid);
	dev->drv = NULL;
}

void alloy_config_generic_defaults(const struct alloy_driver *drv,
				   struct alloy_config *cfg)
{
	uint8_t i;

	memset(cfg, 0, sizeof(*cfg));

	cfg->dpi_count = ALLOY_MIN(2, drv->dpi.max_presets);
	cfg->dpi[0][0] = 800;
	cfg->dpi[0][1] = 800;
	if (cfg->dpi_count > 1) {
		cfg->dpi[1][0] = 1600;
		cfg->dpi[1][1] = 1600;
	}
	cfg->dpi_active = 0;

	cfg->polling_hz = drv->num_polling_rates ? drv->polling_rates[0] : 1000;

	for (i = 0; i < drv->num_zones && i < ALLOY_MAX_LED_ZONES; i++)
		cfg->zone_color[i] = drv->zones[i].def_color;

	cfg->brightness = 100;

	for (i = 0; i < drv->num_buttons && i < ALLOY_MAX_BUTTONS; i++)
		cfg->buttons[i] = drv->buttons[i].def;

	cfg->acceleration = 0;
	cfg->deceleration = 0;
	cfg->angle_snapping = 0;
}
