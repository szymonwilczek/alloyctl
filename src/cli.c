// SPDX-License-Identifier: GPL-2.0-only
/*
 * Generic declarative command-line interface engine for alloyctl.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "cli.h"
#include "keyboard_driver.h"
#include "mouse_driver.h"
#include "state.h"

static const struct alloy_cli_option *find_option(const char *arg)
{
	if (!arg || !*arg)
		return NULL;

	/* 1. Common options */
	for (size_t i = 0; i < alloy_num_common_cli_options; i++) {
		const struct alloy_cli_option *opt =
			&alloy_common_cli_options[i];
		if (!strcmp(opt->name, arg) ||
		    (opt->alias && !strcmp(opt->alias, arg)) ||
		    (opt->short_name && !strcmp(opt->short_name, arg)))
			return opt;
	}

	/* 2. Mouse options */
	for (size_t i = 0; i < alloy_num_mouse_cli_options; i++) {
		const struct alloy_cli_option *opt =
			&alloy_mouse_cli_options[i];
		if (!strcmp(opt->name, arg) ||
		    (opt->alias && !strcmp(opt->alias, arg)) ||
		    (opt->short_name && !strcmp(opt->short_name, arg)))
			return opt;
	}

	/* 3. Keyboard options */
	for (size_t i = 0; i < alloy_num_keyboard_cli_options; i++) {
		const struct alloy_cli_option *opt =
			&alloy_keyboard_cli_options[i];
		if (!strcmp(opt->name, arg) ||
		    (opt->alias && !strcmp(opt->alias, arg)) ||
		    (opt->short_name && !strcmp(opt->short_name, arg)))
			return opt;
	}

	/* 4. Driver-specific options from registered drivers */
	const struct alloy_driver *const *iter;
	alloy_for_each_driver(iter)
	{
		const struct alloy_driver *drv = *iter;
		if (!drv || !drv->cli_options)
			continue;
		for (uint8_t i = 0; i < drv->num_cli_options; i++) {
			const struct alloy_cli_option *opt =
				&drv->cli_options[i];
			if (!strcmp(opt->name, arg) ||
			    (opt->alias && !strcmp(opt->alias, arg)) ||
			    (opt->short_name && !strcmp(opt->short_name, arg)))
				return opt;
		}
	}

	return NULL;
}

static void print_option_row(FILE *out, const struct alloy_cli_option *opt)
{
	char names[48];
	if (opt->alias && opt->short_name)
		snprintf(names, sizeof(names), "%s, %s, %s", opt->short_name,
			 opt->name, opt->alias);
	else if (opt->alias)
		snprintf(names, sizeof(names), "%s, %s", opt->name, opt->alias);
	else if (opt->short_name)
		snprintf(names, sizeof(names), "%s, %s", opt->short_name,
			 opt->name);
	else
		snprintf(names, sizeof(names), "%s", opt->name);

	if (opt->arg_desc) {
		char full[64];
		snprintf(full, sizeof(full), "%s %s", names, opt->arg_desc);
		fprintf(out, "      %-24s %s\n", full,
			opt->help ? opt->help : "");
	} else {
		fprintf(out, "      %-24s %s\n", names,
			opt->help ? opt->help : "");
	}
}

void alloy_cli_print_help(FILE *out)
{
	fprintf(out,
		"Usage: alloyctl [OPTIONS]\n\n"
		"Interactive TUI and CLI utility for configuring gaming peripherals on Linux.\n\n"
		"General Options:\n"
		"  -h, --help                  Show this help text and exit\n"
		"  -v, --version               Show version information and exit\n"
		"  -l, --list                  List all supported devices\n"
		"  -d, --device <VID:PID>      Target specific device (e.g. 1038:1824)\n"
		"      --dump-udev             Print udev rules for unprivileged /dev/hidraw access\n"
		"      --accel-daemon <VID:PID> Run host pointer transform daemon\n"
		"      --accel-stop <VID:PID>   Stop host pointer transform daemon\n"
		"      --save                  Commit live settings to device onboard flash\n\n"
		"Common Configuration Options:\n");

	for (size_t i = 0; i < alloy_num_common_cli_options; i++)
		print_option_row(out, &alloy_common_cli_options[i]);

	fprintf(out, "\nMouse Options:\n");
	for (size_t i = 0; i < alloy_num_mouse_cli_options; i++)
		print_option_row(out, &alloy_mouse_cli_options[i]);

	fprintf(out, "\nKeyboard Options:\n");
	for (size_t i = 0; i < alloy_num_keyboard_cli_options; i++)
		print_option_row(out, &alloy_keyboard_cli_options[i]);

	fprintf(out,
		"\nInteractive TUI Mode:\n"
		"  Running 'alloyctl' without configuration options launches the full-screen\n"
		"  interactive terminal user interface.\n\n"
		"Examples:\n"
		"  alloyctl\n"
		"  alloyctl --device 1038:1824 --dpi 1600 --polling 1000 --save\n"
		"  alloyctl --brightness 50 --meta-lock on\n"
		"  alloyctl --color 0:FF0000 --fx steady\n");
}

