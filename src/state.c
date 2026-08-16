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

static void parse_line(const struct alloy_driver *drv, struct alloy_config *cfg,
		       const char *key, const char *val)
{
	unsigned idx;
	unsigned a;
	unsigned b;
	unsigned rgb;

	/* Common keys */
	if (!strcmp(key, "polling_hz")) {
		cfg->common.polling_hz = (uint16_t)atoi(val);
	} else if (!strcmp(key, "brightness")) {
		cfg->common.brightness =
			(uint8_t)ALLOY_CLAMP(atoi(val), 0, 100);
	} else if (sscanf(key, "zone%u", &idx) == 1 &&
		   idx < ALLOY_MAX_LED_ZONES) {
		if (sscanf(val, "%x", &rgb) == 1) {
			cfg->common.zone_color[idx].r = (rgb >> 16) & 0xFF;
			cfg->common.zone_color[idx].g = (rgb >> 8) & 0xFF;
			cfg->common.zone_color[idx].b = rgb & 0xFF;
		}
	} else if (sscanf(key, "zone_fx%u", &idx) == 1 &&
		   idx < ALLOY_MAX_LED_ZONES) {
		if (!strcmp(val, "rainbow"))
			cfg->common.zone_fx[idx] = 1;
		else if (!strcmp(val, "static"))
			cfg->common.zone_fx[idx] = 0;
		else
			cfg->common.zone_fx[idx] =
				(uint8_t)ALLOY_CLAMP(atoi(val), 0, 255);
	} else if (sscanf(key, "zone_freq%u", &idx) == 1 &&
		   idx < ALLOY_MAX_LED_ZONES) {
		cfg->common.zone_fx_freq[idx] = (uint8_t)ALLOY_CLAMP(
			atoi(val), ALLOY_FX_RATE_MIN, ALLOY_FX_RATE_MAX);
	} else if (sscanf(key, "zone_speed%u", &idx) == 1 &&
		   idx < ALLOY_MAX_LED_ZONES) {
		cfg->common.zone_fx_speed[idx] = (uint8_t)ALLOY_CLAMP(
			atoi(val), ALLOY_FX_RATE_MIN, ALLOY_FX_RATE_MAX);
	} else if (sscanf(key, "zone_multi%u", &idx) == 1 &&
		   idx < ALLOY_MAX_LED_ZONES) {
		cfg->common.zone_fx_multicolor[idx] = atoi(val) ? 1 : 0;
	} else if (sscanf(key, "zone_dir%u", &idx) == 1 &&
		   idx < ALLOY_MAX_LED_ZONES) {
		cfg->common.zone_fx_direction[idx] = (uint8_t)atoi(val);
	} else if (sscanf(key, "zone_custom%u", &idx) == 1 &&
		   idx < ALLOY_MAX_LED_ZONES) {
		cfg->common.zone_fx_custom[idx] = (uint8_t)atoi(val);
	} else if (!strcmp(key, "fx")) {
		for (idx = 0; idx < ALLOY_MAX_LED_ZONES; idx++)
			cfg->common.zone_fx[idx] =
				(uint8_t)ALLOY_CLAMP(atoi(val), 0, 255);
	} else if (!strcmp(key, "illum_smart")) {
		cfg->common.illum_smart = atoi(val) ? 1 : 0;
	} else if (!strcmp(key, "illum_dim_s")) {
		cfg->common.illum_dim_s = (uint16_t)ALLOY_CLAMP(
			atoi(val), 0, ALLOY_ILLUM_DIM_MAX);
	} else if (!strcmp(key, "sleep_min")) {
		cfg->common.sleep_min =
			(uint8_t)ALLOY_CLAMP(atoi(val), 0, ALLOY_SLEEP_MAX);
	}

	/* Mouse-specific keys */
	if (alloy_driver_is_mouse(drv)) {
		if (!strcmp(key, "dpi_count")) {
			cfg->mouse.dpi_count = (uint8_t)ALLOY_CLAMP(
				atoi(val), 1, ALLOY_MAX_DPI_PRESETS);
		} else if (!strcmp(key, "dpi_active")) {
			cfg->mouse.dpi_active = (uint8_t)ALLOY_CLAMP(
				atoi(val), 0, ALLOY_MAX_DPI_PRESETS - 1);
		} else if (sscanf(key, "dpi%u", &idx) == 1 &&
			   idx < ALLOY_MAX_DPI_PRESETS) {
			if (sscanf(val, "%u:%u", &a, &b) == 2) {
				cfg->mouse.dpi[idx][0] = (uint16_t)a;
				cfg->mouse.dpi[idx][1] = (uint16_t)b;
			}
		} else if (!strcmp(key, "reactive")) {
			if (sscanf(val, "%x", &rgb) == 1) {
				cfg->mouse.reactive_enabled = 1;
				cfg->mouse.reactive_color.r = (rgb >> 16) &
							      0xFF;
				cfg->mouse.reactive_color.g = (rgb >> 8) & 0xFF;
				cfg->mouse.reactive_color.b = rgb & 0xFF;
			} else {
				cfg->mouse.reactive_enabled = 0;
			}
		} else if (!strcmp(key, "startup_fx")) {
			cfg->mouse.startup_fx = (uint8_t)ALLOY_CLAMP(
				atoi(val), 0, ALLOY_STARTUP_REACTIVE_RAINBOW);
		} else if (!strcmp(key, "high_efficiency")) {
			cfg->mouse.high_efficiency = atoi(val) ? 1 : 0;
		} else if (sscanf(key, "button%u", &idx) == 1 &&
			   idx < ALLOY_MAX_BUTTONS) {
			parse_action(val, &cfg->mouse.buttons[idx]);
		} else if (!strcmp(key, "acceleration")) {
			cfg->mouse.acceleration = (int8_t)atoi(val);
		} else if (!strcmp(key, "deceleration")) {
			cfg->mouse.deceleration = (int8_t)atoi(val);
		} else if (!strcmp(key, "angle_snapping")) {
			cfg->mouse.angle_snapping = (uint8_t)atoi(val);
		} else if (!strcmp(key, "accel_enabled")) {
			cfg->mouse.accel_enabled = atoi(val) ? 1 : 0;
		}
	}

	/* Keyboard-specific keys */
	if (alloy_driver_is_keyboard(drv)) {
		if (!strcmp(key, "win_lock") || !strcmp(key, "meta_lock")) {
			cfg->kbd.win_lock = atoi(val) ? 1 : 0;
		} else if (!strcmp(key, "snap_tap")) {
			cfg->kbd.snap_tap = atoi(val) ? 1 : 0;
		} else if (!strcmp(key, "snap_tap_groups") ||
			   !strcmp(key, "snap_tap_group_count")) {
			cfg->kbd.snap_tap_group_count = (uint8_t)ALLOY_CLAMP(
				atoi(val), 1, ALLOY_MAX_SNAP_TAP_GROUPS);
		} else if (sscanf(key, "snap_tap%u", &idx) == 1 &&
			   idx < ALLOY_MAX_SNAP_TAP_GROUPS) {
			unsigned mode = 0, k1 = 0x04, k2 = 0x07;
			if (sscanf(val, "%u:%u:%u", &mode, &k1, &k2) == 3 ||
			    sscanf(val, "%u:%x:%x", &mode, &k1, &k2) == 3) {
				cfg->kbd.snap_tap_groups[idx].mode =
					(uint8_t)mode;
				cfg->kbd.snap_tap_groups[idx].key1 =
					(uint8_t)k1;
				cfg->kbd.snap_tap_groups[idx].key2 =
					(uint8_t)k2;
				if (idx >= cfg->kbd.snap_tap_group_count)
					cfg->kbd.snap_tap_group_count = idx + 1;
			}
		} else if (!strcmp(key, "profile_active") ||
			   !strcmp(key, "profile")) {
			cfg->kbd.profile_active =
				(uint8_t)ALLOY_CLAMP(atoi(val), 1, 3);
		}
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
		parse_line(drv, cfg, line, eq + 1);
	}
	fclose(f);

	/* edited file may point the active preset past the count (for mice) */
	if (alloy_driver_is_mouse(drv) &&
	    cfg->mouse.dpi_active >= cfg->mouse.dpi_count)
		cfg->mouse.dpi_active = (uint8_t)(cfg->mouse.dpi_count - 1);
	return 0;
}

