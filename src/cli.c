/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Command-line engine.
 *
 * Parsing happens in two passes because the vocabulary depends on the device:
 * the first records whatever flags were given, the second resolves them against
 * the driver that was actually bound.
 * A flag another driver declares is therefore reported as unsupported here.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cli.h"

static int option_matches(const struct alloy_cli_option *opt, const char *arg)
{
	return (opt->name && !strcmp(opt->name, arg)) ||
	       (opt->alias && !strcmp(opt->alias, arg)) ||
	       (opt->short_name && !strcmp(opt->short_name, arg));
}

static const struct alloy_cli_option *
driver_option(const struct alloy_driver *drv, const char *arg)
{
	const struct alloy_cli_option *opt;
	size_t i;

	for (i = 0; (opt = alloy_driver_cli_at(drv, i)); i++) {
		if (option_matches(opt, arg))
			return opt;
	}
	return NULL;
}

/*
 * How many words a flag consumes, learned from whichever driver declares it.
 * Only the arity is taken from the registry at large - which driver may
 * actually use the flag is decided later, once one is bound.
 */
static int option_arity(const char *arg)
{
	const struct alloy_driver *const *iter;

	alloy_for_each_driver(iter)
	{
		const struct alloy_cli_option *opt = driver_option(*iter, arg);

		if (opt)
			return opt->has_arg;
	}
	return -1;
}

static const struct alloy_cli_command *find_command(const char *arg)
{
	const struct alloy_cli_command *const *iter;

	alloy_for_each_command(iter)
	{
		if (!strcmp((*iter)->name, arg))
			return *iter;
	}
	return NULL;
}

static int option_available(const struct alloy_cli_option *opt,
			    const struct alloy_driver *drv)
{
	return !opt->available || opt->available(drv);
}

static void print_row(FILE *out, const char *short_name, const char *name,
		      const char *alias, const char *arg_desc, const char *help)
{
	char names[64];
	char full[96];

	if (alias && short_name)
		snprintf(names, sizeof(names), "%s, %s, %s", short_name, name,
			 alias);
	else if (alias)
		snprintf(names, sizeof(names), "%s, %s", name, alias);
	else if (short_name)
		snprintf(names, sizeof(names), "%s, %s", short_name, name);
	else
		snprintf(names, sizeof(names), "%s", name);

	if (arg_desc)
		snprintf(full, sizeof(full), "%s %s", names, arg_desc);
	else
		snprintf(full, sizeof(full), "%s", names);

	fprintf(out, "      %-28s %s\n", full, help ? help : "");
}

void alloy_cli_print_help(FILE *out)
{
	const struct alloy_cli_command *const *cmd;
	const struct alloy_driver *const *iter;

	fprintf(out,
		"Usage: alloyctl [OPTIONS]\n\n"
		"Interactive TUI and CLI utility for configuring devices on Linux.\n\n"
		"General Options:\n"
		"  -h, --help                       Show this help text and exit\n"
		"  -v, --version                    Show version information and exit\n"
		"  -l, --list                       List all supported devices\n"
		"  -d, --device <VID:PID>           Target a specific device\n"
		"      --dump-udev                  Print udev rules for device access\n"
		"      --save                       Commit live settings to onboard storage\n");

	if (alloy_command_first() != alloy_command_last()) {
		fprintf(out, "\nCommands:\n");
		alloy_for_each_command(cmd)
			print_row(out, NULL, (*cmd)->name, NULL,
				  (*cmd)->arg_desc, (*cmd)->help);
	}

	alloy_for_each_driver(iter)
	{
		const struct alloy_driver *drv = *iter;
		const struct alloy_cli_option *opt;
		size_t i;

		if (!alloy_driver_cli_at(drv, 0))
			continue;
		fprintf(out, "\n%s (%04x:%04x):\n", drv->name, drv->vendor_id,
			drv->product_id);
		for (i = 0; (opt = alloy_driver_cli_at(drv, i)); i++) {
			if (!option_available(opt, drv))
				continue;
			print_row(out, opt->short_name, opt->name, opt->alias,
				  opt->arg_desc, opt->help);
		}
	}

	fprintf(out,
		"\nInteractive mode:\n"
		"  Running 'alloyctl' without configuration options launches the\n"
		"  full-screen interactive interface for the connected device.\n");
}

