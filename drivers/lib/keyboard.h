/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Shared front-end and settings for keyboards.
 *
 * Driver-side code layered on drivers/lib/devcfg.h, on the same terms as mouse.h:
 * a keyboard driver opts in by embedding struct alloy_keyboard_config
 * and publishing a struct alloy_keyboard_info.
 * A keyboard that works nothing like this writes its own front-end instead.
 */
#ifndef ALLOY_LIB_KEYBOARD_H
#define ALLOY_LIB_KEYBOARD_H

#include "lib/devcfg.h"
#include "lib/light.h"

/* Capabilities this layer claims */
#define ALLOY_KBD_CAP(n) (1ull << (ALLOY_CAP_KBD_BASE + (n)))

#define ALLOY_CAP_WIN_LOCK ALLOY_KBD_CAP(0) /* Windows / Meta key lock */
#define ALLOY_CAP_SNAP_TAP ALLOY_KBD_CAP(1) /* hardware SOCD counter-strafing */
#define ALLOY_CAP_PROFILE ALLOY_KBD_CAP(2) /* onboard hardware profiles */

/* Apply-step names this layer pushes */
#define ALLOY_STEP_WIN_LOCK "win-lock"
#define ALLOY_STEP_SNAP_TAP "snap-tap"
#define ALLOY_STEP_PROFILE "profile"

#define ALLOY_MAX_SNAP_TAP_GROUPS 10

struct alloy_snap_tap_group {
	uint8_t mode; /* 0: Last Input, 1: Key 1, 2: Key 2, 3: Neutral */
	uint8_t key1; /* HID keycode, e.g. 0x04 ('A') */
	uint8_t key2; /* HID keycode, e.g. 0x07 ('D') */
};

struct alloy_keyboard_config {
	struct alloy_devcfg dev; /* must be first */

	/* 0 = normal (Win key active), 1 = locked */
	uint8_t win_lock;

	uint8_t snap_tap;
	uint8_t snap_tap_group_count;
	struct alloy_snap_tap_group snap_tap_groups[ALLOY_MAX_SNAP_TAP_GROUPS];

	uint8_t profile_active; /* 1-based */
};

static inline struct alloy_keyboard_config *
alloy_kbd_cfg(struct alloy_config *cfg)
{
	return (struct alloy_keyboard_config *)alloy_config_data(cfg);
}

static inline const struct alloy_keyboard_config *
alloy_kbd_cfg_c(const struct alloy_config *cfg)
{
	return (const struct alloy_keyboard_config *)alloy_config_data_c(cfg);
}

/* pointed to by struct alloy_devinfo.ext */
struct alloy_keyboard_info {
	uint8_t num_profiles; /* onboard hardware profiles, 0 = none */
};

static inline const struct alloy_keyboard_info *
alloy_keyboard_info(const struct alloy_driver *drv)
{
	const struct alloy_devinfo *info = alloy_devinfo(drv);

	return info ? (const struct alloy_keyboard_info *)info->ext : NULL;
}

extern const struct alloy_ui_desc alloy_keyboard_ui;

void alloy_keyboard_defaults(const struct alloy_driver *drv,
			     struct alloy_config *cfg);
void alloy_keyboard_state_save(const struct alloy_driver *drv,
			       const struct alloy_config *cfg, void *ctx,
			       alloy_state_emit_fn emit);
int alloy_keyboard_state_load(const struct alloy_driver *drv,
			      struct alloy_config *cfg, const char *key,
			      const char *val);
void alloy_keyboard_state_done(const struct alloy_driver *drv,
			       struct alloy_config *cfg);

/* flag group this layer contributes; see devcfg.h for how to splice it in */
extern const struct alloy_cli_option alloy_keyboard_cli_options[];
#define ALLOY_KEYBOARD_CLI_COUNT 3

/* USB HID keyboard usage names, shared with anything that shows key bindings */
const char *alloy_hid_key_name(uint8_t hid_code);
uint8_t alloy_hid_key_from_name(const char *name);

#endif /* ALLOY_LIB_KEYBOARD_H */
