/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Mouse driver defaults and configuration utilities.
 */
#include <string.h>

#include "driver.h"
#include "mouse_driver.h"

void alloy_config_mouse_defaults(const struct alloy_driver *drv,
				 struct alloy_config *cfg)
{
	uint8_t i;

	memset(cfg, 0, sizeof(*cfg));
	alloy_config_common_defaults(drv, &cfg->common);

	/* one preset out of the box (800 CPI) */
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

static int parse_bool_val(const char *str, uint8_t *out)
{
	if (!str || !*str)
		return -1;
	if (!strcasecmp(str, "1") || !strcasecmp(str, "on") ||
	    !strcasecmp(str, "true") || !strcasecmp(str, "yes")) {
		*out = 1;
		return 0;
	}
	if (!strcasecmp(str, "0") || !strcasecmp(str, "off") ||
	    !strcasecmp(str, "false") || !strcasecmp(str, "no")) {
		*out = 0;
		return 0;
	}
	return -1;
}

static int opt_parse_dpi(const char *arg, struct alloy_config *cfg,
			 char *err_buf, size_t err_len)
{
	int cpi;
	if (!arg || !parse_int_val(arg, 1, 65535, &cpi)) {
		snprintf(err_buf, err_len, "invalid CPI '%s'", arg ? arg : "");
		return -1;
	}
	for (uint8_t i = 0; i < ALLOY_MAX_DPI_PRESETS; i++) {
		cfg->mouse.dpi[i][0] = (uint16_t)cpi;
		cfg->mouse.dpi[i][1] = (uint16_t)cpi;
	}
	return 0;
}

static int opt_validate_dpi(const struct alloy_driver *drv,
			    const struct alloy_config *cfg, char *err_buf,
			    size_t err_len)
{
	uint16_t cpi = cfg->mouse.dpi[0][0];
	if (cpi < drv->dpi.min || cpi > drv->dpi.max) {
		snprintf(err_buf, err_len,
			 "CPI %u out of range [%u, %u] for device '%s'", cpi,
			 drv->dpi.min, drv->dpi.max, drv->name);
		return -1;
	}
	return 0;
}

static int opt_apply_dpi(struct alloy_device *dev,
			 const struct alloy_config *cfg)
{
	if (dev->drv->ops && dev->drv->ops->apply_dpi)
		return dev->drv->ops->apply_dpi(dev, cfg);
	return 0;
}

static int opt_parse_accel(const char *arg, struct alloy_config *cfg,
			   char *err_buf, size_t err_len)
{
	int val;
	if (!arg || !parse_int_val(arg, 0, 100, &val)) {
		snprintf(err_buf, err_len,
			 "invalid acceleration '%s'; expected 0-100",
			 arg ? arg : "");
		return -1;
	}
	cfg->mouse.acceleration = (int8_t)val;
	cfg->mouse.accel_enabled = 1;
	return 0;
}

static int opt_parse_decel(const char *arg, struct alloy_config *cfg,
			   char *err_buf, size_t err_len)
{
	int val;
	if (!arg || !parse_int_val(arg, 0, 100, &val)) {
		snprintf(err_buf, err_len,
			 "invalid deceleration '%s'; expected 0-100",
			 arg ? arg : "");
		return -1;
	}
	cfg->mouse.deceleration = (int8_t)val;
	cfg->mouse.accel_enabled = 1;
	return 0;
}

static int opt_parse_snap(const char *arg, struct alloy_config *cfg,
			  char *err_buf, size_t err_len)
{
	int val;
	if (!arg || !parse_int_val(arg, 0, 45, &val)) {
		snprintf(err_buf, err_len,
			 "invalid angle snapping '%s'; expected 0-45",
			 arg ? arg : "");
		return -1;
	}
	cfg->mouse.angle_snapping = (uint8_t)val;
	cfg->mouse.accel_enabled = 1;
	return 0;
}

static int opt_parse_high_efficiency(const char *arg, struct alloy_config *cfg,
				     char *err_buf, size_t err_len)
{
	uint8_t bool_val = 1;
	(void)err_buf;
	(void)err_len;

	if (arg && !parse_bool_val(arg, &bool_val)) {
		/* parsed successfully */
	}
	cfg->mouse.high_efficiency = bool_val;
	return 0;
}

static int opt_apply_high_efficiency(struct alloy_device *dev,
				     const struct alloy_config *cfg)
{
	if (dev->drv->ops && dev->drv->ops->apply_high_efficiency)
		return dev->drv->ops->apply_high_efficiency(dev, cfg);
	return 0;
}

const struct alloy_cli_option alloy_mouse_cli_options[] = {
	{
		.name = "--dpi",
		.alias = "--cpi",
		.arg_desc = "<cpi>",
		.help = "Set primary sensor CPI level",
		.category = ALLOY_OPT_MOUSE,
		.has_arg = 1,
		.parse = opt_parse_dpi,
		.validate = opt_validate_dpi,
		.apply = opt_apply_dpi,
	},
	{
		.name = "--accel",
		.arg_desc = "<0-100>",
		.help = "Set pointer acceleration intensity",
		.category = ALLOY_OPT_MOUSE,
		.has_arg = 1,
		.parse = opt_parse_accel,
	},
	{
		.name = "--decel",
		.arg_desc = "<0-100>",
		.help = "Set pointer deceleration intensity",
		.category = ALLOY_OPT_MOUSE,
		.has_arg = 1,
		.parse = opt_parse_decel,
	},
	{
		.name = "--snap",
		.arg_desc = "<0-45>",
		.help = "Set angle snapping threshold in degrees",
		.category = ALLOY_OPT_MOUSE,
		.has_arg = 1,
		.parse = opt_parse_snap,
	},
	{
		.name = "--high-efficiency",
		.arg_desc = "[on|off]",
		.help = "Toggle wireless high-efficiency mode",
		.category = ALLOY_OPT_MOUSE,
		.required_cap = ALLOY_CAP_HIGH_EFFICIENCY,
		.has_arg = 2,
		.parse = opt_parse_high_efficiency,
		.apply = opt_apply_high_efficiency,
	},
};

const size_t alloy_num_mouse_cli_options =
	ALLOY_ARRAY_SIZE(alloy_mouse_cli_options);
