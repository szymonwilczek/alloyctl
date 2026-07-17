// SPDX-License-Identifier: GPL-2.0-only
/*
 * alloyctl - SteelSeries device configuration TUI for Linux.
 */
#include <stdio.h>
#include <string.h>

#include "driver.h"
#include "tui.h"

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

int main(int argc, char **argv)
{
	struct alloy_device dev;
	unsigned vid;
	unsigned pid;
	int opened;
	int ret;

	if (argc > 1 && !strcmp(argv[1], "--list")) {
		list_drivers();
		return 0;
	}
	if (argc > 1 && !strcmp(argv[1], "--version")) {
		printf("alloyctl %s\n", ALLOY_VERSION);
		return 0;
	}

	if (argc > 2 && !strcmp(argv[1], "--device")) {
		if (sscanf(argv[2], "%4x:%4x", &vid, &pid) != 2) {
			fprintf(stderr, "alloyctl: --device expects VID:PID "
					"(e.g. 1038:184c)\n");
			return 1;
		}
		opened = alloy_device_open_id(&dev, (uint16_t)vid,
					      (uint16_t)pid);
	} else {
		opened = alloy_device_open(&dev);
	}

	if (opened) {
		fprintf(stderr, "alloyctl: no supported mouse found "
				"(or no permission to open /dev/hidraw*)\n");
		list_drivers();
		return 1;
	}

	ret = alloy_tui_run(&dev);
	alloy_device_close(&dev);
	return ret;
}