int alloy_cli_parse(int argc, char **argv, struct alloy_cli_opts *opts,
		    char *err_buf, size_t err_len)
{
	int i = 1;
	unsigned vid, pid;

	memset(opts, 0, sizeof(*opts));

	while (i < argc) {
		const char *arg = argv[i++];

		if (!strcmp(arg, "-h") || !strcmp(arg, "--help")) {
			opts->show_help = 1;
			return 0;
		}
		if (!strcmp(arg, "-l") || !strcmp(arg, "--list")) {
			opts->show_list = 1;
			return 0;
		}
		if (!strcmp(arg, "-v") || !strcmp(arg, "--version")) {
			opts->show_version = 1;
			return 0;
		}
		if (!strcmp(arg, "--dump-udev")) {
			opts->dump_udev = 1;
			return 0;
		}
		if (!strcmp(arg, "--accel-daemon")) {
			if (i >= argc) {
				snprintf(
					err_buf, err_len,
					"--accel-daemon requires a device ID (VID:PID)");
				return -1;
			}
			if (sscanf(argv[i++], "%x:%x", &vid, &pid) != 2) {
				snprintf(
					err_buf, err_len,
					"invalid device ID format for --accel-daemon; expected VID:PID (hex)");
				return -1;
			}
			opts->vid = (uint16_t)vid;
			opts->pid = (uint16_t)pid;
			opts->has_device = 1;
			opts->accel_daemon = 1;
			return 0;
		}
		if (!strcmp(arg, "--accel-stop")) {
			if (i >= argc) {
				snprintf(
					err_buf, err_len,
					"--accel-stop requires a device ID (VID:PID)");
				return -1;
			}
			if (sscanf(argv[i++], "%x:%x", &vid, &pid) != 2) {
				snprintf(
					err_buf, err_len,
					"invalid device ID format for --accel-stop; expected VID:PID (hex)");
				return -1;
			}
			opts->vid = (uint16_t)vid;
			opts->pid = (uint16_t)pid;
			opts->has_device = 1;
			opts->accel_stop = 1;
			return 0;
		}
		if (!strcmp(arg, "-d") || !strcmp(arg, "--device")) {
			if (i >= argc) {
				snprintf(
					err_buf, err_len,
					"--device requires a device ID (VID:PID)");
				return -1;
			}
			if (sscanf(argv[i++], "%x:%x", &vid, &pid) != 2) {
				snprintf(
					err_buf, err_len,
					"invalid device ID format; expected VID:PID (hex)");
				return -1;
			}
			opts->vid = (uint16_t)vid;
			opts->pid = (uint16_t)pid;
			opts->has_device = 1;
			continue;
		}
		if (!strcmp(arg, "--save")) {
			opts->save_flash = 1;
			opts->is_action = 1;
			continue;
		}

		/* Match against declarative CLI option tables */
		const struct alloy_cli_option *opt = find_option(arg);
		if (opt) {
			const char *val_str = NULL;
			if (opt->has_arg == 1) {
				if (i >= argc) {
					snprintf(err_buf, err_len,
						 "%s requires an argument",
						 arg);
					return -1;
				}
				val_str = argv[i++];
			} else if (opt->has_arg == 2) {
				if (i < argc && argv[i][0] != '-')
					val_str = argv[i++];
			}

			if (opt->parse) {
				if (opt->parse(val_str, &opts->cfg, err_buf,
					       err_len) < 0)
					return -1;
			}

			if (opts->num_active_opts < ALLOY_CLI_MAX_ACTIVE_OPTS)
				opts->active_opts[opts->num_active_opts++] =
					opt;
			opts->is_action = 1;
			continue;
		}

		snprintf(err_buf, err_len, "unknown option '%s'", arg);
		return -1;
	}

	return 0;
}

int alloy_cli_validate(const struct alloy_driver *drv,
		       const struct alloy_cli_opts *opts, char *err_buf,
		       size_t err_len)
{
	if (!drv || !opts)
		return -1;

	for (size_t i = 0; i < opts->num_active_opts; i++) {
		const struct alloy_cli_option *opt = opts->active_opts[i];

		/* 1. Device category check */
		if (opt->category == ALLOY_OPT_MOUSE &&
		    !alloy_driver_is_mouse(drv)) {
			snprintf(err_buf, err_len,
				 "%s is not supported on keyboards (%s is a "
				 "keyboard)",
				 opt->name, drv->name);
			return -1;
		}
		if (opt->category == ALLOY_OPT_KEYBOARD &&
		    !alloy_driver_is_keyboard(drv)) {
			snprintf(err_buf, err_len,
				 "%s is not supported on mice (%s is a mouse)",
				 opt->name, drv->name);
			return -1;
		}
		if (opt->category == ALLOY_OPT_DRIVER_SPECIFIC) {
			int supported = 0;
			for (uint8_t d = 0; d < drv->num_cli_options; d++) {
				if (&drv->cli_options[d] == opt ||
				    !strcmp(drv->cli_options[d].name,
					    opt->name)) {
					supported = 1;
					break;
				}
			}
			if (!supported) {
				snprintf(err_buf, err_len,
					 "%s is not supported on '%s'",
					 opt->name, drv->name);
				return -1;
			}
		}

		/* 2. Capability flag check */
		if (opt->required_cap && !(drv->caps & opt->required_cap)) {
			snprintf(err_buf, err_len,
				 "device '%s' does not support %s", drv->name,
				 opt->help ? opt->help : opt->name);
			return -1;
		}

		/* 3. Custom validator callback */
		if (opt->validate) {
			if (opt->validate(drv, &opts->cfg, err_buf, err_len) <
			    0)
				return -1;
		}
	}

	return 0;
}

