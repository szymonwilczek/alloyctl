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
/*
 * Acceleration / deceleration / angle snapping are host-side features applied
 * by the accel daemon for every device, so these three bits are no longer
 * consulted; Kept only so the flag values stay stable.
 * TODO: Cut it out in the near future
 */
#define ALLOY_CAP_ACCELERATION (1u << 0)
#define ALLOY_CAP_DECELERATION (1u << 1)
#define ALLOY_CAP_ANGLE_SNAPPING (1u << 2)
#define ALLOY_CAP_BRIGHTNESS (1u << 3)
#define ALLOY_CAP_FIRMWARE_VERSION (1u << 4)

/* Lighting effects the hardware can run on its own */
#define ALLOY_CAP_FX_RAINBOW (1u << 5) /* per-zone rainbow cycle */
#define ALLOY_CAP_FX_REACTIVE (1u << 6) /* flash color on click */
#define ALLOY_CAP_FX_STARTUP (1u << 7) /* power-up lighting choice */
#define ALLOY_CAP_FX_GLOBAL (1u << 8) /* one effect device-wide only */

/*
 * Wireless devices carry a rechargeable pack and report its charge through ops->battery.
 * This is the marker of the wireless driver family.
 * Wired mice leave it clear.
 */
#define ALLOY_CAP_BATTERY (1u << 9)

/*
 * Wireless power-saver toggle:
 * Firmware trades runtime features for battery life.
 * Driven through ops->apply_high_efficiency.
 * Only meaningful alongside ALLOY_CAP_BATTERY.
 * Wired mice leave it clear.
 */
#define ALLOY_CAP_HIGH_EFFICIENCY (1u << 10)

/*
 * Per-zone effect rate knobs.
 * Frequency is how many cycles one period packs, speed is the tempo the
 * animation runs at; both are unitless steps the driver maps best-effort.
 */
#define ALLOY_FX_RATE_MIN 1
#define ALLOY_FX_RATE_MAX 10
#define ALLOY_FX_RATE_DEF 5

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

	/*
	 * Per-zone lighting effect as index into the driver's fx_names list;
	 * index 0 is by convention the static "steady" mode.
	 * Hardware that only runs one effect device-wide (ALLOY_CAP_FX_GLOBAL)
	 * is driven best-effort from the first zone not running steady.
	 */
	uint8_t zone_fx[ALLOY_MAX_LED_ZONES];
	uint8_t zone_fx_freq[ALLOY_MAX_LED_ZONES]; /* ALLOY_FX_RATE_* */
	uint8_t zone_fx_speed[ALLOY_MAX_LED_ZONES]; /* ALLOY_FX_RATE_* */

	uint8_t brightness; /* 0-100 */

	/* only meaningful with ALLOY_CAP_FX_REACTIVE */
	uint8_t reactive_enabled;
	struct alloy_rgb reactive_color;

	/* only meaningful with ALLOY_CAP_FX_STARTUP */
	uint8_t startup_fx; /* enum alloy_startup_fx */

	/* only meaningful with ALLOY_CAP_HIGH_EFFICIENCY; 0 = off, 1 = on */
	uint8_t high_efficiency;

	struct alloy_action buttons[ALLOY_MAX_BUTTONS];

	/*
	 * Host-side pointer transform (acceleration/deceleration/angle snapping)
	 * applied by the accel daemon, not by the device -
	 * these are always meaningful, independent of any ALLOY_CAP_* bit.
	 * accel_enabled is the persisted "engine on" intent.
	 */
	int8_t acceleration; /* 0..100 */
	int8_t deceleration; /* 0..100 */
	uint8_t angle_snapping; /* 0 = off, else degrees 1..45 */
	uint8_t accel_enabled;
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

	/*
	 * Optional (ALLOY_CAP_HIGH_EFFICIENCY):
	 * drive the wireless power-saver toggle from cfg->high_efficiency.
	 * Mode is device-defined bundle, so enabling it may also force other
	 * registers (polling, brightness).
	 * Disabling restores them from cfg.
	 * Wired mice leave this NULL.
	 */
	int (*apply_high_efficiency)(struct alloy_device *dev,
				     const struct alloy_config *cfg);

	/* commit live configuration to onboard flash */
	int (*save)(struct alloy_device *dev);

	/* optional: NUL-terminated firmware version string */
	int (*firmware_version)(struct alloy_device *dev, char *buf,
				size_t len);

	/*
	 * Optional (wireless devices, ALLOY_CAP_BATTERY):
	 * Read the battery gauge.
	 * Fills *percent (0-100) and *charging (0 or 1) and returns 0 on success.
	 * Negative when the device reports no valid level - e.g 2.4 GHz receiver
	 * whose mouse is asleep or not linked answers with an idle marker, not a charge.
	 */
	int (*battery)(struct alloy_device *dev, int *percent, int *charging);

	/*
	 * Optional:
	 * parse one unsolicited report from the driver's event interface
	 * (see alloy_driver.event_interface).
	 * Returns 1 when cfg was updated to reflect a device-initiated change
	 * (e.g. the hardware CPI button switching the active level),
	 * 0 when the report is not recognized event.
	 */
	int (*parse_event)(const uint8_t *buf, size_t len,
			   struct alloy_config *cfg);
};

struct alloy_driver {
	const char *name;
	uint16_t vendor_id;
	uint16_t product_id;
	int interface; /* USB interface carrying config reports */
	/*
	 * USB interface streaming unsolicited device events;
	 * only consulted when ops->parse_event is set.
	 */
	int event_interface;

	/* Vendor report payload size;
	 * 0 selects the 64-byte default */
	uint16_t report_size;

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
	 * Lighting effects selectable per zone:
	 * display names, index 0 being the static/steady mode.
	 * Driver maps the index to its wire encoding; hardware running one
	 * effect device-wide (ALLOY_CAP_FX_GLOBAL) applies the selection
	 * best-effort.
	 */
	const char *const *fx_names;
	uint8_t num_fx;

	/*
	 * Optional ASCII art of the mouse, drawn in the center pane.
	 * Art should be provided in `drivers/<driver>/<driver>_art.txt`
	 * and injected via the auto-generated `build/art_<driver>.h` header.
	 * If no custom art is provided, include `build/default_art.h` and use
	 * `alloy_default_mouse_art` as the fallback.
	 * Keep every line at most 40 rendered columns wide, please.
	 *
	 * Prefix a character with "$N" (N = 1..8) to paint it in the
	 * live color of zone N-1; "$$" renders a literal dollar.
	 * Markers take no column.
	 * Marker naming zone the device lacks renders its character unpainted.
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
	/* unsolicited-event channel;
	 * fd < 0 when the driver has none */
	struct alloy_hid_dev ev;
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

/*
 * Scan the registry against connected hardware and collect every supported
 * device currently plugged in.
 * Fills out[] with up to max driver pointers (in registry order) and returns
 * the total number found, which may exceed max.
 * Pass out=NULL to only count.
 */
int alloy_device_enumerate(const struct alloy_driver **out, int max);

/* Open a specific device by USB id (e.g. from --device) */
int alloy_device_open_id(struct alloy_device *dev, uint16_t vendor_id,
			 uint16_t product_id);
void alloy_device_close(struct alloy_device *dev);

/* Generic default-config helper usable by most drivers */
void alloy_config_generic_defaults(const struct alloy_driver *drv,
				   struct alloy_config *cfg);

#endif /* ALLOY_DRIVER_H */
