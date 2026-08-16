/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Conventional device settings, shared by the drivers that happen to want them.
 *
 * This is driver-side code.
 * Core has never heard of polling rates, LED zones or brightness;
 * they are a convention that several drivers found useful, so the code implementing
 * them lives here where it can be maintained by the people who maintain those drivers.
 *
 * Using it is entirely optional.
 *
 * A driver opts in by starting its configuration struct with a struct alloy_devcfg
 * and pointing struct alloy_driver.data at a struct alloy_devinfo describing what
 * it actually has:
 *
 *	struct mymouse_config {
 *		struct alloy_devcfg dev;
 *		uint16_t actuation_um;
 *	};
 *
 * A driver for something none of this fits declares neither and talks to the
 * core directly.
 */
#ifndef ALLOY_LIB_DEVCFG_H
#define ALLOY_LIB_DEVCFG_H

#include "driver.h"

/* Static limits of this convention, not of the core */
#define ALLOY_MAX_LED_ZONES 8
#define ALLOY_MAX_DPI_PRESETS 5
#define ALLOY_MAX_BUTTONS 16

/*
 * Per-zone effect parameter slots.
 * What slot N means is agreed between a driver and whoever reads it;
 * the slots below are the ones the shared lighting screen understands,
 * and a driver with a knob nobody thought of claims a free one.
 */
#define ALLOY_FX_PARAMS 8
#define ALLOY_FX_P_FREQ 0
#define ALLOY_FX_P_SPEED 1
#define ALLOY_FX_P_MULTICOLOR 2
#define ALLOY_FX_P_DIRECTION 3
#define ALLOY_FX_P_CUSTOM 4

/* Conventional range for unitless effect rate knobs */
#define ALLOY_FX_RATE_MIN 1
#define ALLOY_FX_RATE_MAX 10
#define ALLOY_FX_RATE_DEF 5

/*
 * Shorthand for "this driver's device has the thing the shared code draws".
 * They gate nothing in the core;
 * they exist so a driver can reuse the shared panes without writing them
 * out control by control.
 */
#define ALLOY_CAP_BRIGHTNESS (1ull << 0)
#define ALLOY_CAP_COLOR (1ull << 1)
#define ALLOY_CAP_FX_RAINBOW (1ull << 2)
#define ALLOY_CAP_FX_REACTIVE (1ull << 3)
#define ALLOY_CAP_FX_STARTUP (1ull << 4)
#define ALLOY_CAP_FX_GLOBAL (1ull << 5) /* one effect device-wide only */
#define ALLOY_CAP_FX_FREQ (1ull << 6)
#define ALLOY_CAP_FX_SPEED (1ull << 7)
#define ALLOY_CAP_POLLING (1ull << 8)

/* claimed by drivers/lib/mouse.h */
#define ALLOY_CAP_MOUSE_BASE 16
/* claimed by drivers/lib/keyboard.h */
#define ALLOY_CAP_KBD_BASE 32
/* free for a driver's own use */
#define ALLOY_CAP_DRIVER_BASE 48

struct alloy_led_zone {
	const char *name; /* e.g. "TOP" */
	struct alloy_rgb def_color; /* factory color */
};

/*
 * The settings block a driver puts first in its configuration struct.
 * Zeroed fields are simply unused;
 * Driver with no LEDs never touches the zone arrays and the shared code
 * never draws them.
 */
struct alloy_devcfg {
	uint16_t polling_hz;
	uint8_t brightness; /* 0-100 */

	struct alloy_rgb zone_color[ALLOY_MAX_LED_ZONES];
	uint8_t zone_fx[ALLOY_MAX_LED_ZONES];
	uint8_t zone_fx_param[ALLOY_MAX_LED_ZONES][ALLOY_FX_PARAMS];
};

/*
 * What a driver publishes through struct alloy_driver.data so the shared code
 * knows what to draw.
 * @ext carries whatever a more specific shared layer wants on top
 * (see mouse.h, keyboard.h).
 */
struct alloy_devinfo {
	uint64_t caps;

	const uint16_t *polling_rates; /* descending, Hz */
	uint8_t num_polling_rates;

	const struct alloy_led_zone *zones;
	uint8_t num_zones;

	/*
	 * Lighting effects selectable per zone:
	 * display names, index 0 being the static/steady mode.
	 * The driver maps the index to its wire encoding.
	 */
	const char *const *fx_names;
	uint8_t num_fx;

	/*
	 * Optional lighting hooks, for drivers whose effects need more than
	 * the conventions the shared lighting screen assumes.
	 */
	const struct alloy_light_ops *light;

	/* op table of the more specific shared layer this driver uses */
	const void *ext;
};

static inline const struct alloy_devinfo *
alloy_devinfo(const struct alloy_driver *drv)
{
	return (const struct alloy_devinfo *)drv->data;
}

static inline struct alloy_devcfg *alloy_devcfg(struct alloy_config *cfg)
{
	return (struct alloy_devcfg *)alloy_config_data(cfg);
}

static inline const struct alloy_devcfg *
alloy_devcfg_c(const struct alloy_config *cfg)
{
	return (const struct alloy_devcfg *)alloy_config_data_c(cfg);
}

/* Apply-step names the shared panes push */
#define ALLOY_STEP_POLLING "polling"
#define ALLOY_STEP_COLORS "colors"
#define ALLOY_STEP_BRIGHTNESS "brightness"

/* Factory defaults for the shared block */
void alloy_devcfg_defaults(const struct alloy_driver *drv,
			   struct alloy_config *cfg);

/* Persistence of the shared block; both return/consume its own keys only */
void alloy_devcfg_state_save(const struct alloy_driver *drv,
			     const struct alloy_config *cfg, void *ctx,
			     alloy_state_emit_fn emit);
int alloy_devcfg_state_load(const struct alloy_driver *drv,
			    struct alloy_config *cfg, const char *key,
			    const char *val);

/*
 * Command-line flags for the shared block (brightness, polling, color, fx).
 * A driver splices the group into its own list:
 *
 *	static const struct alloy_cli_table mydrv_cli[] = {
 *		{ alloy_devcfg_cli_options, ALLOY_DEVCFG_CLI_COUNT },
 *		{ mydrv_own_options, ALLOY_ARRAY_SIZE(mydrv_own_options) },
 *	};
 */
extern const struct alloy_cli_option alloy_devcfg_cli_options[];
#define ALLOY_DEVCFG_CLI_COUNT 4

#endif /* ALLOY_LIB_DEVCFG_H */
