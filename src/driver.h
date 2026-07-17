/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Driver model.
 *
 * One file under drivers/ describes one mouse:
 * its USB identity, its capabilities (DPI range, LED zones, remappable buttons,
 * polling rates) and the operations that translate device-independent struct
 * alloy_config into vendor HID packets.
 *
 * Drivers register themselves with ALLOY_DRIVER_REGISTER().
 * Registry is dedicated ELF section collected by the linker, so adding support
 * for new mouse never touches core code.
 */
#ifndef ALLOY_DRIVER_H
#define ALLOY_DRIVER_H

#include "alloy.h"
#include "hid.h"

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

struct alloy_led_zone {
	const char *name; /* e.g. "TOP" */
	struct alloy_rgb def_color; /* factory color */
};

/* Optional feature flags advertised by driver */
#define ALLOY_CAP_ACCELERATION (1u << 0)
#define ALLOY_CAP_DECELERATION (1u << 1)
#define ALLOY_CAP_ANGLE_SNAPPING (1u << 2)
#define ALLOY_CAP_BRIGHTNESS (1u << 3)
#define ALLOY_CAP_FIRMWARE_VERSION (1u << 4)

/* Lighting effects the hardware can run on its own */
#define ALLOY_CAP_FX_RAINBOW (1u << 5) /* per-zone rainbow cycle */
#define ALLOY_CAP_FX_REACTIVE (1u << 6) /* flash color on click */
#define ALLOY_CAP_FX_STARTUP (1u << 7) /* power-up lighting choice */

/* Per-zone lighting mode */
enum alloy_led_mode {
	ALLOY_LED_STATIC,
	ALLOY_LED_RAINBOW,
};

/* Power-up lighting (ALLOY_CAP_FX_STARTUP) */
enum alloy_startup_fx {
	ALLOY_STARTUP_OFF,
	ALLOY_STARTUP_REACTIVE,
	ALLOY_STARTUP_RAINBOW,
	ALLOY_STARTUP_REACTIVE_RAINBOW,
};

/*
 * Device-independent configuration.
 * TUI edits this structure and hands it to the driver ops for translation to the wire format.
 */
struct alloy_config {
	/* DPI presets, X/Y pairs; count in [1, dpi.max_presets] */
	uint16_t dpi[ALLOY_MAX_DPI_PRESETS][2];
	uint8_t dpi_count;
	uint8_t dpi_active; /* 0-based index of active preset */

	uint16_t polling_hz;

	struct alloy_rgb zone_color[ALLOY_MAX_LED_ZONES];
	uint8_t zone_mode[ALLOY_MAX_LED_ZONES]; /* enum alloy_led_mode */
	uint8_t brightness; /* 0-100 */

	/* only meaningful with ALLOY_CAP_FX_REACTIVE */
	uint8_t reactive_enabled;
	struct alloy_rgb reactive_color;

	/* only meaningful with ALLOY_CAP_FX_STARTUP */
	uint8_t startup_fx; /* enum alloy_startup_fx */

	struct alloy_action buttons[ALLOY_MAX_BUTTONS];

	/* only meaningful when the matching ALLOY_CAP_* bit is set */
	int8_t acceleration;
	int8_t deceleration;
	uint8_t angle_snapping;
};

struct alloy_device;

struct alloy_driver_ops {
	/* push one aspect of the config to the device (live change) */
	int (*apply_dpi)(struct alloy_device *dev,
			 const struct alloy_config *cfg);
	int (*apply_polling)(struct alloy_device *dev,
			     const struct alloy_config *cfg);
	int (*apply_colors)(struct alloy_device *dev,
			    const struct alloy_config *cfg);
	int (*apply_brightness)(struct alloy_device *dev,
				const struct alloy_config *cfg);
	int (*apply_buttons)(struct alloy_device *dev,
			     const struct alloy_config *cfg);

	/* commit live configuration to onboard flash */
	int (*save)(struct alloy_device *dev);

	/* optional: NUL-terminated firmware version string */
	int (*firmware_version)(struct alloy_device *dev, char *buf,
				size_t len);
};

struct alloy_driver {
	const char *name;
	uint16_t vendor_id;
	uint16_t product_id;
	int interface; /* USB interface carrying config reports */

	struct {
		uint16_t min;
		uint16_t max;
		uint16_t step;
		uint8_t max_presets;
	} dpi;

	const uint16_t *polling_rates; /* descending, Hz */
	uint8_t num_polling_rates;

	const struct alloy_led_zone *zones;
	uint8_t num_zones;

	const struct alloy_button *buttons;
	uint8_t num_buttons;

	uint32_t caps; /* ALLOY_CAP_* bits */

	/*
	 * Optional ASCII art of the mouse, drawn in the center pane.
	 * NULL selects the built-in generic art.
	 * Keep every line at most 40 columns wide, please.
	 */
	const char *ascii_art;

	const struct alloy_driver_ops *ops;

	/* fill cfg with the factory defaults of this device */
	void (*config_defaults)(const struct alloy_driver *drv,
				struct alloy_config *cfg);
};

/* opened, driver-bound device */
struct alloy_device {
	const struct alloy_driver *drv;
	struct alloy_hid_dev hid;
};

#define ALLOY_DRIVER_REGISTER(drv)                             \
	static const struct alloy_driver *__alloy_driver_##drv \
		__attribute__((used, section("alloy_drivers"))) = &(drv)

/* Registry iteration (linker-provided section bounds) */
const struct alloy_driver *const *alloy_driver_first(void);
const struct alloy_driver *const *alloy_driver_last(void);

#define alloy_for_each_driver(iter) \
	for (iter = alloy_driver_first(); iter < alloy_driver_last(); iter++)

const struct alloy_driver *alloy_driver_find(uint16_t vendor_id,
					     uint16_t product_id);

/* Scan the registry against connected hardware and open device */
int alloy_device_open(struct alloy_device *dev);
void alloy_device_close(struct alloy_device *dev);

/* Generic default-config helper usable by most drivers */
void alloy_config_generic_defaults(const struct alloy_driver *drv,
				   struct alloy_config *cfg);

#endif /* ALLOY_DRIVER_H */
