/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Shared front-end for keyboards.
 *
 * Driver-side code: this is where "a keyboard has a backlight, a report rate,
 * Snap Tap groups and onboard profiles" is written down, as generic controls
 * the front-end can draw without knowing any of those words.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "lib/keyboard.h"
#include "lib/light.h"
#include "lib/widgets.h"

/* pane ids */
enum {
	KP_CENTER = 1,
	KP_CONTROLS,
};

/* item ids inside CONTROLS */
enum {
	KI_SNAP_TAP = 1,
	KI_GROUP_KEYS,
	KI_GROUP_MODE,
	KI_GROUP_ADD,
	KI_GROUP_DEL,
	KI_PROFILE,
};

static const char *const snap_modes[] = { "Last Input", "Key 1", "Key 2",
					  "Neutral" };

/*
 * Key pairs Snap Tap is worth arming on.
 * Counter-strafing wants opposed keys, so the list is pairs rather than two
 * independent pickers; hand-edited configs with other codes still display fine
 * and simply start the cycle at the first entry.
 */
static const uint8_t snap_tap_pairs[][2] = {
	{ 0x04, 0x07 }, /* A / D */
	{ 0x1A, 0x16 }, /* W / S */
	{ 0x14, 0x08 }, /* Q / E */
	{ 0x50, 0x4F }, /* Left / Right */
	{ 0x52, 0x51 }, /* Up / Down */
};

static void snap_tap_push(struct alloy_ui *ui)
{
	alloy_ui_changed(ui, ALLOY_STEP_SNAP_TAP);
}

static int snap_get(struct alloy_ui *ui, const struct alloy_ui_item *it)
{
	(void)it;
	return alloy_kbd_cfg(alloy_ui_config(ui))->snap_tap;
}

static void snap_set(struct alloy_ui *ui, const struct alloy_ui_item *it,
		     int val)
{
	(void)it;
	alloy_kbd_cfg(alloy_ui_config(ui))->snap_tap = (uint8_t)(val ? 1 : 0);
}

static void snap_changed(struct alloy_ui *ui, const struct alloy_ui_item *it)
{
	(void)it;
	snap_tap_push(ui);
	alloy_ui_status(ui, "Snap Tap %s",
			alloy_kbd_cfg(alloy_ui_config(ui))->snap_tap ? "ON" :
								       "OFF");
}

static int keys_get(struct alloy_ui *ui, const struct alloy_ui_item *it)
{
	const struct alloy_snap_tap_group *g =
		&alloy_kbd_cfg(alloy_ui_config(ui))->snap_tap_groups[it->idx];
	size_t i;

	for (i = 0; i < ALLOY_ARRAY_SIZE(snap_tap_pairs); i++) {
		if (snap_tap_pairs[i][0] == g->key1 &&
		    snap_tap_pairs[i][1] == g->key2)
			return (int)i;
	}
	return 0;
}

static void keys_set(struct alloy_ui *ui, const struct alloy_ui_item *it,
		     int val)
{
	struct alloy_snap_tap_group *g =
		&alloy_kbd_cfg(alloy_ui_config(ui))->snap_tap_groups[it->idx];
	int idx =
		ALLOY_CLAMP(val, 0, (int)ALLOY_ARRAY_SIZE(snap_tap_pairs) - 1);

	g->key1 = snap_tap_pairs[idx][0];
	g->key2 = snap_tap_pairs[idx][1];
}

static void keys_text(struct alloy_ui *ui, const struct alloy_ui_item *it,
		      char *buf, size_t len)
{
	const struct alloy_snap_tap_group *g =
		&alloy_kbd_cfg(alloy_ui_config(ui))->snap_tap_groups[it->idx];

	snprintf(buf, len, "%s / %s", alloy_hid_key_name(g->key1),
		 alloy_hid_key_name(g->key2));
}

