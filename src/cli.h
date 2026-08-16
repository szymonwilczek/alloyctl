/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef ALLOY_CLI_H
#define ALLOY_CLI_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "driver.h"

#define ALLOY_CLI_MAX_ACTIVE_OPTS 32

struct alloy_cli_opts {
	int has_device;
	uint16_t vid;
	uint16_t pid;

	/* Standalone command flags */
	int show_help;
	int show_list;
	int show_version;
	int dump_udev;
	int accel_daemon;
	int accel_stop;
	int save_flash;

	int is_action;

	struct alloy_config cfg;
	const struct alloy_cli_option *active_opts[ALLOY_CLI_MAX_ACTIVE_OPTS];
	size_t num_active_opts;
};

void alloy_cli_print_help(FILE *out);

int alloy_cli_parse(int argc, char **argv, struct alloy_cli_opts *opts,
		    char *err_buf, size_t err_len);

int alloy_cli_validate(const struct alloy_driver *drv,
		       const struct alloy_cli_opts *opts, char *err_buf,
		       size_t err_len);

int alloy_cli_apply(struct alloy_device *dev,
		    const struct alloy_cli_opts *opts);

#endif /* ALLOY_CLI_H */
