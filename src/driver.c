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
		const struct alloy_driver *drv = *iter;
		int present = drv->bustype ?
				      alloy_hid_present_bus(drv->bustype,
							    drv->product_id) :
				      alloy_hid_present(drv->vendor_id,
							drv->product_id,
							drv->interface);

		if (!present)
			continue;
		if (out && n < max)
			out[n] = drv;
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

	if (drv->bustype) {
		/*
		 * Bluetooth (HID-over-GATT):
		 * one node, matched by product id, config on the numbered Output report.
		 * No separate event interface.
		 * Device-initiated events are not tracked here!
		 */
		if (alloy_hid_open_bus(&dev->hid, drv->bustype, drv->product_id,
				       drv->report_id, drv->report_size))
			return -1;
		dev->drv = drv;
		return 0;
	}

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

const char *alloy_device_type_name(enum alloy_device_type type)
{
	switch (type) {
	case ALLOY_DEV_KEYBOARD:
		return "keyboard";
	case ALLOY_DEV_MOUSE:
	default:
		return "mouse";
	}
}

void alloy_config_common_defaults(const struct alloy_driver *drv,
				  struct alloy_config_common *common)
{
	uint8_t i;

	memset(common, 0, sizeof(*common));

	common->polling_hz = drv->num_polling_rates ? drv->polling_rates[0] :
						      1000;
	common->brightness = 100;

	for (i = 0; i < drv->num_zones && i < ALLOY_MAX_LED_ZONES; i++) {
		common->zone_color[i] = drv->zones[i].def_color;
		common->zone_fx[i] = 0; /* steady */
		common->zone_fx_freq[i] = ALLOY_FX_RATE_DEF;
		common->zone_fx_speed[i] = ALLOY_FX_RATE_DEF;
	}

	/*
	 * Wireless power defaults (inert on wired devices, which never push them):
	 * mirror the GG out-of-box 5-minute sleep timer;
	 * smart mode and the LED dim timer stay off
	 */
	common->illum_smart = 0;
	common->illum_dim_s = 0;
	common->sleep_min = ALLOY_SLEEP_MIN_DEFAULT;
}

void alloy_config_mouse_defaults(const struct alloy_driver *drv,
				 struct alloy_config *cfg)
{
	uint8_t i;

	memset(cfg, 0, sizeof(*cfg));
	alloy_config_common_defaults(drv, &cfg->common);

	/* One preset out of the box (800 CPI) */
	cfg->mouse.dpi_count = 1;
	cfg->mouse.dpi[0][0] = 800;
	cfg->mouse.dpi[0][1] = 800;
	cfg->mouse.dpi_active = 0;

	cfg->mouse.reactive_enabled = 0;
	cfg->mouse.reactive_color = (struct alloy_rgb){ 0xFF, 0xFF, 0xFF };

	cfg->mouse.startup_fx = (drv->caps & ALLOY_CAP_FX_RAINBOW) ?
					ALLOY_STARTUP_RAINBOW :
					ALLOY_STARTUP_OFF;

	for (i = 0; i < drv->num_buttons && i < ALLOY_MAX_BUTTONS; i++)
		cfg->mouse.buttons[i] = drv->buttons[i].def;

	cfg->mouse.acceleration = 0;
	cfg->mouse.deceleration = 0;
	cfg->mouse.angle_snapping = 0;
	cfg->mouse.accel_enabled = 0;
	cfg->mouse.high_efficiency = 0;
}

void alloy_config_keyboard_defaults(const struct alloy_driver *drv,
				    struct alloy_config *cfg)
{
	memset(cfg, 0, sizeof(*cfg));
	alloy_config_common_defaults(drv, &cfg->common);

	cfg->kbd.win_lock = 0;
}

void alloy_config_generic_defaults(const struct alloy_driver *drv,
				   struct alloy_config *cfg)
{
	switch (drv ? drv->type : ALLOY_DEV_MOUSE) {
	case ALLOY_DEV_KEYBOARD:
		alloy_config_keyboard_defaults(drv, cfg);
		break;
	case ALLOY_DEV_MOUSE:
	default:
		alloy_config_mouse_defaults(drv, cfg);
		break;
	}
}
