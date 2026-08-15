// SPDX-License-Identifier: GPL-2.0-only
/*
 * alloyctl - SteelSeries device configuration tool for Linux.
 */
#include <stdio.h>
#include <string.h>

#include "accel.h"
#include "cli.h"
#include "driver.h"
#include "tui.h"
#include "udev.h"

static void list_drivers(void)
{
	const struct alloy_driver *const *iter;

	printf("supported devices:\n");
	alloy_for_each_driver(iter)
	{
		printf("  %04x:%04x  [%s]  %s\n", (*iter)->vendor_id,
		       (*iter)->product_id,
		       alloy_device_type_name((*iter)->type), (*iter)->name);
	}
}

/* Upper bound on connected supported devices offered in the chooser */
#define ALLOY_MAX_CANDIDATES 16

static int open_selected(struct alloy_device *dev)
{
	const struct alloy_driver *cands[ALLOY_MAX_CANDIDATES];
	const struct alloy_driver *pick;
	int count;
	int idx = 0;

	count = alloy_device_enumerate(cands, ALLOY_MAX_CANDIDATES);
	if (count == 0) {
		fprintf(stderr, "alloyctl: no compatible device found.\n"
				"alloyctl configures SteelSeries devices only; "
				"none is connected.\n");
		list_drivers();
		return 1;
	}

	if (count > ALLOY_MAX_CANDIDATES)
		count = ALLOY_MAX_CANDIDATES;

	/* more than one plugged in: let the user pick which to configure */
	if (count > 1) {
		idx = alloy_tui_select_device(cands, count);
		if (idx < 0)
			return 130; /* user aborted the chooser */
	}
	pick = cands[idx];

	if (alloy_device_open_id(dev, pick->vendor_id, pick->product_id)) {
		fprintf(stderr,
			"alloyctl: cannot open %s (%04x:%04x) - "
			"no permission to open /dev/hidraw*?\n"
			"Install the udev rules once with 'sudo make install' "
			"(or 'sudo ./install.sh').\n",
			pick->name, pick->vendor_id, pick->product_id);
		return 1;
	}
	return 0;
}

int main(int argc, char **argv)
{
	struct alloy_cli_opts opts;
	struct alloy_device dev;
	char err_buf[256];
	int ret;

	if (alloy_cli_parse(argc, argv, &opts, err_buf, sizeof(err_buf)) < 0) {
		fprintf(stderr, "alloyctl: error: %s\n", err_buf);
		return 1;
	}

	if (opts.show_help) {
		alloy_cli_print_help(stdout);
		return 0;
	}
	if (opts.show_list) {
		list_drivers();
		return 0;
	}
	if (opts.show_version) {
		printf("alloyctl %s\n", ALLOY_VERSION);
		return 0;
	}
	if (opts.dump_udev) {
		alloy_udev_rules_write(stdout);
		return 0;
	}
	if (opts.accel_stop)
		return alloy_accel_stop(opts.vid, opts.pid) ? 1 : 0;
	if (opts.accel_daemon)
		return alloy_accel_daemon_run(opts.vid, opts.pid);

	if (opts.has_device) {
		if (alloy_device_open_id(&dev, opts.vid, opts.pid)) {
			fprintf(stderr,
				"alloyctl: no supported device found for "
				"%04x:%04x "
				"(or no permission to open /dev/hidraw*; "
				"install the udev rules with 'sudo make "
				"install')\n",
				opts.vid, opts.pid);
			list_drivers();
			return 1;
		}
	} else {
		ret = open_selected(&dev);
		if (ret)
			return ret == 130 ? 0 : ret;
	}

	if (alloy_cli_validate(dev.drv, &opts, err_buf, sizeof(err_buf)) < 0) {
		fprintf(stderr, "alloyctl: error: %s\n", err_buf);
		alloy_device_close(&dev);
		return 1;
	}

	if (opts.is_action) {
		ret = alloy_cli_apply(&dev, &opts);
		alloy_device_close(&dev);
		return ret;
	}

	ret = alloy_tui_run(&dev);
	alloy_device_close(&dev);
	return ret;
}