static void keys_changed(struct alloy_ui *ui, const struct alloy_ui_item *it)
{
	char buf[32];

	keys_text(ui, it, buf, sizeof(buf));
	snap_tap_push(ui);
	alloy_ui_status(ui, "Group %d Keys: %s", it->idx + 1, buf);
}

static int mode_get(struct alloy_ui *ui, const struct alloy_ui_item *it)
{
	return alloy_kbd_cfg(alloy_ui_config(ui))->snap_tap_groups[it->idx].mode;
}

static void mode_set(struct alloy_ui *ui, const struct alloy_ui_item *it,
		     int val)
{
	alloy_kbd_cfg(alloy_ui_config(ui))->snap_tap_groups[it->idx].mode =
		(uint8_t)ALLOY_CLAMP(val, 0,
				     (int)ALLOY_ARRAY_SIZE(snap_modes) - 1);
}

static void mode_changed(struct alloy_ui *ui, const struct alloy_ui_item *it)
{
	snap_tap_push(ui);
	alloy_ui_status(ui, "Group %d Mode: %s", it->idx + 1,
			snap_modes[mode_get(ui, it)]);
}

static void group_add(struct alloy_ui *ui, const struct alloy_ui_item *it)
{
	struct alloy_keyboard_config *k = alloy_kbd_cfg(alloy_ui_config(ui));
	uint8_t gc = k->snap_tap_group_count;

	(void)it;
	if (gc >= ALLOY_MAX_SNAP_TAP_GROUPS)
		return;

	k->snap_tap_groups[gc].mode = 0;
	k->snap_tap_groups[gc].key1 =
		snap_tap_pairs[gc % ALLOY_ARRAY_SIZE(snap_tap_pairs)][0];
	k->snap_tap_groups[gc].key2 =
		snap_tap_pairs[gc % ALLOY_ARRAY_SIZE(snap_tap_pairs)][1];
	k->snap_tap_group_count++;

	snap_tap_push(ui);
	alloy_ui_status(ui, "Added Snap Tap Group %u", k->snap_tap_group_count);
}

static void group_del(struct alloy_ui *ui, const struct alloy_ui_item *it)
{
	struct alloy_keyboard_config *k = alloy_kbd_cfg(alloy_ui_config(ui));

	(void)it;
	if (k->snap_tap_group_count <= 1)
		return;
	k->snap_tap_group_count--;
	snap_tap_push(ui);
	alloy_ui_status(ui, "Removed Snap Tap Group");
}

static int profile_get(struct alloy_ui *ui, const struct alloy_ui_item *it)
{
	(void)it;
	return alloy_kbd_cfg(alloy_ui_config(ui))->profile_active;
}

static void profile_set(struct alloy_ui *ui, const struct alloy_ui_item *it,
			int val)
{
	(void)it;
	alloy_kbd_cfg(alloy_ui_config(ui))->profile_active = (uint8_t)val;
}

static void profile_text(struct alloy_ui *ui, const struct alloy_ui_item *it,
			 char *buf, size_t len)
{
	(void)it;
	snprintf(buf, len, "Profile %u",
		 alloy_kbd_cfg(alloy_ui_config(ui))->profile_active);
}

static void profile_changed(struct alloy_ui *ui, const struct alloy_ui_item *it)
{
	(void)it;
	alloy_ui_changed(ui, ALLOY_STEP_PROFILE);
	alloy_ui_status(ui, "Profile %u active",
			alloy_kbd_cfg(alloy_ui_config(ui))->profile_active);
}

