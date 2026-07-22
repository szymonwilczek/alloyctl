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

int alloy_device_enumerate(const struct alloy_driver **out, int max)
{
	const struct alloy_driver *const *iter;
	int n = 0;

	alloy_for_each_driver(iter)
	{
		if (!alloy_hid_present((*iter)->vendor_id, (*iter)->product_id,
				       (*iter)->interface))
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
	dev->hid.fd = -1;
	dev->ev.fd = -1;

	drv = alloy_driver_find(vendor_id, product_id);
	if (!drv)
		return -1;
	if (alloy_hid_open(&dev->hid, drv->vendor_id, drv->product_id,
			   drv->interface, drv->report_size))
		return -1;
	/*
	 * Event channel is best-effort:
	 * without it the device still configures fine,
	 * only device-initiated changes go unnoticed.
	 */
	if (drv->ops->parse_event &&
	    alloy_hid_open(&dev->ev, drv->vendor_id, drv->product_id,
			   drv->event_interface, drv->report_size))
		dev->ev.fd = -1;
	dev->drv = drv;
	return 0;
}

void alloy_device_close(struct alloy_device *dev)
{
	alloy_hid_close(&dev->ev);
	alloy_hid_close(&dev->hid);
	dev->drv = NULL;
}

void alloy_config_generic_defaults(const struct alloy_driver *drv,
				   struct alloy_config *cfg)
{
	uint8_t i;

	memset(cfg, 0, sizeof(*cfg));

	/*
	 * One preset out of the box;
	 * More are created on demand in the CPI LEVELS pane.
	 * Persisted baseline always overrides this.
	 */
	cfg->dpi_count = 1;
	cfg->dpi[0][0] = 800;
	cfg->dpi[0][1] = 800;
	cfg->dpi_active = 0;

	cfg->polling_hz = drv->num_polling_rates ? drv->polling_rates[0] : 1000;

	for (i = 0; i < drv->num_zones && i < ALLOY_MAX_LED_ZONES; i++) {
		cfg->zone_color[i] = drv->zones[i].def_color;
		cfg->zone_fx[i] = 0; /* steady */
		cfg->zone_fx_freq[i] = ALLOY_FX_RATE_DEF;
		cfg->zone_fx_speed[i] = ALLOY_FX_RATE_DEF;
	}

	cfg->brightness = 100;

	cfg->reactive_enabled = 0;
	cfg->reactive_color = (struct alloy_rgb){ 0xFF, 0xFF, 0xFF };

	cfg->startup_fx = (drv->caps & ALLOY_CAP_FX_RAINBOW) ?
				  ALLOY_STARTUP_RAINBOW :
				  ALLOY_STARTUP_OFF;

	for (i = 0; i < drv->num_buttons && i < ALLOY_MAX_BUTTONS; i++)
		cfg->buttons[i] = drv->buttons[i].def;

	cfg->acceleration = 0;
	cfg->deceleration = 0;
	cfg->angle_snapping = 0;
	cfg->accel_enabled = 0;

	/*
	 * Wireless power defaults (inert on wired mice, which never push them):
	 * mirror the GG out-of-box 5-minute sleep timer;
	 * smart mode and the LED dim timer stay off
	 */
	cfg->illum_smart = 0;
	cfg->illum_dim_s = 0;
	cfg->sleep_min = 5;
}
