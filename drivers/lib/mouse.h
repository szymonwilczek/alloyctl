/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Shared front-end and settings for pointing devices.
 *
 * Driver-side code layered on drivers/lib/devcfg.h:
 * it knows what CPI levels, remappable buttons, a pointer transform
 * and a wireless battery are, and expresses them as generic controls.
 *
 * A mouse driver opts in by embedding struct alloy_mouse_config
 * and publishing a struct alloy_mouse_info.
 *
 * None of it is mandatory.
 *
 * A pointing device that works nothing like this writes its own front-end
 * and ignores this header.
 */
#ifndef ALLOY_LIB_MOUSE_H
#define ALLOY_LIB_MOUSE_H

#include "lib/devcfg.h"
#include "lib/light.h"

/* Capabilities this layer claims */
#define ALLOY_MOUSE_CAP(n) (1ull << (ALLOY_CAP_MOUSE_BASE + (n)))

#define ALLOY_CAP_ACCELERATION ALLOY_MOUSE_CAP(0)
#define ALLOY_CAP_ACCEL ALLOY_CAP_ACCELERATION
#define ALLOY_CAP_DECELERATION ALLOY_MOUSE_CAP(1)
#define ALLOY_CAP_DECEL ALLOY_CAP_DECELERATION
#define ALLOY_CAP_ANGLE_SNAPPING ALLOY_MOUSE_CAP(2)
#define ALLOY_CAP_BATTERY ALLOY_MOUSE_CAP(3)
#define ALLOY_CAP_HIGH_EFFICIENCY ALLOY_MOUSE_CAP(4)
#define ALLOY_CAP_PAIRING ALLOY_MOUSE_CAP(5)
#define ALLOY_CAP_DPI ALLOY_MOUSE_CAP(6)
#define ALLOY_CAP_BUTTONS ALLOY_MOUSE_CAP(7)

/* Apply-step names this layer pushes */
#define ALLOY_STEP_DPI "dpi"
#define ALLOY_STEP_BUTTONS "buttons"
#define ALLOY_STEP_SLEEP "sleep"
#define ALLOY_STEP_HIGH_EFFICIENCY "high-efficiency"

enum alloy_action_type {
	ALLOY_ACT_DISABLED,
	ALLOY_ACT_MOUSE, /* value: mouse button number (1-based) */
	ALLOY_ACT_DPI_CYCLE,
	ALLOY_ACT_SCROLL_UP,
	ALLOY_ACT_SCROLL_DOWN,
	ALLOY_ACT_KEYBOARD, /* value: USB HID keyboard usage ID */
	ALLOY_ACT_MEDIA, /* value: vendor multimedia code */
};

struct alloy_action {
	enum alloy_action_type type;
	uint16_t value;
};

struct alloy_button {
	const char *name; /* e.g. "Button 6 (CPI)" */
	struct alloy_action def; /* factory mapping */
};

/* ops->pair sentinel */
#define ALLOY_PAIR_UNIMPLEMENTED 1

/* Wireless power knobs */
#define ALLOY_SLEEP_MIN 0
#define ALLOY_SLEEP_MAX 20
#define ALLOY_SLEEP_STEP 1
#define ALLOY_SLEEP_MIN_DEFAULT 5
#define ALLOY_ILLUM_DIM_MAX 1200
#define ALLOY_ILLUM_DIM_STEP 15

/* Power-up lighting (ALLOY_CAP_FX_STARTUP) */
enum alloy_startup_fx {
	ALLOY_STARTUP_OFF,
	ALLOY_STARTUP_REACTIVE,
	ALLOY_STARTUP_RAINBOW,
	ALLOY_STARTUP_REACTIVE_RAINBOW,
};

/*
 * A mouse driver's configuration starts with this;
 * anything peculiar to the device follows it in the driver's own struct.
 */
struct alloy_mouse_config {
	struct alloy_devcfg dev; /* must be first */