static size_t controls_items(struct alloy_ui *ui, struct alloy_ui_item *out,
			     size_t max)
{
	const struct alloy_driver *drv = alloy_ui_driver(ui);
	const struct alloy_keyboard_config *k =
		alloy_kbd_cfg(alloy_ui_config(ui));
	const struct alloy_keyboard_info *ki = alloy_keyboard_info(drv);
	uint8_t profiles = (ki && ki->num_profiles) ? ki->num_profiles : 1;
	size_t n = 0;
	uint8_t g;

#define PUSH(...)                                                         \
	do {                                                              \
		if (n < max)                                              \
			out[n++] = (struct alloy_ui_item){ __VA_ARGS__ }; \
	} while (0)

	if (alloy_devinfo(drv)->caps & ALLOY_CAP_BRIGHTNESS) {
		PUSH(.label = "BACKLIGHT", .kind = ALLOY_UI_HEADING);
		n += alloy_widget_brightness(ui, out + n, max - n);
		PUSH(.kind = ALLOY_UI_SPACER);
	}

	n += alloy_widget_polling(ui, out + n, max - n);

	if (alloy_devinfo(drv)->caps & ALLOY_CAP_SNAP_TAP) {
		PUSH(.kind = ALLOY_UI_SPACER);
		PUSH(.label = "SNAP TAP (SOCD)", .kind = ALLOY_UI_HEADING);
		PUSH(.label = "Snap Tap", .kind = ALLOY_UI_TOGGLE,
		     .id = KI_SNAP_TAP, .get = snap_get, .set = snap_set,
		     .changed = snap_changed);

		for (g = 0; g < k->snap_tap_group_count &&
			    g < ALLOY_MAX_SNAP_TAP_GROUPS;
		     g++) {
			static char keys_label[ALLOY_MAX_SNAP_TAP_GROUPS][16];
			static char mode_label[ALLOY_MAX_SNAP_TAP_GROUPS][16];

			snprintf(keys_label[g], sizeof(keys_label[g]),
				 "Grp %u Keys", g + 1);
			snprintf(mode_label[g], sizeof(mode_label[g]),
				 "Grp %u Mode", g + 1);

			PUSH(.label = keys_label[g], .kind = ALLOY_UI_CHOICE,
			     .id = KI_GROUP_KEYS, .idx = g,
			     .num_choices = ALLOY_ARRAY_SIZE(snap_tap_pairs),
			     .get = keys_get, .set = keys_set,
			     .text = keys_text, .changed = keys_changed);
			PUSH(.label = mode_label[g], .kind = ALLOY_UI_CHOICE,
			     .id = KI_GROUP_MODE, .idx = g,
			     .choices = snap_modes,
			     .num_choices = ALLOY_ARRAY_SIZE(snap_modes),
			     .get = mode_get, .set = mode_set,
			     .changed = mode_changed);
		}

		if (k->snap_tap_group_count < ALLOY_MAX_SNAP_TAP_GROUPS)
			PUSH(.label = "+ Add Group", .kind = ALLOY_UI_BUTTON,
			     .id = KI_GROUP_ADD, .activate = group_add);
		if (k->snap_tap_group_count > 1)
			PUSH(.label = "- Remove Last Group",
			     .kind = ALLOY_UI_BUTTON, .id = KI_GROUP_DEL,
			     .activate = group_del);
	}

	if (alloy_devinfo(drv)->caps & ALLOY_CAP_PROFILE) {
		PUSH(.kind = ALLOY_UI_SPACER);
		PUSH(.label = "HARDWARE PROFILE", .kind = ALLOY_UI_HEADING);
		PUSH(.label = "Profile", .kind = ALLOY_UI_STEPPER,
		     .id = KI_PROFILE, .min_val = 1, .max_val = profiles,
		     .get = profile_get, .set = profile_set,
		     .text = profile_text, .changed = profile_changed);
	}
#undef PUSH
	return n;
}

static size_t keyboard_items(struct alloy_ui *ui,
			     const struct alloy_ui_pane *pane,
			     struct alloy_ui_item *out, size_t max)
{
	switch (pane->id) {
	case KP_CENTER:
		return alloy_light_gateway(ui, out);
	case KP_CONTROLS:
		return controls_items(ui, out, max);
	default:
		return alloy_light_items(ui, pane, out, max);
	}
}

/*
 * Snap Tap or profile shortcut pressed on the keyboard itself is the user
 * acting on the device, not a pending edit, so it lands in the baseline too.
 */
