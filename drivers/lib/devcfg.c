/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Conventional device settings: defaults, persistence and command-line flags.
 * See devcfg.h - none of this is core, all of it is opt-in.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "lib/devcfg.h"

void alloy_devcfg_defaults(const struct alloy_driver *drv,
			   struct alloy_config *cfg)
{
	const struct alloy_devinfo *info = alloy_devinfo(drv);
	struct alloy_devcfg *d = alloy_devcfg(cfg);
	uint8_t i;
	uint8_t p;

	if (!info)
		return;

	d->polling_hz = info->num_polling_rates ? info->polling_rates[0] : 0;
	d->brightness = 100;

	for (i = 0; i < info->num_zones && i < ALLOY_MAX_LED_ZONES; i++) {
		d->zone_color[i] = info->zones[i].def_color;
		d->zone_fx[i] = 0; /* steady */
		for (p = 0; p < ALLOY_FX_PARAMS; p++)
			d->zone_fx_param[i][p] = ALLOY_FX_RATE_DEF;
	}
}

void alloy_devcfg_state_save(const struct alloy_driver *drv,
			     const struct alloy_config *cfg, void *ctx,
			     alloy_state_emit_fn emit)
{
	const struct alloy_devinfo *info = alloy_devinfo(drv);
	const struct alloy_devcfg *d = alloy_devcfg_c(cfg);
	char key[48];
	char val[48];
	uint8_t i;
	uint8_t p;

	if (!info)
		return;

	if (info->num_polling_rates) {
		snprintf(val, sizeof(val), "%u", d->polling_hz);
		emit(ctx, "polling_hz", val);
	}
	if (info->caps & ALLOY_CAP_BRIGHTNESS) {
		snprintf(val, sizeof(val), "%u", d->brightness);
		emit(ctx, "brightness", val);
	}

	for (i = 0; i < info->num_zones && i < ALLOY_MAX_LED_ZONES; i++) {
		snprintf(key, sizeof(key), "zone%u", i);
		snprintf(val, sizeof(val), "%02x%02x%02x", d->zone_color[i].r,
			 d->zone_color[i].g, d->zone_color[i].b);
		emit(ctx, key, val);

		if (info->num_fx <= 1)
			continue;

		snprintf(key, sizeof(key), "zone_fx%u", i);
		snprintf(val, sizeof(val), "%u", d->zone_fx[i]);
		emit(ctx, key, val);

		for (p = 0; p < ALLOY_FX_PARAMS; p++) {
			snprintf(key, sizeof(key), "zone_param%u_%u", i, p);
			snprintf(val, sizeof(val), "%u",
				 d->zone_fx_param[i][p]);
			emit(ctx, key, val);
		}
	}
}

int alloy_devcfg_state_load(const struct alloy_driver *drv,
			    struct alloy_config *cfg, const char *key,
			    const char *val)
{
	struct alloy_devcfg *d = alloy_devcfg(cfg);
	unsigned idx;
	unsigned slot;
	unsigned rgb;

	(void)drv;

	if (!strcmp(key, "polling_hz")) {
		d->polling_hz = (uint16_t)atoi(val);
		return 1;
	}
	if (!strcmp(key, "brightness")) {
		d->brightness = (uint8_t)ALLOY_CLAMP(atoi(val), 0, 100);
		return 1;
	}
	if (sscanf(key, "zone_param%u_%u", &idx, &slot) == 2 &&
	    idx < ALLOY_MAX_LED_ZONES && slot < ALLOY_FX_PARAMS) {
		d->zone_fx_param[idx][slot] =
			(uint8_t)ALLOY_CLAMP(atoi(val), 0, 255);
		return 1;
	}
	if (sscanf(key, "zone_fx%u", &idx) == 1 && idx < ALLOY_MAX_LED_ZONES) {
		d->zone_fx[idx] = (uint8_t)ALLOY_CLAMP(atoi(val), 0, 255);
		return 1;
	}
	if (sscanf(key, "zone%u", &idx) == 1 && idx < ALLOY_MAX_LED_ZONES) {
		if (sscanf(val, "%x", &rgb) == 1) {
			d->zone_color[idx].r = (rgb >> 16) & 0xFF;
			d->zone_color[idx].g = (rgb >> 8) & 0xFF;
			d->zone_color[idx].b = rgb & 0xFF;
		}
		return 1;
	}
	return 0;
}

static int has_cap(const struct alloy_driver *drv, uint64_t cap)
{
	const struct alloy_devinfo *info = alloy_devinfo(drv);

	return info && (info->caps & cap);
}

static int avail_brightness(const struct alloy_driver *drv)
{
	return has_cap(drv, ALLOY_CAP_BRIGHTNESS);
}

static int avail_color(const struct alloy_driver *drv)
{
	return has_cap(drv, ALLOY_CAP_COLOR);
}

static int avail_polling(const struct alloy_driver *drv)
{
	const struct alloy_devinfo *info = alloy_devinfo(drv);

	return info && info->num_polling_rates > 0;
}

