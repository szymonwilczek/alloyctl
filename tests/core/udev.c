// SPDX-License-Identifier: GPL-2.0-only
/*
 * Unit tests for the udev rule generator (src/udev.c).
 *
 * Rules are built from the driver registry, so the test walks the same registry
 * and asserts every supported device gets a hidraw match line with the right
 * transport-tagged HID name pattern and access tags.
 *
 * No hardware and no real /dev involved: output is captured through temp stream.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver.h"
#include "test.h"
#include "udev.h"

/*
 * Render the rules into a heap buffer the caller frees.
 * Uses tmpfile() (ISO C) so the test needs no feature-test
 * macro and no fixed cap.
 */
static char *render_rules(void)
{
	FILE *tmp = tmpfile();
	long len;
	char *buf;

	ASSERT_TRUE(tmp != NULL);
	if (!tmp)
		return NULL;
	alloy_udev_rules_write(tmp);

	len = ftell(tmp);
	ASSERT_TRUE(len > 0);
	if (len <= 0) {
		fclose(tmp);
		return NULL;
	}
	buf = malloc((size_t)len + 1);
	ASSERT_TRUE(buf != NULL);
	if (!buf) {
		fclose(tmp);
		return NULL;
	}
	rewind(tmp);
	buf[fread(buf, 1, (size_t)len, tmp)] = '\0';
	fclose(tmp);
	return buf;
}

ALLOY_TEST(test_udev_covers_every_driver)
{
	const struct alloy_driver *const *iter;
	char *rules = render_rules();

	ASSERT_TRUE(rules != NULL);
	if (!rules)
		return;

	/* uaccess plus the group fallback must be present at all */
	ASSERT_TRUE(strstr(rules, "TAG+=\"uaccess\"") != NULL);
	ASSERT_TRUE(strstr(rules, "GROUP=\"input\"") != NULL);

	/*
	 * each registered device must appear as a KERNELS match keyed on its
	 * "BUS:VID:PID" HID name, with USB/2.4 GHz on bus 0003 and Bluetooth
	 * on the bus the driver declares
	 */
	alloy_for_each_driver(iter)
	{
		const struct alloy_driver *drv = *iter;
		unsigned bus = drv->bustype ? drv->bustype : 0x0003;
		char pat[64];

		snprintf(pat, sizeof(pat), "KERNELS==\"%04X:%04X:%04X.*\"", bus,
			 drv->vendor_id, drv->product_id);
		ASSERT_TRUE(strstr(rules, pat) != NULL);
	}

	free(rules);
}

ALLOY_TEST(test_udev_has_spdx_header)
{
	char *rules = render_rules();

	ASSERT_TRUE(rules != NULL);
	if (!rules)
		return;
	/* installed verbatim, so it must carry the license tag udev files use */
	ASSERT_TRUE(strncmp(rules, "# SPDX-License-Identifier: GPL-2.0-only",
			    39) == 0);
	free(rules);
}
