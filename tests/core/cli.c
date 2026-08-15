// SPDX-License-Identifier: GPL-2.0-only
/*
 * Unit tests for declarative CLI argument parsing and device-type validation.
 */
#include <string.h>

#include "cli.h"
#include "driver.h"
#include "keyboard_driver.h"
#include "mouse_driver.h"
#include "test.h"

ALLOY_TEST(test_cli_parse_common_options)
{
	char *argv[] = {
		"alloyctl", "--device",	 "1038:1234", "--brightness",
		"75",	    "--polling", "500",	      "--color",
		"0:00FF88", "--fx",	 "steady",    "--save",
	};
	int argc = (int)ALLOY_ARRAY_SIZE(argv);
	struct alloy_cli_opts opts;
	char err[128] = "";

	ASSERT_EQ(alloy_cli_parse(argc, argv, &opts, err, sizeof(err)), 0);
	ASSERT_TRUE(opts.has_device);
	ASSERT_EQ(opts.vid, 0x1038);
	ASSERT_EQ(opts.pid, 0x1234);
	ASSERT_EQ(opts.cfg.common.brightness, 75);
	ASSERT_EQ(opts.cfg.common.polling_hz, 500);
	ASSERT_EQ(opts.cfg.common.zone_color[0].r, 0x00);
	ASSERT_EQ(opts.cfg.common.zone_color[0].g, 0xFF);
	ASSERT_EQ(opts.cfg.common.zone_color[0].b, 0x88);
	ASSERT_EQ(opts.cfg.common.zone_fx[0], 0);
	ASSERT_TRUE(opts.save_flash);
	ASSERT_TRUE(opts.is_action);
	ASSERT_TRUE(opts.num_active_opts >= 4);
}

ALLOY_TEST(test_cli_parse_mouse_options)
{
	char *argv[] = {
		"alloyctl", "--dpi", "1200",   "--accel", "30",
		"--decel",  "10",    "--snap", "12",	  "--high-efficiency",
		"on",
	};
	int argc = (int)ALLOY_ARRAY_SIZE(argv);
	struct alloy_cli_opts opts;
	char err[128] = "";

	ASSERT_EQ(alloy_cli_parse(argc, argv, &opts, err, sizeof(err)), 0);
	ASSERT_EQ(opts.cfg.mouse.dpi[0][0], 1200);
	ASSERT_EQ(opts.cfg.mouse.acceleration, 30);
	ASSERT_EQ(opts.cfg.mouse.deceleration, 10);
	ASSERT_EQ(opts.cfg.mouse.angle_snapping, 12);
	ASSERT_EQ(opts.cfg.mouse.high_efficiency, 1);
	ASSERT_TRUE(opts.is_action);
	ASSERT_TRUE(opts.num_active_opts == 5);
}

ALLOY_TEST(test_cli_parse_keyboard_options)
{
	char *argv[] = {
		"alloyctl", "--meta-lock", "on", "--snap-tap",
		"1",	    "--profile",   "2",
	};
	int argc = (int)ALLOY_ARRAY_SIZE(argv);
	struct alloy_cli_opts opts;
	char err[128] = "";

	ASSERT_EQ(alloy_cli_parse(argc, argv, &opts, err, sizeof(err)), 0);
	ASSERT_EQ(opts.cfg.kbd.win_lock, 1);
	ASSERT_EQ(opts.cfg.kbd.snap_tap, 1);
	ASSERT_EQ(opts.cfg.kbd.profile_active, 2);
	ASSERT_TRUE(opts.is_action);

	char *argv2[] = { "alloyctl", "--win-lock", "off", "--snap-tap",
			  "off" };
	int argc2 = (int)ALLOY_ARRAY_SIZE(argv2);
	ASSERT_EQ(alloy_cli_parse(argc2, argv2, &opts, err, sizeof(err)), 0);
	ASSERT_EQ(opts.cfg.kbd.win_lock, 0);
	ASSERT_EQ(opts.cfg.kbd.snap_tap, 0);
}

ALLOY_TEST(test_cli_validate_rejects_mouse_options_on_keyboard)
{
	struct alloy_driver kbd_drv = {
		.name = "Mock Keyboard",
		.type = ALLOY_DEV_KEYBOARD,
		.caps = ALLOY_CAP_WIN_LOCK | ALLOY_CAP_BRIGHTNESS |
			ALLOY_CAP_SNAP_TAP | ALLOY_CAP_PROFILE,
	};
	struct alloy_cli_opts opts;
	char err[128];

	/* --dpi on keyboard must fail */
	char *argv1[] = { "alloyctl", "--dpi", "800" };
	ASSERT_EQ(alloy_cli_parse(3, argv1, &opts, err, sizeof(err)), 0);
	ASSERT_EQ(alloy_cli_validate(&kbd_drv, &opts, err, sizeof(err)), -1);
	ASSERT_TRUE(strstr(err, "--dpi is not supported on keyboards") != NULL);

	/* --accel on keyboard must fail */
	char *argv2[] = { "alloyctl", "--accel", "50" };
	ASSERT_EQ(alloy_cli_parse(3, argv2, &opts, err, sizeof(err)), 0);
	ASSERT_EQ(alloy_cli_validate(&kbd_drv, &opts, err, sizeof(err)), -1);
	ASSERT_TRUE(strstr(err, "--accel is not supported on keyboards") !=
		    NULL);

	/* --high-efficiency on keyboard must fail */
	char *argv3[] = { "alloyctl", "--high-efficiency", "on" };
	ASSERT_EQ(alloy_cli_parse(3, argv3, &opts, err, sizeof(err)), 0);
	ASSERT_EQ(alloy_cli_validate(&kbd_drv, &opts, err, sizeof(err)), -1);
	ASSERT_TRUE(strstr(err, "--high-efficiency is not supported on") !=
		    NULL);

	/* keyboard options on keyboard must pass */
	char *argv4[] = { "alloyctl",	"--win-lock",	"on",
			  "--snap-tap", "on",		"--profile",
			  "2",		"--brightness", "50" };
	ASSERT_EQ(alloy_cli_parse(9, argv4, &opts, err, sizeof(err)), 0);
	ASSERT_EQ(alloy_cli_validate(&kbd_drv, &opts, err, sizeof(err)), 0);
}

