// SPDX-License-Identifier: GPL-2.0-only
/*
 * alloyctl - SteelSeries device configuration TUI for Linux.
 */
#include <stdio.h>
#include <string.h>

#include "accel.h"
#include "driver.h"
#include "tui.h"
#include "udev.h"

static void list_drivers(void)
{
	const struct alloy_driver *const *iter;

	printf("supported devices:\n");
	alloy_for_each_driver(iter)
	{
		printf("  %04x:%04x  %s\n", (*iter)->vendor_id,
		       (*iter)->product_id, (*iter)->name);
	}
}

/* Upper bound on connected supported mice offered in the chooser */
#define ALLOY_MAX_CANDIDATES 16

static int open_selected(struct alloy_device *dev)
{
	const struct alloy_driver *cands[ALLOY_MAX_CANDIDATES];
	const struct alloy_driver *pick;
	int count;
	int idx = 0;

	count = alloy_device_enumerate(cands, ALLOY_MAX_CANDIDATES);
	if (count == 0) {
		fprintf(stderr, "alloyctl: no compatible mouse found.\n"
				"alloyctl configures SteelSeries mice only; "
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
	struct alloy_device dev;
	unsigned vid;
	unsigned pid;
	int ret;

	if (argc > 1 && !strcmp(argv[1], "--list")) {
		list_drivers();
		return 0;
	}
	if (argc > 1 && !strcmp(argv[1], "--version")) {
		printf("alloyctl %s\n", ALLOY_VERSION);
		return 0;
	}
	/*
	 * Print udev rules for unprivileged /dev/hidraw* access,
	 * one line per supported device, built from the driver registry.
	 * Meant to be piped into rules file; the installers do this for you.
	 *   alloyctl --dump-udev | sudo tee \
	 *     /usr/lib/udev/rules.d/71-alloyctl-hidraw.rules
	 */
	if (argc > 1 && !strcmp(argv[1], "--dump-udev")) {
		alloy_udev_rules_write(stdout);
		return 0;
	}

	/*
	 * Host-side pointer-transform daemon
	 * (acceleration / deceleration / angle snapping)
	 * Normally spawned by the TUI or an autostart entry, not run by hand;
	 * takes VID:PID because it binds an evdev node,
	 * not a hidraw one.
	 */
	if (argc > 2 && (!strcmp(argv[1], "--accel-daemon") ||
			 !strcmp(argv[1], "--accel-stop"))) {
		if (sscanf(argv[2], "%4x:%4x", &vid, &pid) != 2) {
			fprintf(stderr,
				"alloyctl: %s expects VID:PID "
				"(e.g. 1038:184c)\n",
				argv[1]);
			return 1;
		}
		if (!strcmp(argv[1], "--accel-stop"))
			return alloy_accel_stop((uint16_t)vid, (uint16_t)pid) ?
				       1 :
				       0;
		return alloy_accel_daemon_run((uint16_t)vid, (uint16_t)pid);
	}

	if (argc > 2 && !strcmp(argv[1], "--device")) {
		if (sscanf(argv[2], "%4x:%4x", &vid, &pid) != 2) {
			fprintf(stderr, "alloyctl: --device expects VID:PID "
					"(e.g. 1038:184c)\n");
			return 1;
		}
		if (alloy_device_open_id(&dev, (uint16_t)vid, (uint16_t)pid)) {
			fprintf(stderr,
				"alloyctl: no supported mouse found "
				"(or no permission to open /dev/hidraw*; "
				"install the udev rules with 'sudo make "
				"install')\n");
			list_drivers();
			return 1;
		}
	} else {
		ret = open_selected(&dev);
		if (ret)
			return ret == 130 ? 0 : ret;
	}

	ret = alloy_tui_run(&dev);
	alloy_device_close(&dev);
	return ret;
}
