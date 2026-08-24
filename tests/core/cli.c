// SPDX-License-Identifier: GPL-2.0-only
/*
 * Unit tests for declarative CLI argument parsing and device-type validation.
 */
#include <string.h>

#include "cli.h"
#include "driver.h"
#include "lib/devcfg.h"
#include "lib/keyboard.h"
#include "lib/mouse.h"
#include "test.h"

ALLOY_TEST(test_cli_parse_program_options)
{
	char *argv[] = {
		"alloyctl",
		"--device",
		"1038:1824",
		"--save",
	};
	int argc = (int)ALLOY_ARRAY_SIZE(argv);
	struct alloy_cli_opts opts;
	char err[128] = "";

	ASSERT_EQ(alloy_cli_parse(argc, argv, &opts, err, sizeof(err)), 0);
	ASSERT_TRUE(opts.has_device);
	ASSERT_EQ(opts.vid, 0x1038);
	ASSERT_EQ(opts.pid, 0x1824);
	ASSERT_TRUE(opts.save_flash);

	char *help_argv[] = { "alloyctl", "-h" };
	ASSERT_EQ(alloy_cli_parse(2, help_argv, &opts, err, sizeof(err)), 0);
	ASSERT_TRUE(opts.show_help);

	char *ver_argv[] = { "alloyctl", "--version" };
	ASSERT_EQ(alloy_cli_parse(2, ver_argv, &opts, err, sizeof(err)), 0);
	ASSERT_TRUE(opts.show_version);

	char *list_argv[] = { "alloyctl", "-l" };
	ASSERT_EQ(alloy_cli_parse(2, list_argv, &opts, err, sizeof(err)), 0);
	ASSERT_TRUE(opts.show_list);
}

ALLOY_TEST(test_cli_bind_mouse_options)
{
	const struct alloy_driver *drv = alloy_driver_find(0x1038, 0x1824);
	ASSERT_TRUE(drv != NULL);

	char *argv[] = {
		"alloyctl", "--dpi", "1200",   "--accel", "30",
		"--decel",  "10",    "--snap", "12",
	};
	int argc = (int)ALLOY_ARRAY_SIZE(argv);
	struct alloy_cli_opts opts;
	char err[128] = "";

	ASSERT_EQ(alloy_cli_parse(argc, argv, &opts, err, sizeof(err)), 0);
	ASSERT_EQ(opts.num_args, 4);

	ASSERT_EQ(alloy_cli_bind(drv, &opts, err, sizeof(err)), 0);
	ASSERT_TRUE(opts.cfg != NULL);
	ASSERT_TRUE(opts.is_action);

	const struct alloy_mouse_config *m = alloy_mouse_cfg_c(opts.cfg);
	ASSERT_EQ(m->dpi[0][0], 1200);
	ASSERT_EQ(m->acceleration, 30);
	ASSERT_EQ(m->deceleration, 10);
	ASSERT_EQ(m->angle_snapping, 12);

	alloy_cli_free(&opts);
}

ALLOY_TEST(test_cli_bind_keyboard_options)
{
	const struct alloy_driver *drv = alloy_driver_find(0x1038, 0x160E);
	ASSERT_TRUE(drv != NULL);

	char *argv[] = {
		"alloyctl", "--brightness", "75", "--polling", "500",
	};
	int argc = (int)ALLOY_ARRAY_SIZE(argv);
	struct alloy_cli_opts opts;
	char err[128] = "";

	ASSERT_EQ(alloy_cli_parse(argc, argv, &opts, err, sizeof(err)), 0);
	ASSERT_EQ(opts.num_args, 2);

	ASSERT_EQ(alloy_cli_bind(drv, &opts, err, sizeof(err)), 0);
	ASSERT_TRUE(opts.cfg != NULL);
	ASSERT_TRUE(opts.is_action);

	const struct alloy_keyboard_config *k = alloy_kbd_cfg_c(opts.cfg);
	ASSERT_EQ(k->dev.brightness, 75);
	ASSERT_EQ(k->dev.polling_hz, 500);

	alloy_cli_free(&opts);
}

ALLOY_TEST(test_cli_bind_rejects_unsupported_options)
{
	const struct alloy_driver *kbd = alloy_driver_find(0x1038, 0x160E);
	const struct alloy_driver *mouse = alloy_driver_find(0x1038, 0x1824);
	struct alloy_cli_opts opts;
	char err[128] = "";

	/* --dpi on Apex 100 keyboard must fail */
	char *argv1[] = { "alloyctl", "--dpi", "800" };
	ASSERT_EQ(alloy_cli_parse(3, argv1, &opts, err, sizeof(err)), 0);
	ASSERT_EQ(alloy_cli_bind(kbd, &opts, err, sizeof(err)), -1);
	ASSERT_TRUE(strstr(err, "--dpi is not supported by") != NULL);
	alloy_cli_free(&opts);

	/* --win-lock on mouse must fail */
	char *argv2[] = { "alloyctl", "--win-lock", "on" };
	ASSERT_EQ(alloy_cli_parse(3, argv2, &opts, err, sizeof(err)), 0);
	ASSERT_EQ(alloy_cli_bind(mouse, &opts, err, sizeof(err)), -1);
	ASSERT_TRUE(strstr(err, "--win-lock is not supported by") != NULL);
	alloy_cli_free(&opts);
}

ALLOY_TEST(test_cli_parse_errors)
{
	struct alloy_cli_opts opts;
	char err[128] = "";

	char *missing_val[] = { "alloyctl", "--brightness" };
	ASSERT_EQ(alloy_cli_parse(2, missing_val, &opts, err, sizeof(err)), -1);

	char *unknown_flag[] = { "alloyctl", "--non-existent-flag-xyz" };
	ASSERT_EQ(alloy_cli_parse(2, unknown_flag, &opts, err, sizeof(err)),
		  -1);
}