static void keyboard_event(struct alloy_ui *ui, struct alloy_config *baseline)
{
	const struct alloy_driver *drv = alloy_ui_driver(ui);
	const struct alloy_keyboard_config *k =
		alloy_kbd_cfg_c(alloy_ui_config(ui));

	alloy_kbd_cfg(baseline)->snap_tap = k->snap_tap;
	alloy_kbd_cfg(baseline)->profile_active = k->profile_active;

	if (alloy_devinfo(drv)->caps & ALLOY_CAP_SNAP_TAP)
		alloy_ui_status(ui, "Snap Tap %s (keyboard shortcut)",
				k->snap_tap ? "ON" : "OFF");
	if (alloy_devinfo(drv)->caps & ALLOY_CAP_PROFILE)
		alloy_ui_status(ui, "Profile %u active", k->profile_active);
}

static const char *center_title(struct alloy_ui *ui,
				const struct alloy_ui_pane *pane)
{
	(void)pane;
	return alloy_ui_driver(ui)->name;
}

static const struct alloy_ui_pane keyboard_main_panes[] = {
	{
		.dyn_title = center_title,
		.id = KP_CENTER,
		.col = 0,
		.flags = ALLOY_UI_PANE_ART,
	},
	{
		.title = "CONTROLS",
		.id = KP_CONTROLS,
		.col = 1,
		.width_pct = 28,
		.min_width = 26,
		.max_width = 34,
		.hint = "h/l: Adjust  Enter: Select",
	},
};

static const struct alloy_ui_screen keyboard_screens[] = {
	{
		.name = "MAIN",
		.id = ALLOY_SCREEN_MAIN,
		.panes = keyboard_main_panes,
		.num_panes = ALLOY_ARRAY_SIZE(keyboard_main_panes),
		.hint = "Tab: Pane  Enter: Select  s: Save  q: Quit",
	},
	{
		.name = "ILLUMINATION",
		.id = ALLOY_SCREEN_LIGHT,
		.panes = alloy_light_panes,
		.num_panes = ALLOY_ARRAY_SIZE(alloy_light_panes),
		.hint = "Tab: Pane  Enter: Edit zone  s: Save  Esc: Back",
	},
};

const struct alloy_ui_desc alloy_keyboard_ui = {
	.screens = keyboard_screens,
	.num_screens = ALLOY_ARRAY_SIZE(keyboard_screens),
	.items = keyboard_items,
	.art_cell = alloy_light_art_cell,
	.event = keyboard_event,
};

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
	size_t i;

	for (i = 0; i < ALLOY_ARRAY_SIZE(hid_key_table); i++) {
		if (hid_key_table[i].code == hid_code)
			return hid_key_table[i].name;
	}
	snprintf(hex_buf, sizeof(hex_buf), "0x%02X", hid_code);
	return hex_buf;
}

uint8_t alloy_hid_key_from_name(const char *name)
{
	size_t i;

	if (!name || !*name)
		return 0;
	for (i = 0; i < ALLOY_ARRAY_SIZE(hid_key_table); i++) {
		if (!strcasecmp(hid_key_table[i].name, name))
			return hid_key_table[i].code;
	}
	if (!strncasecmp(name, "0x", 2))
		return (uint8_t)strtoul(name, NULL, 16);
	return (uint8_t)strtoul(name, NULL, 10);
}

void alloy_keyboard_defaults(const struct alloy_driver *drv,
			     struct alloy_config *cfg)
{
	struct alloy_keyboard_config *k = alloy_kbd_cfg(cfg);

	alloy_devcfg_defaults(drv, cfg);

	k->snap_tap_group_count = 1;
	k->snap_tap_groups[0].mode = 0; /* last input */
	k->snap_tap_groups[0].key1 = 0x04; /* A */
	k->snap_tap_groups[0].key2 = 0x07; /* D */
	k->profile_active = 1;
}