int alloy_cli_parse(int argc, char **argv, struct alloy_cli_opts *opts,
		    char *err_buf, size_t err_len)
{
	unsigned vid;
	unsigned pid;
	int i = 1;

	memset(opts, 0, sizeof(*opts));

	while (i < argc) {
		const char *arg = argv[i++];
		const struct alloy_cli_command *cmd;
		int arity;

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
			continue;
		}

		cmd = find_command(arg);
		if (cmd) {
			if (cmd->has_arg == 1) {
				if (i >= argc) {
					snprintf(err_buf, err_len,
						 "%s requires an argument",
						 arg);
					return -1;
				}
				opts->command_arg = argv[i++];
			} else if (cmd->has_arg == 2 && i < argc &&
				   argv[i][0] != '-') {
				opts->command_arg = argv[i++];
			}
			opts->command = cmd;
			return 0;
		}

		arity = option_arity(arg);
		if (arity < 0) {
			snprintf(err_buf, err_len, "unknown option '%s'", arg);
			return -1;
		}
		if (opts->num_args >= ALLOY_CLI_MAX_ARGS) {
			snprintf(err_buf, err_len, "too many options");
			return -1;
		}

		opts->args[opts->num_args].flag = arg;
		opts->args[opts->num_args].value = NULL;
		if (arity == 1) {
			if (i >= argc) {
				snprintf(err_buf, err_len,
					 "%s requires an argument", arg);
				return -1;
			}
			opts->args[opts->num_args].value = argv[i++];
		} else if (arity == 2 && i < argc && argv[i][0] != '-') {
			opts->args[opts->num_args].value = argv[i++];
		}
		opts->num_args++;
	}
	return 0;
}

int alloy_cli_bind(const struct alloy_driver *drv, struct alloy_cli_opts *opts,
		   char *err_buf, size_t err_len)
{
	size_t i;

	opts->num_active = 0;
	opts->is_action = opts->save_flash;

	if (!opts->num_args && !opts->save_flash)
		return 0;

	opts->cfg = alloy_config_alloc(drv);
	if (!opts->cfg) {
		snprintf(err_buf, err_len, "out of memory");
		return -1;
	}
	alloy_config_defaults(drv, opts->cfg);

	for (i = 0; i < opts->num_args; i++) {
		const struct alloy_cli_arg *a = &opts->args[i];
		const struct alloy_cli_option *opt =
			driver_option(drv, a->flag);

		if (!opt || !option_available(opt, drv)) {
			snprintf(err_buf, err_len,
				 "%s is not supported by %s (%04x:%04x)",
				 a->flag, drv->name, drv->vendor_id,
				 drv->product_id);
			return -1;
		}
		if (opt->has_arg == 1 && !a->value) {
			snprintf(err_buf, err_len, "%s requires an argument",
				 a->flag);
			return -1;
		}
		if (opt->parse &&
		    opt->parse(drv, a->value, opts->cfg, err_buf, err_len) < 0)
			return -1;

		opts->active[opts->num_active++] = opt;
		opts->is_action = 1;
	}

	for (i = 0; i < opts->num_active; i++) {
		const struct alloy_cli_option *opt = opts->active[i];

		if (opt->validate &&
		    opt->validate(drv, opts->cfg, err_buf, err_len) < 0)
			return -1;
	}
	return 0;
}

int alloy_cli_apply(struct alloy_device *dev, struct alloy_cli_opts *opts)
{
	const struct alloy_driver *drv = dev->drv;
	int failed = 0;
	size_t i;

	for (i = 0; i < opts->num_active; i++) {
		const struct alloy_cli_option *opt = opts->active[i];
		int ret = 0;

		if (opt->apply)
			ret = opt->apply(dev, opts->cfg);
		else if (opt->apply_step)
			ret = alloy_driver_apply(dev, opts->cfg,
						 opt->apply_step);
		if (ret) {
			fprintf(stderr,
				"alloyctl: %s: device did not accept "
				"the change\n",
				opt->name);
			failed = 1;
		}
	}

	if (opts->save_flash && drv->ops && drv->ops->save) {
		if (drv->ops->save(dev)) {
			fprintf(stderr,
				"alloyctl: --save: device did not acknowledge\n");
			failed = 1;
		}
	}
	return failed;
}

void alloy_cli_free(struct alloy_cli_opts *opts)
{
	alloy_config_free(opts->cfg);
	opts->cfg = NULL;
}
