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