void alloy_keyboard_state_save(const struct alloy_driver *drv,
			       const struct alloy_config *cfg, void *ctx,
			       alloy_state_emit_fn emit)
{
	const struct alloy_devinfo *info = alloy_devinfo(drv);
	const struct alloy_keyboard_config *k = alloy_kbd_cfg_c(cfg);
	char key[48];
	char val[48];
	uint8_t i;

	alloy_devcfg_state_save(drv, cfg, ctx, emit);
	if (!info)
		return;

	if (info->caps & ALLOY_CAP_WIN_LOCK) {
		snprintf(val, sizeof(val), "%u", k->win_lock ? 1 : 0);
		emit(ctx, "kbd.win_lock", val);
	}
	if (info->caps & ALLOY_CAP_SNAP_TAP) {
		snprintf(val, sizeof(val), "%u", k->snap_tap ? 1 : 0);
		emit(ctx, "kbd.snap_tap", val);
		snprintf(val, sizeof(val), "%u", k->snap_tap_group_count);
		emit(ctx, "kbd.snap_tap_groups", val);
		for (i = 0; i < k->snap_tap_group_count &&
			    i < ALLOY_MAX_SNAP_TAP_GROUPS;
		     i++) {
			snprintf(key, sizeof(key), "kbd.snap_tap%u", i);
			snprintf(val, sizeof(val), "%u:%02x:%02x",
				 k->snap_tap_groups[i].mode,
				 k->snap_tap_groups[i].key1,
				 k->snap_tap_groups[i].key2);
			emit(ctx, key, val);
		}
	}
	if (info->caps & ALLOY_CAP_PROFILE) {
		snprintf(val, sizeof(val), "%u", k->profile_active);
		emit(ctx, "kbd.profile_active", val);
	}
}

int alloy_keyboard_state_load(const struct alloy_driver *drv,
			      struct alloy_config *cfg, const char *key,
			      const char *val)
{
	struct alloy_keyboard_config *k = alloy_kbd_cfg(cfg);
	unsigned idx;

	if (alloy_devcfg_state_load(drv, cfg, key, val))
		return 1;
	if (strncmp(key, "kbd.", 4))
		return 0;
	key += 4;

	if (!strcmp(key, "win_lock")) {
		k->win_lock = atoi(val) ? 1 : 0;
	} else if (!strcmp(key, "snap_tap")) {
		k->snap_tap = atoi(val) ? 1 : 0;
	} else if (!strcmp(key, "snap_tap_groups")) {
		k->snap_tap_group_count = (uint8_t)ALLOY_CLAMP(
			atoi(val), 1, ALLOY_MAX_SNAP_TAP_GROUPS);
	} else if (sscanf(key, "snap_tap%u", &idx) == 1 &&
		   idx < ALLOY_MAX_SNAP_TAP_GROUPS) {
		unsigned mode = 0;
		unsigned k1 = 0x04;
		unsigned k2 = 0x07;

		if (sscanf(val, "%u:%x:%x", &mode, &k1, &k2) == 3) {
			k->snap_tap_groups[idx].mode = (uint8_t)mode;
			k->snap_tap_groups[idx].key1 = (uint8_t)k1;
			k->snap_tap_groups[idx].key2 = (uint8_t)k2;
			if (idx >= k->snap_tap_group_count)
				k->snap_tap_group_count = (uint8_t)(idx + 1);
		}
	} else if (!strcmp(key, "profile_active")) {
		k->profile_active = (uint8_t)ALLOY_MAX(atoi(val), 1);
	} else {
		return 0;
	}
	return 1;
}

void alloy_keyboard_state_done(const struct alloy_driver *drv,
			       struct alloy_config *cfg)
{
	struct alloy_keyboard_config *k = alloy_kbd_cfg(cfg);

	alloy_light_normalize(drv, cfg);
	if (!k->snap_tap_group_count)
		k->snap_tap_group_count = 1;
	if (!k->profile_active)
		k->profile_active = 1;
}