static void state_store_common(FILE *f, const struct alloy_driver *drv,
			       const struct alloy_config_common *common)
{
	uint8_t i;

	fprintf(f, "polling_hz=%u\n", common->polling_hz);
	for (i = 0; i < drv->num_zones; i++)
		fprintf(f, "zone%u=%02x%02x%02x\n", i, common->zone_color[i].r,
			common->zone_color[i].g, common->zone_color[i].b);
	if (drv->num_fx > 1) {
		for (i = 0; i < drv->num_zones; i++) {
			fprintf(f, "zone_fx%u=%u\n", i, common->zone_fx[i]);
			fprintf(f, "zone_freq%u=%u\n", i,
				common->zone_fx_freq[i]);
			fprintf(f, "zone_speed%u=%u\n", i,
				common->zone_fx_speed[i]);
			if (drv->caps & ALLOY_CAP_MULTICOLOR)
				fprintf(f, "zone_multi%u=%u\n", i,
					common->zone_fx_multicolor[i]);
			if (drv->caps & ALLOY_CAP_DIRECTION)
				fprintf(f, "zone_dir%u=%u\n", i,
					common->zone_fx_direction[i]);
		}
	}
	fprintf(f, "brightness=%u\n", common->brightness);
	if (drv->caps & ALLOY_CAP_BATTERY) {
		fprintf(f, "illum_smart=%u\n", common->illum_smart ? 1 : 0);
		fprintf(f, "illum_dim_s=%u\n", common->illum_dim_s);
		fprintf(f, "sleep_min=%u\n", common->sleep_min);
	}
}

