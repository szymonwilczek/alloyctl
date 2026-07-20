// SPDX-License-Identifier: GPL-2.0-only
/*
 * Host-side configuration state, stored as flat key=value file:
 *
 *   dpi_count=2
 *   dpi_active=0
 *   dpi0=800:800
 *   polling_hz=1000
 *   zone0=ff0000
 *   zone_fx0=1
 *   zone_freq0=5
 *   zone_speed0=5
 *   brightness=100
 *   button0=mouse:1
 *
 * Unknown keys are ignored so newer files load on older builds.
 */
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "state.h"

static int state_path(const struct alloy_driver *drv, char *buf, size_t len,
		      int create_dirs)
{
	const char *xdg = getenv("XDG_CONFIG_HOME");
	const char *home = getenv("HOME");
	char dir[PATH_MAX];
	int n;

	if (xdg && *xdg)
		n = snprintf(dir, sizeof(dir), "%s/alloyctl", xdg);
	else if (home && *home)
		n = snprintf(dir, sizeof(dir), "%s/.config/alloyctl", home);
	else
		return -1;
	if (n < 0 || (size_t)n >= sizeof(dir))
		return -1;

	if (create_dirs && mkdir(dir, 0755) && errno != EEXIST)
		return -1;

	n = snprintf(buf, len, "%s/%04x-%04x.conf", dir, drv->vendor_id,
		     drv->product_id);
	if (n < 0 || (size_t)n >= len)
		return -1;
	return 0;
}

static const char *action_type_name(enum alloy_action_type type)
{
	switch (type) {
	case ALLOY_ACT_MOUSE:
		return "mouse";
	case ALLOY_ACT_DPI_CYCLE:
		return "dpi";
	case ALLOY_ACT_SCROLL_UP:
		return "scrollup";
	case ALLOY_ACT_SCROLL_DOWN:
		return "scrolldown";
	case ALLOY_ACT_KEYBOARD:
		return "key";
	case ALLOY_ACT_MEDIA:
		return "media";
	case ALLOY_ACT_DISABLED:
	default:
		return "disabled";
	}
}

static int parse_action(const char *val, struct alloy_action *act)
{
	char name[16];
	unsigned value = 0;
	const char *colon = strchr(val, ':');
	size_t n;

	if (colon) {
		n = ALLOY_MIN((size_t)(colon - val), sizeof(name) - 1);
		value = (unsigned)strtoul(colon + 1, NULL, 10);
	} else {
		n = ALLOY_MIN(strlen(val), sizeof(name) - 1);
	}
	memcpy(name, val, n);
	name[n] = '\0';

	if (!strcmp(name, "mouse"))
		act->type = ALLOY_ACT_MOUSE;
	else if (!strcmp(name, "dpi"))
		act->type = ALLOY_ACT_DPI_CYCLE;
	else if (!strcmp(name, "scrollup"))
		act->type = ALLOY_ACT_SCROLL_UP;
	else if (!strcmp(name, "scrolldown"))
		act->type = ALLOY_ACT_SCROLL_DOWN;
	else if (!strcmp(name, "key"))
		act->type = ALLOY_ACT_KEYBOARD;
	else if (!strcmp(name, "media"))
		act->type = ALLOY_ACT_MEDIA;
	else if (!strcmp(name, "disabled"))
		act->type = ALLOY_ACT_DISABLED;
	else
		return -1;

	act->value = (uint16_t)value;
	return 0;
}

static void parse_line(struct alloy_config *cfg, const char *key,
		       const char *val)
{
	unsigned idx;
	unsigned a;
	unsigned b;
	unsigned rgb;

	if (!strcmp(key, "dpi_count")) {
		cfg->dpi_count = (uint8_t)ALLOY_CLAMP(atoi(val), 1,
						      ALLOY_MAX_DPI_PRESETS);
	} else if (!strcmp(key, "dpi_active")) {
		cfg->dpi_active = (uint8_t)ALLOY_CLAMP(
			atoi(val), 0, ALLOY_MAX_DPI_PRESETS - 1);
	} else if (sscanf(key, "dpi%u", &idx) == 1 &&
		   idx < ALLOY_MAX_DPI_PRESETS) {
		if (sscanf(val, "%u:%u", &a, &b) == 2) {
			cfg->dpi[idx][0] = (uint16_t)a;
			cfg->dpi[idx][1] = (uint16_t)b;
		}
	} else if (!strcmp(key, "polling_hz")) {
		cfg->polling_hz = (uint16_t)atoi(val);
	} else if (sscanf(key, "zone%u", &idx) == 1 &&
		   idx < ALLOY_MAX_LED_ZONES) {
		if (sscanf(val, "%x", &rgb) == 1) {
			cfg->zone_color[idx].r = (rgb >> 16) & 0xFF;
			cfg->zone_color[idx].g = (rgb >> 8) & 0xFF;
			cfg->zone_color[idx].b = rgb & 0xFF;
		}
	} else if (sscanf(key, "zone_fx%u", &idx) == 1 &&
		   idx < ALLOY_MAX_LED_ZONES) {
		if (!strcmp(val, "rainbow"))
			cfg->zone_fx[idx] = 1;
		else if (!strcmp(val, "static"))
			cfg->zone_fx[idx] = 0;
		else
			cfg->zone_fx[idx] =
				(uint8_t)ALLOY_CLAMP(atoi(val), 0, 255);
	} else if (sscanf(key, "zone_freq%u", &idx) == 1 &&
		   idx < ALLOY_MAX_LED_ZONES) {
		cfg->zone_fx_freq[idx] = (uint8_t)ALLOY_CLAMP(
			atoi(val), ALLOY_FX_RATE_MIN, ALLOY_FX_RATE_MAX);
	} else if (sscanf(key, "zone_speed%u", &idx) == 1 &&
		   idx < ALLOY_MAX_LED_ZONES) {
		cfg->zone_fx_speed[idx] = (uint8_t)ALLOY_CLAMP(
			atoi(val), ALLOY_FX_RATE_MIN, ALLOY_FX_RATE_MAX);
	} else if (!strcmp(key, "reactive")) {
		if (sscanf(val, "%x", &rgb) == 1) {
			cfg->reactive_enabled = 1;
			cfg->reactive_color.r = (rgb >> 16) & 0xFF;
			cfg->reactive_color.g = (rgb >> 8) & 0xFF;
			cfg->reactive_color.b = rgb & 0xFF;
		} else {
			cfg->reactive_enabled = 0;
		}
	} else if (!strcmp(key, "startup_fx")) {
		cfg->startup_fx = (uint8_t)ALLOY_CLAMP(
			atoi(val), 0, ALLOY_STARTUP_REACTIVE_RAINBOW);
	} else if (!strcmp(key, "fx")) {
		for (idx = 0; idx < ALLOY_MAX_LED_ZONES; idx++)
			cfg->zone_fx[idx] =
				(uint8_t)ALLOY_CLAMP(atoi(val), 0, 255);
	} else if (!strcmp(key, "brightness")) {
		cfg->brightness = (uint8_t)ALLOY_CLAMP(atoi(val), 0, 100);
	} else if (sscanf(key, "button%u", &idx) == 1 &&
		   idx < ALLOY_MAX_BUTTONS) {
		parse_action(val, &cfg->buttons[idx]);
	} else if (!strcmp(key, "acceleration")) {
		cfg->acceleration = (int8_t)atoi(val);
	} else if (!strcmp(key, "deceleration")) {
		cfg->deceleration = (int8_t)atoi(val);
	} else if (!strcmp(key, "angle_snapping")) {
		cfg->angle_snapping = (uint8_t)atoi(val);
	} else if (!strcmp(key, "accel_enabled")) {
		cfg->accel_enabled = atoi(val) ? 1 : 0;
	}
}

