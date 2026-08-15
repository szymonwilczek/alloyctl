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

#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include "cli.h"

static int parse_int_val(const char *str, int min, int max, int *out)
{
	char *end;
	long val;

	if (!str || !*str)
		return 0;
	val = strtol(str, &end, 10);
	if (*end != '\0' || val < min || val > max)
		return 0;
	*out = (int)val;
	return 1;
}

static int parse_hex_rgb_val(const char *str, struct alloy_rgb *rgb)
{
	unsigned val;

	if (!str || strlen(str) != 6)
		return -1;
	if (sscanf(str, "%6x", &val) != 1)
		return -1;
	rgb->r = (uint8_t)((val >> 16) & 0xFF);
	rgb->g = (uint8_t)((val >> 8) & 0xFF);
	rgb->b = (uint8_t)(val & 0xFF);
	return 0;
}

static int opt_parse_brightness(const char *arg, struct alloy_config *cfg,
				char *err_buf, size_t err_len)
{
	int b;
	if (!arg || !parse_int_val(arg, 0, 100, &b)) {
		snprintf(err_buf, err_len,
			 "invalid brightness '%s'; expected percentage 0-100",
			 arg ? arg : "");
		return -1;
	}
	cfg->common.brightness = (uint8_t)b;
	return 0;
}

static int opt_apply_brightness(struct alloy_device *dev,
				const struct alloy_config *cfg)
{
	if (dev->drv->ops && dev->drv->ops->apply_brightness)
		return dev->drv->ops->apply_brightness(dev, cfg);
	return 0;
}

static int opt_parse_polling(const char *arg, struct alloy_config *cfg,
			     char *err_buf, size_t err_len)
{
	int hz;
	if (!arg || !parse_int_val(arg, 1, 8000, &hz)) {
		snprintf(err_buf, err_len, "invalid polling rate '%s'",
			 arg ? arg : "");
		return -1;
	}
	cfg->common.polling_hz = (uint16_t)hz;
	return 0;
}

static int opt_validate_polling(const struct alloy_driver *drv,
				const struct alloy_config *cfg, char *err_buf,
				size_t err_len)
{
	int valid = 0;
	for (uint8_t i = 0; i < drv->num_polling_rates; i++) {
		if (drv->polling_rates[i] == cfg->common.polling_hz) {
			valid = 1;
			break;
		}
	}
	if (!valid && drv->num_polling_rates > 0) {
		snprintf(err_buf, err_len,
			 "unsupported polling rate %u Hz for '%s'",
			 cfg->common.polling_hz, drv->name);
		return -1;
	}
	return 0;
}

static int opt_apply_polling(struct alloy_device *dev,
			     const struct alloy_config *cfg)
{
	if (dev->drv->ops && dev->drv->ops->apply_polling)
		return dev->drv->ops->apply_polling(dev, cfg);
	return 0;
}

static int opt_parse_color(const char *arg, struct alloy_config *cfg,
			   char *err_buf, size_t err_len)
{
	const char *val = arg;
	const char *colon;
	int zone = -1;
	struct alloy_rgb rgb;

	if (!arg) {
		snprintf(err_buf, err_len,
			 "--color requires a hex color code ([zone:]RRGGBB)");
		return -1;
	}
	colon = strchr(arg, ':');
	if (colon) {
		char zbuf[16];
		size_t zlen = colon - arg;
		if (zlen >= sizeof(zbuf)) {
			snprintf(err_buf, err_len, "invalid zone in color '%s'",
				 arg);
			return -1;
		}
		memcpy(zbuf, arg, zlen);
		zbuf[zlen] = '\0';
		if (!parse_int_val(zbuf, 0, ALLOY_MAX_LED_ZONES - 1, &zone)) {
			snprintf(err_buf, err_len, "invalid zone '%s' in color",
				 zbuf);
			return -1;
		}
		val = colon + 1;
	}

	if (parse_hex_rgb_val(val, &rgb)) {
		snprintf(err_buf, err_len,
			 "invalid color format '%s'; expected RRGGBB", val);
		return -1;
	}

	if (zone >= 0) {
		cfg->common.zone_color[zone] = rgb;
	} else {
		for (uint8_t i = 0; i < ALLOY_MAX_LED_ZONES; i++)
			cfg->common.zone_color[i] = rgb;
	}
	return 0;
}