static int kbd_has_cap(const struct alloy_driver *drv, uint64_t cap)
{
	const struct alloy_devinfo *info = alloy_devinfo(drv);

	return info && (info->caps & cap);
}

static int avail_win_lock(const struct alloy_driver *drv)
{
	return kbd_has_cap(drv, ALLOY_CAP_WIN_LOCK);
}

static int avail_snap_tap(const struct alloy_driver *drv)
{
	return kbd_has_cap(drv, ALLOY_CAP_SNAP_TAP);
}

static int avail_profile(const struct alloy_driver *drv)
{
	return kbd_has_cap(drv, ALLOY_CAP_PROFILE);
}

static int parse_onoff(const char *arg, uint8_t *out)
{
	if (!arg || !*arg)
		return -1;
	if (!strcasecmp(arg, "1") || !strcasecmp(arg, "on") ||
	    !strcasecmp(arg, "true") || !strcasecmp(arg, "yes")) {
		*out = 1;
		return 0;
	}
	if (!strcasecmp(arg, "0") || !strcasecmp(arg, "off") ||
	    !strcasecmp(arg, "false") || !strcasecmp(arg, "no")) {
		*out = 0;
		return 0;
	}
	return -1;
}

static int opt_parse_win_lock(const struct alloy_driver *drv, const char *arg,
			      struct alloy_config *cfg, char *err,
			      size_t err_len)
{
	uint8_t on = 1;

	(void)drv;
	(void)err;
	(void)err_len;
	if (arg)
		parse_onoff(arg, &on);
	alloy_kbd_cfg(cfg)->win_lock = on;
	return 0;
}

static int opt_parse_snap_tap(const struct alloy_driver *drv, const char *arg,
			      struct alloy_config *cfg, char *err,
			      size_t err_len)
{
	uint8_t on = 1;

	(void)drv;
	(void)err;
	(void)err_len;
	if (arg)
		parse_onoff(arg, &on);
	alloy_kbd_cfg(cfg)->snap_tap = on;
	return 0;
}

static int opt_parse_profile(const struct alloy_driver *drv, const char *arg,
			     struct alloy_config *cfg, char *err,
			     size_t err_len)
{
	const struct alloy_keyboard_info *ki = alloy_keyboard_info(drv);
	uint8_t max = (ki && ki->num_profiles) ? ki->num_profiles : 1;
	int p;

	if (!arg || sscanf(arg, "%d", &p) != 1 || p < 1 || p > max) {
		snprintf(err, err_len,
			 "--profile requires a profile index (1-%u)", max);
		return -1;
	}
	alloy_kbd_cfg(cfg)->profile_active = (uint8_t)p;
	return 0;
}

const struct alloy_cli_option alloy_keyboard_cli_options[] = {
	{
		.name = "--meta-lock",
		.alias = "--win-lock",
		.arg_desc = "[on|off]",
		.help = "Toggle the Windows/Meta key lock",
		.has_arg = 2,
		.available = avail_win_lock,
		.parse = opt_parse_win_lock,
		.apply_step = ALLOY_STEP_WIN_LOCK,
	},
	{
		.name = "--snap-tap",
		.arg_desc = "[on|off]",
		.help = "Toggle hardware Snap Tap / SOCD counter-strafing",
		.has_arg = 2,
		.available = avail_snap_tap,
		.parse = opt_parse_snap_tap,
		.apply_step = ALLOY_STEP_SNAP_TAP,
	},
	{
		.name = "--profile",
		.arg_desc = "<n>",
		.help = "Switch the active onboard profile",
		.has_arg = 1,
		.available = avail_profile,
		.parse = opt_parse_profile,
		.apply_step = ALLOY_STEP_PROFILE,
	},
};

_Static_assert(ALLOY_ARRAY_SIZE(alloy_keyboard_cli_options) ==
		       ALLOY_KEYBOARD_CLI_COUNT,
	       "ALLOY_KEYBOARD_CLI_COUNT is out of step with the table");