	/* CPI presets, X/Y pairs; count in [1, dpi.max_presets] */
	uint16_t dpi[ALLOY_MAX_DPI_PRESETS][2];
	uint8_t dpi_count;
	uint8_t dpi_active; /* 0-based index of the active preset */

	struct alloy_action buttons[ALLOY_MAX_BUTTONS];

	/* only meaningful with ALLOY_CAP_FX_REACTIVE */
	uint8_t reactive_enabled;
	struct alloy_rgb reactive_color;

	/* only meaningful with ALLOY_CAP_FX_STARTUP */
	uint8_t startup_fx; /* enum alloy_startup_fx */

	/* only meaningful with ALLOY_CAP_HIGH_EFFICIENCY */
	uint8_t high_efficiency;

	/* wireless power knobs; inert on wired mice */
	uint8_t illum_smart; /* blank the LEDs while the mouse is moving */
	uint16_t illum_dim_s; /* dim the LEDs after N s idle; 0 = off */
	uint8_t sleep_min; /* sleep after N min idle; 0 = never */

	/* host-side pointer transform */
	int8_t acceleration; /* 0..100 */
	int8_t deceleration; /* 0..100 */
	uint8_t angle_snapping; /* 0 = off, else degrees 1..45 */
	uint8_t accel_enabled;
};

static inline struct alloy_mouse_config *
alloy_mouse_cfg(struct alloy_config *cfg)
{
	return (struct alloy_mouse_config *)alloy_config_data(cfg);
}

static inline const struct alloy_mouse_config *
alloy_mouse_cfg_c(const struct alloy_config *cfg)
{
	return (const struct alloy_mouse_config *)alloy_config_data_c(cfg);
}

/* pointed to by struct alloy_devinfo.ext */
struct alloy_mouse_info {
	struct {
		uint16_t min;
		uint16_t max;
		uint16_t step;
		uint8_t max_presets;
	} dpi;

	const struct alloy_button *buttons;
	uint8_t num_buttons;

	/*
	 * Wireless mice:
	 * HID product id this mouse enumerates as over Bluetooth,
	 * used purely to light the connection indicator.
	 */
	uint16_t bt_product_id;

	/*
	 * Set when the transport only binds while the device is genuinely
	 * connected, so there is no "receiver present but mouse asleep" state
	 * to wait out before talking to it.
	 */
	uint8_t link_implicit;

	/*
	 * Optional (ALLOY_CAP_BATTERY): read the battery gauge.
	 * Fills *percent (0-100) and *charging, returns 0 on success.
	 * Negative when the device reports no valid level - a receiver whose
	 * mouse is asleep answers with an idle marker, not a charge.
	 */
	int (*battery)(struct alloy_device *dev, int *percent, int *charging);

	/* Optional (ALLOY_CAP_PAIRING): bind a new mouse to the receiver */
	int (*pair)(struct alloy_device *dev);
};

static inline const struct alloy_mouse_info *
alloy_mouse_info(const struct alloy_driver *drv)
{
	const struct alloy_devinfo *info = alloy_devinfo(drv);

	return info ? (const struct alloy_mouse_info *)info->ext : NULL;
}

/* the shared front-end; a driver may use it as-is or inherit from it */
extern const struct alloy_ui_desc alloy_mouse_ui;

/* factory defaults, persistence and command-line flags for the block above */
void alloy_mouse_defaults(const struct alloy_driver *drv,
			  struct alloy_config *cfg);
void alloy_mouse_state_save(const struct alloy_driver *drv,
			    const struct alloy_config *cfg, void *ctx,
			    alloy_state_emit_fn emit);
int alloy_mouse_state_load(const struct alloy_driver *drv,
			   struct alloy_config *cfg, const char *key,
			   const char *val);
void alloy_mouse_state_done(const struct alloy_driver *drv,
			    struct alloy_config *cfg);

/* flag group this layer contributes; see devcfg.h for how to splice it in */
extern const struct alloy_cli_option alloy_mouse_cli_options[];
#define ALLOY_MOUSE_CLI_COUNT 5

#endif /* ALLOY_LIB_MOUSE_H */