static int opt_apply_colors(struct alloy_device *dev,
			    const struct alloy_config *cfg)
{
	if (dev->drv->ops && dev->drv->ops->apply_colors)
		return dev->drv->ops->apply_colors(dev, cfg);
	return 0;
}

static int opt_parse_fx(const char *arg, struct alloy_config *cfg,
			char *err_buf, size_t err_len)
{
	const char *val = arg;
	const char *colon;
	int zone = -1;
	uint8_t mode = 0;

	if (!arg) {
		snprintf(err_buf, err_len, "--fx requires an effect mode");
		return -1;
	}
	colon = strchr(arg, ':');
	if (colon) {
		char zbuf[16];
		size_t zlen = colon - arg;
		if (zlen >= sizeof(zbuf)) {
			snprintf(err_buf, err_len, "invalid zone in fx '%s'",
				 arg);
			return -1;
		}
		memcpy(zbuf, arg, zlen);
		zbuf[zlen] = '\0';
		if (!parse_int_val(zbuf, 0, ALLOY_MAX_LED_ZONES - 1, &zone)) {
			snprintf(err_buf, err_len, "invalid zone '%s' in fx",
				 zbuf);
			return -1;
		}
		val = colon + 1;
	}

	if (!strcmp(val, "steady") || !strcmp(val, "static") ||
	    !strcmp(val, "0"))
		mode = 0;
	else if (!strcmp(val, "breath") || !strcmp(val, "breathing") ||
		 !strcmp(val, "1"))
		mode = 1;
	else if (!strcmp(val, "color_cycle") || !strcmp(val, "cycle") ||
		 !strcmp(val, "2"))
		mode = 2;
	else if (!strcmp(val, "rainbow") || !strcmp(val, "3"))
		mode = 3;
	else
		mode = (uint8_t)atoi(val);

	if (zone >= 0) {
		cfg->common.zone_fx[zone] = mode;
	} else {
		for (uint8_t i = 0; i < ALLOY_MAX_LED_ZONES; i++)
			cfg->common.zone_fx[i] = mode;
	}
	return 0;
}

const struct alloy_cli_option alloy_common_cli_options[] = {
	{
		.name = "--brightness",
		.arg_desc = "<0-100>",
		.help = "Set LED brightness level (0-100%)",
		.category = ALLOY_OPT_COMMON,
		.required_cap = ALLOY_CAP_BRIGHTNESS,
		.has_arg = 1,
		.parse = opt_parse_brightness,
		.apply = opt_apply_brightness,
	},
	{
		.name = "--polling",
		.arg_desc = "<hz>",
		.help = "Set USB polling rate in Hz (125, 250, 500, 1000)",
		.category = ALLOY_OPT_COMMON,
		.has_arg = 1,
		.parse = opt_parse_polling,
		.validate = opt_validate_polling,
		.apply = opt_apply_polling,
	},
	{
		.name = "--color",
		.arg_desc = "<[z:]hex>",
		.help = "Set LED color (e.g. FF0000 or 0:00FF88)",
		.category = ALLOY_OPT_COMMON,
		.has_arg = 1,
		.parse = opt_parse_color,
		.apply = opt_apply_colors,
	},
	{
		.name = "--fx",
		.arg_desc = "<[z:]mode>",
		.help = "Set lighting effect (steady, breath, etc.)",
		.category = ALLOY_OPT_COMMON,
		.has_arg = 1,
		.parse = opt_parse_fx,
		.apply = opt_apply_colors,
	},
};

const size_t alloy_num_common_cli_options =
	ALLOY_ARRAY_SIZE(alloy_common_cli_options);
