/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Driver model.
 *
 * Core knows that a driver exists, how to reach its device, and how to ask it
 * to do things by name. It knows nothing else.
 * There is no notion here of a mouse or a keyboard, of DPI, brightness, polling,
 * lighting, batteries or actuation points - not even of a "capability",
 * because naming one would mean guessing what devices can do.
 *
 * Driver declares:
 *
 *	config_size	how many bytes of settings it owns (opaque to the core)
 *	transport	how packets reach the device (hidraw by default)
 *	apply_steps[]	named operations that push part of those settings
 *	cli_options[]	command-line flags, with their own parse and apply
 *	ui		front-end description: screens, panes, controls
 *	ops		the few lifecycle hooks that mean the same for anything
 *
 * Everything a device can do is expressed through those.
 * Adding support for a device nobody anticipated is one new file under drivers/
 * and no core change at all.
 *
 * Code shared between drivers (conventional configuration layouts, a mouse
 * or keyboard front-end, a lighting screen) lives under drivers/lib/
 * and is strictly opt-in: a driver for an unheard-of device may use none of it.
 */
#ifndef ALLOY_DRIVER_H
#define ALLOY_DRIVER_H

#include "alloy.h"
#include "ui.h"

struct alloy_config;
struct alloy_device;
struct alloy_driver;

/*
 * Configuration
 * Bag of bytes the driver owns.
 * Core allocates it, copies it, compares it and asks the driver to serialize it,
 * and never looks inside.
 */

struct alloy_config *alloy_config_alloc(const struct alloy_driver *drv);
void alloy_config_free(struct alloy_config *cfg);
void alloy_config_copy(struct alloy_config *dst,
		       const struct alloy_config *src);
int alloy_config_equal(const struct alloy_config *a,
		       const struct alloy_config *b);
void *alloy_config_data(struct alloy_config *cfg);
const void *alloy_config_data_c(const struct alloy_config *cfg);
const struct alloy_driver *alloy_config_driver(const struct alloy_config *cfg);

/* Typed view of the block, for driver code */
#define ALLOY_CFG(cfg, type) ((type *)alloy_config_data(cfg))
#define ALLOY_CFG_C(cfg, type) ((const type *)alloy_config_data_c(cfg))

/* Callback handed to a driver's state writer */
typedef void (*alloy_state_emit_fn)(void *ctx, const char *key,
				    const char *val);

/*
 * How packets reach the device.
 * The default speaks Linux hidraw and is configured through a driver-supplied
 * parameter block (see hid.h).
 * Driver whose device wants something else supplies its own table and the core
 * is none the wiser.
 */
struct alloy_transport {
	const char *name;

	/* is a device this driver binds currently connected? */
	int (*present)(const struct alloy_driver *drv);
	/* bind the config channel; returns 0 on success */
	int (*open)(struct alloy_device *dev, const struct alloy_driver *drv);
	/* bind the unsolicited-event channel, if the driver has one */
	int (*open_events)(struct alloy_device *dev,
			   const struct alloy_driver *drv);
	void (*close)(struct alloy_device *dev);

	int (*write)(struct alloy_device *dev, const uint8_t *buf, size_t len);
	int (*read)(struct alloy_device *dev, uint8_t *buf, size_t len,
		    int timeout_ms);
	/* non-blocking read from the event channel */
	int (*poll_event)(struct alloy_device *dev, uint8_t *buf, size_t len);

	int (*send_feature)(struct alloy_device *dev, const uint8_t *buf,
			    size_t len);
	int (*get_feature)(struct alloy_device *dev, uint8_t *buf, size_t len);

	/*
	 * Optional: describe the device nodes this driver needs access to,
	 * for the generated udev rules.
	 * Returns the number of lines written.
	 */
	size_t (*udev_rules)(const struct alloy_driver *drv, char *buf,
			     size_t len);
};

/* The default: Linux hidraw (src/hid_transport.c) */
extern const struct alloy_transport alloy_hid_transport;

/* not pushed by the one-shot startup handshake */
#define ALLOY_APPLY_SKIP_SYNC (1u << 0)
/*
 * Never pushed in bulk, only by name - for writes whose side effects make
 * replaying them on every save a bad idea.
 */
