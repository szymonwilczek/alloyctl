/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Mouse-specific driver definitions and configuration.
 */
#ifndef ALLOY_MOUSE_DRIVER_H
#define ALLOY_MOUSE_DRIVER_H

#include "alloy.h"

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

/* Mouse capability flags */
#define ALLOY_CAP_ACCELERATION (1u << 0)
#define ALLOY_CAP_DECELERATION (1u << 1)
#define ALLOY_CAP_ANGLE_SNAPPING (1u << 2)
#define ALLOY_CAP_BATTERY (1u << 9)
#define ALLOY_CAP_HIGH_EFFICIENCY (1u << 10)
#define ALLOY_CAP_PAIRING (1u << 11)

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

/* Mouse-specific configuration */
struct alloy_config_mouse {
	/* DPI presets, X/Y pairs; count in [1, dpi.max_presets] */
	uint16_t dpi[ALLOY_MAX_DPI_PRESETS][2];
	uint8_t dpi_count;
	uint8_t dpi_active; /* 0-based index of active preset */

	struct alloy_action buttons[ALLOY_MAX_BUTTONS];

	/* only meaningful with ALLOY_CAP_FX_REACTIVE */
	uint8_t reactive_enabled;
	struct alloy_rgb reactive_color;

	/* only meaningful with ALLOY_CAP_FX_STARTUP */
	uint8_t startup_fx; /* enum alloy_startup_fx */

	/* only meaningful with ALLOY_CAP_HIGH_EFFICIENCY; 0 = off, 1 = on */
	uint8_t high_efficiency;

	/* Host-side pointer transform */
	int8_t acceleration; /* 0..100 */
	int8_t deceleration; /* 0..100 */
	uint8_t angle_snapping; /* 0 = off, else degrees 1..45 */
	uint8_t accel_enabled;
};

struct alloy_driver;
struct alloy_config;
struct alloy_cli_option;

extern const struct alloy_cli_option alloy_mouse_cli_options[];
extern const size_t alloy_num_mouse_cli_options;

void alloy_config_mouse_defaults(const struct alloy_driver *drv,
				 struct alloy_config *cfg);

#endif /* ALLOY_MOUSE_DRIVER_H */
