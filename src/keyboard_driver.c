/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *	- Keyboard driver defaults,
 *	- HID key tables,
 *	- Utilities
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "driver.h"
#include "keyboard_driver.h"

static const struct {
	uint8_t code;
	const char *name;
} hid_key_table[] = {
	{ 0x04, "A" },	   { 0x05, "B" },      { 0x06, "C" },
	{ 0x07, "D" },	   { 0x08, "E" },      { 0x09, "F" },
	{ 0x0A, "G" },	   { 0x0B, "H" },      { 0x0C, "I" },
	{ 0x0D, "J" },	   { 0x0E, "K" },      { 0x0F, "L" },
	{ 0x10, "M" },	   { 0x11, "N" },      { 0x12, "O" },
	{ 0x13, "P" },	   { 0x14, "Q" },      { 0x15, "R" },
	{ 0x16, "S" },	   { 0x17, "T" },      { 0x18, "U" },
	{ 0x19, "V" },	   { 0x1A, "W" },      { 0x1B, "X" },
	{ 0x1C, "Y" },	   { 0x1D, "Z" },      { 0x1E, "1" },
	{ 0x1F, "2" },	   { 0x20, "3" },      { 0x21, "4" },
	{ 0x22, "5" },	   { 0x23, "6" },      { 0x24, "7" },
	{ 0x25, "8" },	   { 0x26, "9" },      { 0x27, "0" },
	{ 0x28, "Enter" }, { 0x29, "Esc" },    { 0x2A, "Bcksp" },
	{ 0x2B, "Tab" },   { 0x2C, "Space" },  { 0x4F, "Right" },
	{ 0x50, "Left" },  { 0x51, "Down" },   { 0x52, "Up" },
	{ 0xE0, "LCtrl" }, { 0xE1, "LShift" }, { 0xE2, "LAlt" },
	{ 0xE3, "LMeta" }, { 0xE4, "RCtrl" },  { 0xE5, "RShift" },
	{ 0xE6, "RAlt" },  { 0xE7, "RMeta" },
};

const char *alloy_hid_key_name(uint8_t hid_code)
{
	static char hex_buf[8];
	for (size_t i = 0; i < ALLOY_ARRAY_SIZE(hid_key_table); i++) {
		if (hid_key_table[i].code == hid_code)
			return hid_key_table[i].name;
	}
	snprintf(hex_buf, sizeof(hex_buf), "0x%02X", hid_code);
	return hex_buf;
}

uint8_t alloy_hid_key_from_name(const char *name)
{
	if (!name || !*name)
		return 0;
	for (size_t i = 0; i < ALLOY_ARRAY_SIZE(hid_key_table); i++) {
		if (!strcasecmp(hid_key_table[i].name, name))
			return hid_key_table[i].code;
	}
	if (!strncasecmp(name, "0x", 2))
		return (uint8_t)strtoul(name, NULL, 16);
	return (uint8_t)strtoul(name, NULL, 10);
}

void alloy_config_keyboard_defaults(const struct alloy_driver *drv,
				    struct alloy_config *cfg)
{
	memset(cfg, 0, sizeof(*cfg));
	alloy_config_common_defaults(drv, &cfg->common);

	cfg->kbd.win_lock = 0;
	cfg->kbd.snap_tap = 0;
	cfg->kbd.snap_tap_group_count = 1;
	cfg->kbd.snap_tap_groups[0].mode = 0; /* Last input */
	cfg->kbd.snap_tap_groups[0].key1 = 0x04; /* 'A' */
	cfg->kbd.snap_tap_groups[0].key2 = 0x07; /* 'D' */
	cfg->kbd.profile_active = 1;
	cfg->kbd.profile_count =
		(drv && drv->num_profiles) ? drv->num_profiles : 1;
}

#include "cli.h"

static int parse_bool_val(const char *str, uint8_t *out)
{
	if (!str || !*str)
		return -1;
	if (!strcasecmp(str, "1") || !strcasecmp(str, "on") ||
	    !strcasecmp(str, "true") || !strcasecmp(str, "yes")) {
		*out = 1;
		return 0;
	}
	if (!strcasecmp(str, "0") || !strcasecmp(str, "off") ||
	    !strcasecmp(str, "false") || !strcasecmp(str, "no")) {
		*out = 0;
		return 0;
	}
	return -1;
}