#define ALLOY_APPLY_MANUAL (1u << 1)

/*
 * One thing the device can be told.
 * @name is the driver's own vocabulary; the front-end and the driver's own UI
 * push by name, so an aspect nobody anticipated is saved, reverted and previewed
 * without the core ever learning what it is.
 */
struct alloy_apply_step {
	const char *name;
	uint32_t flags;
	int (*fn)(struct alloy_device *dev, const struct alloy_config *cfg);
};

struct alloy_cli_option {
	const char *name; /* primary flag, e.g. "--dpi" */
	const char *alias; /* alias, e.g. "--cpi" (or NULL) */
	const char *short_name; /* e.g. "-b" (or NULL) */
	const char *arg_desc; /* e.g. "<cpi>", "[on|off]" */
	const char *help; /* short description for --help */
	int has_arg; /* 0 = none, 1 = required, 2 = optional */

	/* optional: hide the flag on devices that do not offer it */
	int (*available)(const struct alloy_driver *drv);

	int (*parse)(const struct alloy_driver *drv, const char *arg,
		     struct alloy_config *cfg, char *err_buf, size_t err_len);
	int (*validate)(const struct alloy_driver *drv,
			const struct alloy_config *cfg, char *err_buf,
			size_t err_len);
	/* push it; NULL falls back to the apply step named by @apply_step */
	int (*apply)(struct alloy_device *dev, const struct alloy_config *cfg);
	const char *apply_step;
};

/*
 * A group of flags.
 * A driver lists the tables it wants, so it can splice in a shared table from
 * drivers/lib/ and add one of its own without either having to know about
 * the other.
 */
struct alloy_cli_table {
	const struct alloy_cli_option *options;
	uint8_t count;
};

/*
 * A standalone command that runs without binding a device.
 * Registered from anywhere, including driver-library code,
 * so the core needs no built-in vocabulary for daemons, helpers or diagnostics.
 * @run returns the process exit status.
 */
struct alloy_cli_command {
	const char *name; /* e.g. "--accel-daemon" */
	const char *arg_desc;
	const char *help;
	int has_arg;
	int (*run)(const char *arg);
};

#define ALLOY_COMMAND_REGISTER(cmd)                                  \
	static const struct alloy_cli_command *__alloy_command_##cmd \
		__attribute__((used, section("alloy_commands"))) = &(cmd)

const struct alloy_cli_command *const *alloy_command_first(void);
const struct alloy_cli_command *const *alloy_command_last(void);

#define alloy_for_each_command(iter) \
	for (iter = alloy_command_first(); iter < alloy_command_last(); iter++)

/*
 * The handful of operations that mean the same thing whatever the device is.
 * All optional.
 */
struct alloy_driver_ops {
	/* fill @cfg with this device's factory defaults */
	void (*config_defaults)(const struct alloy_driver *drv,
				struct alloy_config *cfg);

	/* commit the live configuration to onboard storage */
	int (*save)(struct alloy_device *dev);

	/* read the full hardware state back into @cfg */
	int (*read_config)(struct alloy_device *dev, struct alloy_config *cfg);

	/*
	 * Parse one unsolicited report from the event channel.
	 * Returns 1 when @cfg was updated to reflect a device-initiated change.
	 */
	int (*parse_event)(struct alloy_device *dev, const uint8_t *buf,
			   size_t len, struct alloy_config *cfg);

	/* NUL-terminated firmware version string */
	int (*firmware_version)(struct alloy_device *dev, char *buf,
				size_t len);

	/*
	 * Persistence.
	 * state_save() emits key/value pairs; state_load() is offered every key
	 * in the file and returns 1 when it consumed one; state_done() runs once
	 * the whole file has been read.
	 */
	void (*state_save)(const struct alloy_driver *drv,
			   const struct alloy_config *cfg, void *ctx,
			   alloy_state_emit_fn emit);
	int (*state_load)(const struct alloy_driver *drv,
			  struct alloy_config *cfg, const char *key,
			  const char *val);
	void (*state_done)(const struct alloy_driver *drv,
			   struct alloy_config *cfg);
};

