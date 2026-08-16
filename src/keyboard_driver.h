/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Keyboard-specific driver definitions and configuration.
 */
#ifndef ALLOY_KEYBOARD_DRIVER_H
#define ALLOY_KEYBOARD_DRIVER_H

#include "alloy.h"

/* Keyboard-specific capabilities */
#define ALLOY_CAP_WIN_LOCK (1u << 12) /* Windows / Meta key lock toggle */
#define ALLOY_CAP_SNAP_TAP \
	(1u << 13) /* Hardware Snap Tap / SOCD counter-strafing */
#define ALLOY_CAP_PROFILE (1u << 14) /* Onboard hardware profiles */

#define ALLOY_MAX_SNAP_TAP_GROUPS 10

struct alloy_snap_tap_group {
	uint8_t mode; /* 0: Last Input, 1: Key 1, 2: Key 2, 3: Neutral */
	uint8_t key1; /* HID keycode, e.g. 0x04 ('A') */
	uint8_t key2; /* HID keycode, e.g. 0x07 ('D') */
};

/*
 * Keyboard-specific configuration.
 */
struct alloy_config_keyboard {
	/*
	 * Windows / Meta key lock toggle (ALLOY_CAP_WIN_LOCK).
	 * 0 = normal (Win key active), 1 = locked (Win key disabled).
	 */
	uint8_t win_lock;
	/* Hardware Snap Tap / SOCD counter-strafing (ALLOY_CAP_SNAP_TAP) */
	uint8_t snap_tap;
	uint8_t snap_tap_group_count;
	struct alloy_snap_tap_group snap_tap_groups[ALLOY_MAX_SNAP_TAP_GROUPS];
	/* Active hardware onboard profile (ALLOY_CAP_PROFILE, 1-based) */
	uint8_t profile_active;
	uint8_t profile_count;
};

struct alloy_driver;
struct alloy_config;

const char *alloy_hid_key_name(uint8_t hid_code);
uint8_t alloy_hid_key_from_name(const char *name);

void alloy_config_keyboard_defaults(const struct alloy_driver *drv,
				    struct alloy_config *cfg);

#endif /* ALLOY_KEYBOARD_DRIVER_H */