static void state_store_mouse(FILE *f, const struct alloy_driver *drv,
			      const struct alloy_config_mouse *mouse)
{
	uint8_t i;

	fprintf(f, "dpi_count=%u\n", mouse->dpi_count);
	fprintf(f, "dpi_active=%u\n", mouse->dpi_active);
	for (i = 0; i < mouse->dpi_count; i++)
		fprintf(f, "dpi%u=%u:%u\n", i, mouse->dpi[i][0],
			mouse->dpi[i][1]);

	if (drv->caps & ALLOY_CAP_FX_REACTIVE) {
		if (mouse->reactive_enabled)
			fprintf(f, "reactive=%02x%02x%02x\n",
				mouse->reactive_color.r,
				mouse->reactive_color.g,
				mouse->reactive_color.b);
		else
			fprintf(f, "reactive=off\n");
	}
	if (drv->caps & ALLOY_CAP_FX_STARTUP)
		fprintf(f, "startup_fx=%u\n", mouse->startup_fx);
	if (drv->caps & ALLOY_CAP_HIGH_EFFICIENCY)
		fprintf(f, "high_efficiency=%u\n",
			mouse->high_efficiency ? 1 : 0);

	for (i = 0; i < drv->num_buttons; i++)
		fprintf(f, "button%u=%s:%u\n", i,
			action_type_name(mouse->buttons[i].type),
			mouse->buttons[i].value);

	fprintf(f, "acceleration=%d\n", mouse->acceleration);
	fprintf(f, "deceleration=%d\n", mouse->deceleration);
	fprintf(f, "angle_snapping=%u\n", mouse->angle_snapping);
	fprintf(f, "accel_enabled=%u\n", mouse->accel_enabled);
}

static void state_store_keyboard(FILE *f, const struct alloy_driver *drv,
				 const struct alloy_config_keyboard *kbd)
{
	uint8_t i;

	if (drv->caps & ALLOY_CAP_WIN_LOCK)
		fprintf(f, "win_lock=%u\n", kbd->win_lock ? 1 : 0);
	if (drv->caps & ALLOY_CAP_SNAP_TAP) {
		fprintf(f, "snap_tap=%u\n", kbd->snap_tap ? 1 : 0);
		fprintf(f, "snap_tap_groups=%u\n", kbd->snap_tap_group_count);
		for (i = 0; i < kbd->snap_tap_group_count &&
			    i < ALLOY_MAX_SNAP_TAP_GROUPS;
		     i++) {
			fprintf(f, "snap_tap%u=%u:%02x:%02x\n", i,
				kbd->snap_tap_groups[i].mode,
				kbd->snap_tap_groups[i].key1,
				kbd->snap_tap_groups[i].key2);
		}
	}
	if (drv->caps & ALLOY_CAP_PROFILE)
		fprintf(f, "profile_active=%u\n", kbd->profile_active);
}

int alloy_state_store(const struct alloy_driver *drv,
		      const struct alloy_config *cfg)
{
	char path[PATH_MAX];
	FILE *f;

	if (state_path(drv, path, sizeof(path), 1))
		return -1;

	f = fopen(path, "we");
	if (!f)
		return -1;

	fprintf(f, "# alloyctl baseline for %s\n", drv->name);
	state_store_common(f, drv, &cfg->common);

	switch (drv->type) {
	case ALLOY_DEV_KEYBOARD:
		state_store_keyboard(f, drv, &cfg->kbd);
		break;
	case ALLOY_DEV_MOUSE:
	default:
		state_store_mouse(f, drv, &cfg->mouse);
		break;
	}

	fclose(f);
	return 0;
}