ALLOY_TEST(test_cli_validate_rejects_keyboard_options_on_mouse)
{
	struct alloy_driver mouse_drv = {
		.name = "Mock Mouse",
		.type = ALLOY_DEV_MOUSE,
		.caps = ALLOY_CAP_BRIGHTNESS,
		.dpi = { .min = 100, .max = 12000 },
	};
	struct alloy_cli_opts opts;
	char err[128];

	/* --meta-lock on mouse must fail */
	char *argv1[] = { "alloyctl", "--meta-lock", "on" };
	ASSERT_EQ(alloy_cli_parse(3, argv1, &opts, err, sizeof(err)), 0);
	ASSERT_EQ(alloy_cli_validate(&mouse_drv, &opts, err, sizeof(err)), -1);
	ASSERT_TRUE(strstr(err, "--meta-lock is not supported on mice") !=
		    NULL);

	/* --snap-tap on mouse must fail */
	char *argv2[] = { "alloyctl", "--snap-tap", "on" };
	ASSERT_EQ(alloy_cli_parse(3, argv2, &opts, err, sizeof(err)), 0);
	ASSERT_EQ(alloy_cli_validate(&mouse_drv, &opts, err, sizeof(err)), -1);
	ASSERT_TRUE(strstr(err, "--snap-tap is not supported on mice") != NULL);

	/* mouse options on mouse must pass */
	char *argv3[] = { "alloyctl", "--dpi", "800", "--brightness", "100" };
	ASSERT_EQ(alloy_cli_parse(5, argv3, &opts, err, sizeof(err)), 0);
	ASSERT_EQ(alloy_cli_validate(&mouse_drv, &opts, err, sizeof(err)), 0);
}

ALLOY_TEST(test_cli_parse_errors)
{
	struct alloy_cli_opts opts;
	char err[128];

	char *missing_val[] = { "alloyctl", "--brightness" };
	ASSERT_EQ(alloy_cli_parse(2, missing_val, &opts, err, sizeof(err)), -1);

	char *invalid_color[] = { "alloyctl", "--color", "invalid" };
	ASSERT_EQ(alloy_cli_parse(3, invalid_color, &opts, err, sizeof(err)),
		  -1);

	char *unknown_flag[] = { "alloyctl", "--non-existent" };
	ASSERT_EQ(alloy_cli_parse(2, unknown_flag, &opts, err, sizeof(err)),
		  -1);
}

ALLOY_TEST(test_cli_parse_help)
{
	struct alloy_cli_opts opts;
	char err[128];

	char *help1[] = { "alloyctl", "--help" };
	ASSERT_EQ(alloy_cli_parse(2, help1, &opts, err, sizeof(err)), 0);
	ASSERT_TRUE(opts.show_help);

	char *help2[] = { "alloyctl", "-h" };
	ASSERT_EQ(alloy_cli_parse(2, help2, &opts, err, sizeof(err)), 0);
	ASSERT_TRUE(opts.show_help);
}

static int custom_opt_parse(const char *arg, struct alloy_config *cfg,
			    char *err_buf, size_t err_len)
{
	(void)arg;
	(void)err_buf;
	(void)err_len;
	cfg->common.zone_fx_multicolor[0] = 1;
	return 0;
}

ALLOY_TEST(test_cli_driver_specific_options)
{
	const struct alloy_cli_option custom_opts[] = {
		{
			.name = "--custom-flag",
			.help = "Custom driver-specific test flag",
			.category = ALLOY_OPT_DRIVER_SPECIFIC,
			.has_arg = 0,
			.parse = custom_opt_parse,
		},
	};
	struct alloy_driver drv_with_opt = {
		.name = "Driver With Opt",
		.type = ALLOY_DEV_KEYBOARD,
		.cli_options = custom_opts,
		.num_cli_options = 1,
	};
	struct alloy_driver drv_without_opt = {
		.name = "Driver Without Opt",
		.type = ALLOY_DEV_KEYBOARD,
		.cli_options = NULL,
		.num_cli_options = 0,
	};
	struct alloy_cli_opts opts;
	char err[128] = "";

	memset(&opts, 0, sizeof(opts));
	opts.active_opts[0] = &custom_opts[0];
	opts.num_active_opts = 1;

	ASSERT_EQ(alloy_cli_validate(&drv_with_opt, &opts, err, sizeof(err)),
		  0);
	ASSERT_EQ(alloy_cli_validate(&drv_without_opt, &opts, err, sizeof(err)),
		  -1);
	ASSERT_TRUE(strstr(err, "--custom-flag is not supported on") != NULL);
}