static int avail_fx(const struct alloy_driver *drv)
{
	const struct alloy_devinfo *info = alloy_devinfo(drv);

	return info && info->num_fx > 1;
}

static int parse_brightness(const struct alloy_driver *drv, const char *arg,
			    struct alloy_config *cfg, char *err, size_t err_len)
{
	int val;

	(void)drv;
	if (!arg || sscanf(arg, "%d", &val) != 1 || val < 0 || val > 100) {
		snprintf(err, err_len,
			 "invalid brightness '%s'; expected 0-100",
			 arg ? arg : "");
		return -1;
	}
	alloy_devcfg(cfg)->brightness = (uint8_t)val;
	return 0;
}

static int parse_polling(const struct alloy_driver *drv, const char *arg,
			 struct alloy_config *cfg, char *err, size_t err_len)
{
	const struct alloy_devinfo *info = alloy_devinfo(drv);
	int hz;
	uint8_t i;

	if (!arg || sscanf(arg, "%d", &hz) != 1) {
		snprintf(err, err_len, "invalid polling rate '%s'",
			 arg ? arg : "");
		return -1;
	}
	for (i = 0; info && i < info->num_polling_rates; i++) {
		if (info->polling_rates[i] == (uint16_t)hz) {
			alloy_devcfg(cfg)->polling_hz = (uint16_t)hz;
			return 0;
		}
	}
	snprintf(err, err_len, "%d Hz is not one this device offers", hz);
	return -1;
}

/* --color <zone>:RRGGBB, or RRGGBB for every zone */
static int parse_color(const struct alloy_driver *drv, const char *arg,
		       struct alloy_config *cfg, char *err, size_t err_len)
{
	const struct alloy_devinfo *info = alloy_devinfo(drv);
	struct alloy_devcfg *d = alloy_devcfg(cfg);
	unsigned zone;
	unsigned rgb;
	uint8_t i;

	if (arg && sscanf(arg, "%u:%x", &zone, &rgb) == 2) {
		if (!info || zone >= info->num_zones) {
			snprintf(err, err_len, "no zone %u on this device",
				 zone);
			return -1;
		}
		d->zone_color[zone].r = (rgb >> 16) & 0xFF;
		d->zone_color[zone].g = (rgb >> 8) & 0xFF;
		d->zone_color[zone].b = rgb & 0xFF;
		return 0;
	}
	if (arg && sscanf(arg, "%x", &rgb) == 1) {
		for (i = 0; info && i < info->num_zones; i++) {
			d->zone_color[i].r = (rgb >> 16) & 0xFF;
			d->zone_color[i].g = (rgb >> 8) & 0xFF;
			d->zone_color[i].b = rgb & 0xFF;
		}
		return 0;
	}
	snprintf(err, err_len,
		 "invalid color '%s'; expected RRGGBB or ZONE:RRGGBB",
		 arg ? arg : "");
	return -1;
}

static int parse_fx(const struct alloy_driver *drv, const char *arg,
		    struct alloy_config *cfg, char *err, size_t err_len)
{
	const struct alloy_devinfo *info = alloy_devinfo(drv);
	struct alloy_devcfg *d = alloy_devcfg(cfg);
	uint8_t i;

	if (!arg || !*arg) {
		snprintf(err, err_len, "missing effect name");
		return -1;
	}
	for (i = 0; info && i < info->num_fx; i++) {
		if (strcasecmp(info->fx_names[i], arg))
			continue;
		for (uint8_t z = 0; z < info->num_zones; z++)
			d->zone_fx[z] = i;
		return 0;
	}
	snprintf(err, err_len, "unknown effect '%s'", arg);
	return -1;
}

const struct alloy_cli_option alloy_devcfg_cli_options[] = {
	{
		.name = "--brightness",
		.short_name = "-b",
		.arg_desc = "<0-100>",
		.help = "Set illumination brightness",
		.has_arg = 1,
		.available = avail_brightness,
		.parse = parse_brightness,
		.apply_step = ALLOY_STEP_BRIGHTNESS,
	},
	{
		.name = "--polling",
		.alias = "--rate",
		.arg_desc = "<hz>",
		.help = "Set the report rate",
		.has_arg = 1,
		.available = avail_polling,
		.parse = parse_polling,
		.apply_step = ALLOY_STEP_POLLING,
	},
	{
		.name = "--color",
		.arg_desc = "[zone:]RRGGBB",
		.help = "Set a lighting color",
		.has_arg = 1,
		.available = avail_color,
		.parse = parse_color,
		.apply_step = ALLOY_STEP_COLORS,
	},
	{
		.name = "--fx",
		.alias = "--effect",
		.arg_desc = "<name>",
		.help = "Select a lighting effect",
		.has_arg = 1,
		.available = avail_fx,
		.parse = parse_fx,
		.apply_step = ALLOY_STEP_COLORS,
	},
};

_Static_assert(ALLOY_ARRAY_SIZE(alloy_devcfg_cli_options) ==
		       ALLOY_DEVCFG_CLI_COUNT,
	       "ALLOY_DEVCFG_CLI_COUNT is out of step with the table");