struct alloy_driver {
	const char *name; /* shown to the user */
	/*
	 * Free-form label the driver picks for its own kind of device
	 * ("mouse", "keyboard", "headset", ...).
	 * Core only ever prints it.
	 */
	const char *kind;

	uint16_t vendor_id;
	uint16_t product_id;

	/* NULL selects alloy_hid_transport */
	const struct alloy_transport *transport;
	/* parameter block the chosen transport understands */
	const void *transport_data;

	/* bytes of driver-owned configuration */
	size_t config_size;

	/*
	 * Opaque pointer the core never touches, for whatever code the driver
	 * shares with: a library under drivers/lib/ uses it to find the metadata
	 * a driver publishes for it.
	 */
	const void *data;

	/*
	 * Optional ASCII art of the device, painted by the front-end.
	 * Provide it in `drivers/<vendor>/<driver>/<driver>_art.txt`;
	 * the build turns it into `build/art_<driver>.h`.
	 *
	 * Prefix a character with "$N" (N = 1..8) to have the driver color it:
	 * the front-end asks ui->art_cell() for that cell, passing N - 1
	 * as the group.
	 * "$i" paints one character in the front-end's accent tint
	 * and "$$" renders a literal dollar.
	 * Markers occupy no column.
	 */
	const char *ascii_art;

	/* command-line flags, in as many groups as the driver cares to list */
	const struct alloy_cli_table *cli_tables;
	uint8_t num_cli_tables;

	const struct alloy_apply_step *apply_steps;
	uint8_t num_apply_steps;

	/* front-end description; without one the device has no interactive UI */
	const struct alloy_ui_desc *ui;

	const struct alloy_driver_ops *ops;
};

/* opened, driver-bound device */
struct alloy_device {
	const struct alloy_driver *drv;
	const struct alloy_transport *tr;
	/* transport-owned state */
	void *tr_data;
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
const char *alloy_driver_kind(const struct alloy_driver *drv);

/* Walk every flag the driver declares, across all of its tables */
const struct alloy_cli_option *
alloy_driver_cli_at(const struct alloy_driver *drv, size_t idx);

/*
 * Scan the registry against connected hardware and collect every supported
 * device currently plugged in.
 * Fills out[] with up to max driver pointers (in registry order) and returns
 * the total number found, which may exceed max.
 * Pass out=NULL to only count.
 */
int alloy_device_enumerate(const struct alloy_driver **out, int max);
int alloy_driver_present(const struct alloy_driver *drv);

/* Open a specific device by USB id (e.g. from --device) */
int alloy_device_open_id(struct alloy_device *dev, uint16_t vendor_id,
			 uint16_t product_id);
void alloy_device_close(struct alloy_device *dev);

/* Transport wrappers; every driver talks to its device through these */
int alloy_dev_write(struct alloy_device *dev, const uint8_t *buf, size_t len);
int alloy_dev_read(struct alloy_device *dev, uint8_t *buf, size_t len,
		   int timeout_ms);
int alloy_dev_poll_event(struct alloy_device *dev, uint8_t *buf, size_t len);
int alloy_dev_send_feature(struct alloy_device *dev, const uint8_t *buf,
			   size_t len);
int alloy_dev_get_feature(struct alloy_device *dev, uint8_t *buf, size_t len);

/* Apply-step lookup and dispatch */
const struct alloy_apply_step *alloy_driver_step(const struct alloy_driver *drv,
						 const char *name);
int alloy_driver_apply(struct alloy_device *dev, const struct alloy_config *cfg,
		       const char *name);
/*
 * Push every step the driver declares, skipping those carrying any bit of
 * @skip_flags (and ALLOY_APPLY_MANUAL always).
 * @report, when given, is called once per step with the driver's return code.
 */
void alloy_driver_apply_all(struct alloy_device *dev,
			    const struct alloy_config *cfg, uint32_t skip_flags,
			    void (*report)(void *ctx, const char *what,
					   int err),
			    void *ctx);

/* Factory defaults */
void alloy_config_defaults(const struct alloy_driver *drv,
			   struct alloy_config *cfg);

#endif /* ALLOY_DRIVER_H */