static int opt_parse_win_lock(const char *arg, struct alloy_config *cfg,
			      char *err_buf, size_t err_len)
{
	uint8_t bool_val = 1;
	(void)err_buf;
	(void)err_len;

	if (arg && !parse_bool_val(arg, &bool_val)) {
		/* parsed successfully */
	}
	cfg->kbd.win_lock = bool_val;
	return 0;
}

static int opt_apply_win_lock(struct alloy_device *dev,
			      const struct alloy_config *cfg)
{
	if (dev->drv->ops && dev->drv->ops->apply_win_lock)
		return dev->drv->ops->apply_win_lock(dev, cfg);
	return 0;
}

static int opt_parse_snap_tap(const char *arg, struct alloy_config *cfg,
			      char *err_buf, size_t err_len)
{
	uint8_t bool_val = 1;
	(void)err_buf;
	(void)err_len;

	if (arg && !parse_bool_val(arg, &bool_val)) {
		/* parsed successfully */
	}
	cfg->kbd.snap_tap = bool_val;
	return 0;
}

static int opt_apply_snap_tap(struct alloy_device *dev,
			      const struct alloy_config *cfg)
{
	if (dev->drv->ops && dev->drv->ops->apply_snap_tap)
		return dev->drv->ops->apply_snap_tap(dev, cfg);
	return 0;
}

static int opt_parse_profile(const char *arg, struct alloy_config *cfg,
			     char *err_buf, size_t err_len)
{
	int p;
	if (!arg || sscanf(arg, "%d", &p) != 1 || p < 1 || p > 3) {
		snprintf(err_buf, err_len,
			 "--profile requires a profile index (1-3)");
		return -1;
	}
	cfg->kbd.profile_active = (uint8_t)p;
	return 0;
}

static int opt_validate_profile(const struct alloy_driver *drv,
				const struct alloy_config *cfg, char *err_buf,
				size_t err_len)
{
	if (drv->num_profiles > 0 &&
	    cfg->kbd.profile_active > drv->num_profiles) {
		snprintf(err_buf, err_len,
			 "profile %u out of range [1, %u] for '%s'",
			 cfg->kbd.profile_active, drv->num_profiles, drv->name);
		return -1;
	}
	return 0;
}

static int opt_apply_profile(struct alloy_device *dev,
			     const struct alloy_config *cfg)
{
	if (dev->drv->ops && dev->drv->ops->apply_profile)
		return dev->drv->ops->apply_profile(dev, cfg);
	return 0;
}

const struct alloy_cli_option alloy_keyboard_cli_options[] = {
	{
		.name = "--meta-lock",
		.alias = "--win-lock",
		.arg_desc = "[on|off]",
		.help = "Toggle Windows/Meta key lock",
		.category = ALLOY_OPT_KEYBOARD,
		.required_cap = ALLOY_CAP_WIN_LOCK,
		.has_arg = 2,
		.parse = opt_parse_win_lock,
		.apply = opt_apply_win_lock,
	},
	{
		.name = "--snap-tap",
		.arg_desc = "[on|off]",
		.help = "Toggle hardware Snap Tap / SOCD counter-strafing",
		.category = ALLOY_OPT_KEYBOARD,
		.required_cap = ALLOY_CAP_SNAP_TAP,
		.has_arg = 2,
		.parse = opt_parse_snap_tap,
		.apply = opt_apply_snap_tap,
	},
	{
		.name = "--profile",
		.arg_desc = "<1-3>",
		.help = "Switch active hardware onboard profile",
		.category = ALLOY_OPT_KEYBOARD,
		.required_cap = ALLOY_CAP_PROFILE,
		.has_arg = 1,
		.parse = opt_parse_profile,
		.validate = opt_validate_profile,
		.apply = opt_apply_profile,
	},
};

const size_t alloy_num_keyboard_cli_options =
	ALLOY_ARRAY_SIZE(alloy_keyboard_cli_options);