int alloy_state_load(const struct alloy_driver *drv, struct alloy_config *cfg)
{
	char path[PATH_MAX];
	char line[128];
	char *eq;
	FILE *f;

	drv->config_defaults(drv, cfg);

	if (state_path(drv, path, sizeof(path), 0))
		return -1;

	f = fopen(path, "re");
	if (!f)
		return 1;

	while (fgets(line, sizeof(line), f)) {
		line[strcspn(line, "\n")] = '\0';
		if (line[0] == '#' || line[0] == '\0')
			continue;
		eq = strchr(line, '=');
		if (!eq)
			continue;
		*eq = '\0';
		parse_line(cfg, line, eq + 1);
	}
	fclose(f);

	/* edited file may point the active preset past the count */
	if (cfg->dpi_active >= cfg->dpi_count)
		cfg->dpi_active = (uint8_t)(cfg->dpi_count - 1);
	return 0;
}

int alloy_state_store(const struct alloy_driver *drv,
		      const struct alloy_config *cfg)
{
	char path[PATH_MAX];
	FILE *f;
	uint8_t i;

	if (state_path(drv, path, sizeof(path), 1))
		return -1;

	f = fopen(path, "we");
	if (!f)
		return -1;

	fprintf(f, "# alloyctl baseline for %s\n", drv->name);
	fprintf(f, "dpi_count=%u\n", cfg->dpi_count);
	fprintf(f, "dpi_active=%u\n", cfg->dpi_active);
	for (i = 0; i < cfg->dpi_count; i++)
		fprintf(f, "dpi%u=%u:%u\n", i, cfg->dpi[i][0], cfg->dpi[i][1]);
	fprintf(f, "polling_hz=%u\n", cfg->polling_hz);
	for (i = 0; i < drv->num_zones; i++)
		fprintf(f, "zone%u=%02x%02x%02x\n", i, cfg->zone_color[i].r,
			cfg->zone_color[i].g, cfg->zone_color[i].b);
	if (drv->num_fx > 1) {
		for (i = 0; i < drv->num_zones; i++) {
			fprintf(f, "zone_fx%u=%u\n", i, cfg->zone_fx[i]);
			fprintf(f, "zone_freq%u=%u\n", i, cfg->zone_fx_freq[i]);
			fprintf(f, "zone_speed%u=%u\n", i,
				cfg->zone_fx_speed[i]);
		}
	}
	if (drv->caps & ALLOY_CAP_FX_REACTIVE) {
		if (cfg->reactive_enabled)
			fprintf(f, "reactive=%02x%02x%02x\n",
				cfg->reactive_color.r, cfg->reactive_color.g,
				cfg->reactive_color.b);
		else
			fprintf(f, "reactive=off\n");
	}
	if (drv->caps & ALLOY_CAP_FX_STARTUP)
		fprintf(f, "startup_fx=%u\n", cfg->startup_fx);
	fprintf(f, "brightness=%u\n", cfg->brightness);
	for (i = 0; i < drv->num_buttons; i++)
		fprintf(f, "button%u=%s:%u\n", i,
			action_type_name(cfg->buttons[i].type),
			cfg->buttons[i].value);
	fprintf(f, "acceleration=%d\n", cfg->acceleration);
	fprintf(f, "deceleration=%d\n", cfg->deceleration);
	fprintf(f, "angle_snapping=%u\n", cfg->angle_snapping);
	fprintf(f, "accel_enabled=%u\n", cfg->accel_enabled);

	fclose(f);
	return 0;
}
