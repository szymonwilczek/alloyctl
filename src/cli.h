/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Command-line engine.
 *
 * Core owns only the handful of flags that are about the program itself.
 * Every other flag comes from a driver, and is resolved against the driver
 * that is actually bound, so one device never sees another's vocabulary.
 *
 * Standalone commands (daemons, helpers) come from the command registry,
 * which driver-library code can add to without the core knowing they exist.
 */
#ifndef ALLOY_CLI_H
#define ALLOY_CLI_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "driver.h"

#define ALLOY_CLI_MAX_ARGS 32

struct alloy_cli_arg {
	const char *flag;
	const char *value; /* NULL when the flag takes no argument */
};

struct alloy_cli_opts {
	int has_device;
	uint16_t vid;
	uint16_t pid;

	/* program-level flags */
	int show_help;
	int show_list;
	int show_version;
	int dump_udev;
	int save_flash;

	/* standalone command from the registry, run without a device */
	const struct alloy_cli_command *command;
	const char *command_arg;

	/*
	 * Driver flags, recorded in the order they were given and resolved once
	 * a device is bound.
	 */
	struct alloy_cli_arg args[ALLOY_CLI_MAX_ARGS];
	size_t num_args;

	/* filled by alloy_cli_bind() */
	struct alloy_config *cfg;
	const struct alloy_cli_option *active[ALLOY_CLI_MAX_ARGS];
	size_t num_active;
	int is_action;
};

void alloy_cli_print_help(FILE *out);

/* Parse argv; driver flags are only recorded here, not interpreted */
int alloy_cli_parse(int argc, char **argv, struct alloy_cli_opts *opts,
		    char *err_buf, size_t err_len);

/*
 * Resolve the recorded flags against @drv, parse and validate their arguments
 * into a freshly allocated configuration.
 */
int alloy_cli_bind(const struct alloy_driver *drv, struct alloy_cli_opts *opts,
		   char *err_buf, size_t err_len);

/* Push everything the resolved flags asked for */
int alloy_cli_apply(struct alloy_device *dev, struct alloy_cli_opts *opts);

void alloy_cli_free(struct alloy_cli_opts *opts);

#endif /* ALLOY_CLI_H */