int alloy_cli_apply(struct alloy_device *dev, const struct alloy_cli_opts *opts)
{
	struct alloy_config live_cfg;
	int probed = 0;
	int ret;

	if (dev->drv->ops && dev->drv->ops->read_config) {
		if (dev->drv->ops->read_config(dev, &live_cfg) == 0)
			probed = 1;
	}
	if (!probed && alloy_state_load(dev->drv, &live_cfg) < 0)
		dev->drv->config_defaults(dev->drv, &live_cfg);

	/* Apply requested changes from parsed config */
	for (size_t i = 0; i < opts->num_active_opts; i++) {
		const struct alloy_cli_option *opt = opts->active_opts[i];

		if (!strcmp(opt->name, "--brightness"))
			live_cfg.common.brightness =
				opts->cfg.common.brightness;
		else if (!strcmp(opt->name, "--polling"))
			live_cfg.common.polling_hz =
				opts->cfg.common.polling_hz;
		else if (!strcmp(opt->name, "--color")) {
			for (uint8_t z = 0; z < ALLOY_MAX_LED_ZONES; z++)
				if (opts->cfg.common.zone_color[z].r ||
				    opts->cfg.common.zone_color[z].g ||
				    opts->cfg.common.zone_color[z].b)
					live_cfg.common.zone_color[z] =
						opts->cfg.common.zone_color[z];
		} else if (!strcmp(opt->name, "--fx")) {
			for (uint8_t z = 0; z < ALLOY_MAX_LED_ZONES; z++)
				live_cfg.common.zone_fx[z] =
					opts->cfg.common.zone_fx[z];
		} else if (!strcmp(opt->name, "--multicolor")) {
			for (uint8_t z = 0; z < ALLOY_MAX_LED_ZONES; z++)
				live_cfg.common.zone_fx_multicolor[z] =
					opts->cfg.common.zone_fx_multicolor[z];
		} else if (!strcmp(opt->name, "--direction")) {
			for (uint8_t z = 0; z < ALLOY_MAX_LED_ZONES; z++)
				live_cfg.common.zone_fx_direction[z] =
					opts->cfg.common.zone_fx_direction[z];
		} else if (!strcmp(opt->name, "--dpi") ||
			   !strcmp(opt->name, "--cpi")) {
			for (uint8_t d = 0; d < live_cfg.mouse.dpi_count; d++) {
				live_cfg.mouse.dpi[d][0] =
					opts->cfg.mouse.dpi[0][0];
				live_cfg.mouse.dpi[d][1] =
					opts->cfg.mouse.dpi[0][1];
			}
		} else if (!strcmp(opt->name, "--accel")) {
			live_cfg.mouse.acceleration =
				opts->cfg.mouse.acceleration;
			live_cfg.mouse.accel_enabled = 1;
		} else if (!strcmp(opt->name, "--decel")) {
			live_cfg.mouse.deceleration =
				opts->cfg.mouse.deceleration;
			live_cfg.mouse.accel_enabled = 1;
		} else if (!strcmp(opt->name, "--snap")) {
			live_cfg.mouse.angle_snapping =
				opts->cfg.mouse.angle_snapping;
			live_cfg.mouse.accel_enabled = 1;
		} else if (!strcmp(opt->name, "--high-efficiency")) {
			live_cfg.mouse.high_efficiency =
				opts->cfg.mouse.high_efficiency;
		} else if (!strcmp(opt->name, "--meta-lock") ||
			   !strcmp(opt->name, "--win-lock")) {
			live_cfg.kbd.win_lock = opts->cfg.kbd.win_lock;
		} else if (!strcmp(opt->name, "--snap-tap")) {
			live_cfg.kbd.snap_tap = opts->cfg.kbd.snap_tap;
		} else if (!strcmp(opt->name, "--profile")) {
			live_cfg.kbd.profile_active =
				opts->cfg.kbd.profile_active;
		}

		/* Execute driver apply hook if provided */
		if (opt->apply) {
			ret = opt->apply(dev, &live_cfg);
			if (ret < 0)
				return ret;
		}
	}

	if (opts->save_flash && dev->drv->ops && dev->drv->ops->save) {
		ret = dev->drv->ops->save(dev);
		if (ret < 0)
			return ret;
	}

	return alloy_state_store(dev->drv, &live_cfg);
}
