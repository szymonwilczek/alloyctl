// SPDX-License-Identifier: GPL-2.0-only
/*
 * Input dispatch:
 * pane navigation, steppers and footer actions.
 */
#include <string.h>

#include "accel.h"
#include "tui_internal.h"

static void mark_dirty(struct tui *t)
{
	t->dirty = memcmp(&t->cfg, &t->baseline, sizeof(t->cfg)) != 0;
}

/* host-side transform steppers: edit the value and live-preview via the daemon */
static void adjust_accel(struct tui *t, int delta)
{
	if (!alloy_driver_is_mouse(t->drv))
		return;
	t->cfg.mouse.acceleration =
		(int8_t)ALLOY_CLAMP(t->cfg.mouse.acceleration + delta,
				    ALLOY_ACCEL_MIN, ALLOY_ACCEL_MAX);
	tui_accel_changed(t);
}

static void adjust_decel(struct tui *t, int delta)
{
	if (!alloy_driver_is_mouse(t->drv))
		return;
	t->cfg.mouse.deceleration =
		(int8_t)ALLOY_CLAMP(t->cfg.mouse.deceleration + delta,
				    ALLOY_DECEL_MIN, ALLOY_DECEL_MAX);
	tui_accel_changed(t);
}

static void adjust_snap(struct tui *t, int delta)
{
	if (!alloy_driver_is_mouse(t->drv))
		return;
	t->cfg.mouse.angle_snapping =
		(uint8_t)ALLOY_CLAMP(t->cfg.mouse.angle_snapping + delta,
				     ALLOY_SNAP_MIN, ALLOY_SNAP_MAX);
	tui_accel_changed(t);
}

/* Battery Saver stepper: the device sleep timer in minutes (0 = never) */
static void adjust_sleep(struct tui *t, int delta)
{
	t->cfg.common.sleep_min =
		(uint8_t)ALLOY_CLAMP(t->cfg.common.sleep_min + delta,
				     ALLOY_SLEEP_MIN, ALLOY_SLEEP_MAX);
	mark_dirty(t);
	if (t->live_preview)
		tui_apply(t, t->drv->ops->apply_sleep, "sleep");
}

/*
 * Smart Illum toggle.
 * It is byte 3 of the 0x63 illumination command, so it rides the brightness
 * apply rather than an op of its own.
 */
static void set_smart(struct tui *t, int on)
{
	on = on ? 1 : 0;
	if (t->cfg.common.illum_smart == on)
		return;
	t->cfg.common.illum_smart = (uint8_t)on;
	mark_dirty(t);
	if (t->live_preview)
		tui_apply(t, t->drv->ops->apply_brightness, "smart mode");
}

/*
 * Dim Timer stepper.
 * Like smart mode it is part of the 0x63 illumination command,
 * so it rides the brightness apply.
 */
static void adjust_dim(struct tui *t, int delta)
{
	t->cfg.common.illum_dim_s = (uint16_t)ALLOY_CLAMP(
		(int)t->cfg.common.illum_dim_s + delta, 0, ALLOY_ILLUM_DIM_MAX);
	mark_dirty(t);
	if (t->live_preview)
		tui_apply(t, t->drv->ops->apply_brightness, "dim timer");
}

static void adjust_brightness(struct tui *t, int delta)
{
	if (!(t->drv->caps & ALLOY_CAP_BRIGHTNESS))
		return;
	t->cfg.common.brightness = (uint8_t)ALLOY_CLAMP(
		(int)t->cfg.common.brightness + delta, 0, 100);
	mark_dirty(t);
	if (t->live_preview && t->drv->ops->apply_brightness)
		tui_apply(t, t->drv->ops->apply_brightness, "brightness");
}

static void adjust_dpi(struct tui *t, int preset, int delta)
{
	const struct alloy_driver *drv = t->drv;
	int dpi;

	if (!alloy_driver_is_mouse(drv))
		return;

	dpi = t->cfg.mouse.dpi[preset][0] + delta;
	dpi = ALLOY_CLAMP(dpi, drv->dpi.min, drv->dpi.max);
	dpi = dpi / drv->dpi.step * drv->dpi.step;
	t->cfg.mouse.dpi[preset][0] = (uint16_t)dpi;
	t->cfg.mouse.dpi[preset][1] = (uint16_t)dpi;
	mark_dirty(t);
	if (t->live_preview)
		tui_apply(t, drv->ops->apply_dpi, "dpi");
}

static void adjust_polling(struct tui *t, int dir, int big)
{
	const struct alloy_driver *drv = t->drv;
	int i;

	if (!drv->num_polling_rates)
		return;

	for (i = 0; i < drv->num_polling_rates; i++) {
		if (drv->polling_rates[i] == t->cfg.common.polling_hz)
			break;
	}
	if (i == drv->num_polling_rates)
		i = 0;
	/* rates are stored descending: lower index is faster
	 * H/L jump straight to the fastest / slowest rate */
	else if (dir > 0)
		i = big ? 0 : ALLOY_MAX(i - 1, 0);
	else
		i = big ? drv->num_polling_rates - 1 :
			  ALLOY_MIN(i + 1, drv->num_polling_rates - 1);

	t->cfg.common.polling_hz = drv->polling_rates[i];
	mark_dirty(t);
	if (t->live_preview)
		tui_apply(t, drv->ops->apply_polling, "polling");
}

static void set_active_dpi_preset(struct tui *t, int preset)
{
	if (!alloy_driver_is_mouse(t->drv))
		return;
	if (preset >= t->cfg.mouse.dpi_count)
		return;
	t->cfg.mouse.dpi_active = (uint8_t)preset;
	mark_dirty(t);
	if (t->live_preview)
		tui_apply(t, t->drv->ops->apply_dpi, "dpi");
	tui_status(t, "level %d active", preset + 1);
}

/*
 * Append preset seeded with double the last one (clamped and snapped),
 * which reproduces the 800/1600/3200/... ladder the stock software builds,
 * and leave the cursor on the newcomer.
 */
static void create_dpi_preset(struct tui *t)
{
	const struct alloy_driver *drv = t->drv;
	uint8_t n;
	int dpi;

	if (!alloy_driver_is_mouse(drv))
		return;

	n = t->cfg.mouse.dpi_count;
	if (n >= tui_dpi_preset_limit(t)) {
		tui_status(t, "this mouse holds at most %d levels",
			   tui_dpi_preset_limit(t));
		return;
	}

	dpi = t->cfg.mouse.dpi[n - 1][0] * 2;
	dpi = ALLOY_CLAMP(dpi, drv->dpi.min, drv->dpi.max);
	dpi = dpi / drv->dpi.step * drv->dpi.step;
	t->cfg.mouse.dpi[n][0] = (uint16_t)dpi;
	t->cfg.mouse.dpi[n][1] = (uint16_t)dpi;
	t->cfg.mouse.dpi_count = (uint8_t)(n + 1);
	t->cursor[PANE_LEVELS] = n;

	mark_dirty(t);
	if (t->live_preview)
		tui_apply(t, drv->ops->apply_dpi, "dpi");
	tui_status(t, "level %u created", n + 1);
}

static void footer_activate(struct tui *t)
{
	switch (t->cursor[PANE_FOOTER]) {
	case FOOTER_LIVE_PREVIEW:
		t->live_preview = !t->live_preview;
		if (t->live_preview) {
			tui_apply_all(t);
			tui_status(t, "live preview on");
		} else {
			tui_status(t, "live preview off - "
				      "changes stay on screen only");
		}
		break;
	case FOOTER_REVERT:
		tui_revert(t);
		tui_status(t, "reverted to session baseline");
		break;
	case FOOTER_SAVE:
		tui_save(t);
		break;
	default:
		break;
	}
}

static const uint8_t snap_tap_pairs[][2] = {
	{ 0x04, 0x07 }, /* A / D */
	{ 0x1A, 0x16 }, /* W / S */
	{ 0x14, 0x08 }, /* Q / E */
	{ 0x50, 0x4F }, /* Left / Right */
	{ 0x52, 0x51 }, /* Up / Down */
};

static void snap_tap_adjust_keys(struct tui *t, uint8_t g, int dir)
{
	int pidx = 0;
	size_t count = ALLOY_ARRAY_SIZE(snap_tap_pairs);

	for (size_t pi = 0; pi < count; pi++) {
		if (snap_tap_pairs[pi][0] ==
			    t->cfg.kbd.snap_tap_groups[g].key1 &&
		    snap_tap_pairs[pi][1] ==
			    t->cfg.kbd.snap_tap_groups[g].key2) {
			pidx = (int)pi;
			break;
		}
	}
	pidx = (int)((pidx + dir + count) % count);
	t->cfg.kbd.snap_tap_groups[g].key1 = snap_tap_pairs[pidx][0];
	t->cfg.kbd.snap_tap_groups[g].key2 = snap_tap_pairs[pidx][1];
	mark_dirty(t);

	if (t->drv->ops->apply_snap_tap)
		t->drv->ops->apply_snap_tap(t->dev, &t->cfg);

	tui_status(t, "Group %u Keys: %s / %s", g + 1,
		   alloy_hid_key_name(snap_tap_pairs[pidx][0]),
		   alloy_hid_key_name(snap_tap_pairs[pidx][1]));
}

static void snap_tap_adjust_mode(struct tui *t, uint8_t g, int dir)
{
	static const char *const mnames[] = { "Last Input", "Key 1", "Key 2",
					      "Neutral" };
	int m = (int)t->cfg.kbd.snap_tap_groups[g].mode + dir;

	if (m < 0)
		m = 3;
	if (m > 3)
		m = 0;

	t->cfg.kbd.snap_tap_groups[g].mode = (uint8_t)m;
	mark_dirty(t);

	if (t->drv->ops->apply_snap_tap)
		t->drv->ops->apply_snap_tap(t->dev, &t->cfg);

	tui_status(t, "Group %u Mode: %s", g + 1, mnames[m]);
}

static void snap_tap_add_group(struct tui *t)
{
	uint8_t gc = t->cfg.kbd.snap_tap_group_count;

	if (gc >= ALLOY_MAX_SNAP_TAP_GROUPS)
		return;

	t->cfg.kbd.snap_tap_groups[gc].mode = 0;
	t->cfg.kbd.snap_tap_groups[gc].key1 =
		snap_tap_pairs[gc % ALLOY_ARRAY_SIZE(snap_tap_pairs)][0];
	t->cfg.kbd.snap_tap_groups[gc].key2 =
		snap_tap_pairs[gc % ALLOY_ARRAY_SIZE(snap_tap_pairs)][1];
	t->cfg.kbd.snap_tap_group_count++;
	mark_dirty(t);

	if (t->drv->ops->apply_snap_tap)
		t->drv->ops->apply_snap_tap(t->dev, &t->cfg);

	tui_status(t, "Added Snap Tap Group %u",
		   t->cfg.kbd.snap_tap_group_count);
}

static void snap_tap_remove_group(struct tui *t)
{
	if (t->cfg.kbd.snap_tap_group_count <= 1)
		return;

	t->cfg.kbd.snap_tap_group_count--;
	mark_dirty(t);

	if (t->drv->ops->apply_snap_tap)
		t->drv->ops->apply_snap_tap(t->dev, &t->cfg);

	tui_status(t, "Removed Snap Tap Group");
}

static void snap_tap_toggle(struct tui *t, int on)
{
	t->cfg.kbd.snap_tap = on ? 1 : 0;
	mark_dirty(t);

	if (t->drv->ops->apply_snap_tap)
		t->drv->ops->apply_snap_tap(t->dev, &t->cfg);

	tui_status(t, "Snap Tap %s", t->cfg.kbd.snap_tap ? "ON" : "OFF");
}

static void adjust_profile(struct tui *t, int dir)
{
	uint8_t prof = t->cfg.kbd.profile_active;

	if (dir > 0)
		prof = (prof >= 3) ? 1 : (uint8_t)(prof + 1);
	else
		prof = (prof <= 1) ? 3 : (uint8_t)(prof - 1);

	t->cfg.kbd.profile_active = prof;
	mark_dirty(t);

	if (t->drv->ops->apply_profile)
		t->drv->ops->apply_profile(t->dev, &t->cfg);

	tui_status(t, "Profile %u active", prof);
}

static void adjust_keyboard_controls(struct tui *t, int dir, int big)
{
	int sel = t->cursor[PANE_TUNING];
	int item = 0;

	if ((t->drv->caps & ALLOY_CAP_BRIGHTNESS) && sel == item++) {
		adjust_brightness(t, dir * (big ? 25 : 5));
		return;
	}

	if (t->drv->num_polling_rates > 0 && sel == item++) {
		adjust_polling(t, dir, big);
		return;
	}

	if (t->drv->caps & ALLOY_CAP_SNAP_TAP) {
		if (sel == item++) {
			snap_tap_toggle(t, dir > 0);
			return;
		}

		for (uint8_t g = 0; g < t->cfg.kbd.snap_tap_group_count; g++) {
			if (sel == item++) {
				snap_tap_adjust_keys(t, g, dir);
				return;
			}
			if (sel == item++) {
				snap_tap_adjust_mode(t, g, dir);
				return;
			}
		}

		if (t->cfg.kbd.snap_tap_group_count <
			    ALLOY_MAX_SNAP_TAP_GROUPS &&
		    sel == item++) {
			if (dir > 0)
				snap_tap_add_group(t);
			return;
		}

		if (t->cfg.kbd.snap_tap_group_count > 1 && sel == item++) {
			if (dir < 0)
				snap_tap_remove_group(t);
			return;
		}
	}

	if ((t->drv->caps & ALLOY_CAP_PROFILE) && sel == item++)
		adjust_profile(t, dir);
}

static void adjust_mouse_tuning(struct tui *t, int dir, int big)
{
	int sel = t->cursor[PANE_TUNING];

	switch (sel) {
	case 0:
		adjust_accel(t, dir * (big ? ALLOY_ACCEL_STEP * 10 :
					     ALLOY_ACCEL_STEP));
		break;
	case 1:
		adjust_decel(t, dir * (big ? ALLOY_DECEL_STEP * 10 :
					     ALLOY_DECEL_STEP));
		break;
	case 2:
		adjust_snap(t, dir * (big ? ALLOY_SNAP_STEP * 5 :
					    ALLOY_SNAP_STEP));
		break;
	case 3:
		tui_status(t, "enter: toggle the OS accel engine");
		break;
	case 4:
		adjust_polling(t, dir, big);
		break;
	default:
		break;
	}
}

static void pane_adjust(struct tui *t, int dir, int big)
{
	int sel = t->cursor[t->focus];

	switch (t->focus) {
	case PANE_LEVELS:
		if (alloy_driver_is_mouse(t->drv) &&
		    sel < t->cfg.mouse.dpi_count)
			adjust_dpi(t, sel,
				   dir * (big ? 10 : 1) * t->drv->dpi.step);
		break;
	case PANE_POWER:
		if (sel == POWER_SLEEP)
			adjust_sleep(t, dir * (big ? ALLOY_SLEEP_STEP * 5 :
						     ALLOY_SLEEP_STEP));
		else if (sel == POWER_SMART)
			set_smart(t, dir > 0);
		else if (sel == POWER_DIM)
			adjust_dim(t, dir * (big ? ALLOY_ILLUM_DIM_STEP * 4 :
						   ALLOY_ILLUM_DIM_STEP));
		break;
	case PANE_TUNING:
		if (alloy_driver_is_keyboard(t->drv))
			adjust_keyboard_controls(t, dir, big);
		else
			adjust_mouse_tuning(t, dir, big);
		break;
	default:
		break;
	}
}

static void activate_keyboard_controls(struct tui *t)
{
	int sel = t->cursor[PANE_TUNING];
	int item = 0;

	if (t->drv->caps & ALLOY_CAP_BRIGHTNESS)
		item++;

	if (t->drv->num_polling_rates > 0)
		item++;

	if (t->drv->caps & ALLOY_CAP_SNAP_TAP) {
		if (sel == item++) {
			snap_tap_toggle(t, !t->cfg.kbd.snap_tap);
			return;
		}

		for (uint8_t g = 0; g < t->cfg.kbd.snap_tap_group_count; g++) {
			if (sel == item++) {
				snap_tap_adjust_keys(t, g, 1);
				return;
			}
			if (sel == item++) {
				snap_tap_adjust_mode(t, g, 1);
				return;
			}
		}

		if (t->cfg.kbd.snap_tap_group_count <
			    ALLOY_MAX_SNAP_TAP_GROUPS &&
		    sel == item++) {
			snap_tap_add_group(t);
			return;
		}

		if (t->cfg.kbd.snap_tap_group_count > 1 && sel == item++) {
			snap_tap_remove_group(t);
			return;
		}
	}

	if ((t->drv->caps & ALLOY_CAP_PROFILE) && sel == item++)
		adjust_profile(t, 1);
}

static void pane_activate(struct tui *t)
{
	int sel = t->cursor[t->focus];

	switch (t->focus) {
	case PANE_ACTIONS:
		if (sel < t->drv->num_buttons)
			tui_modal_remap(t, sel);
		else
			tui_modal_message("MACRO EDITOR", "TBA");
		break;
	case PANE_CENTER:
		if (tui_has_illum_view(t->drv))
			tui_illum_enter(t);
		break;
	case PANE_POWER:
		if (sel == POWER_SMART)
			set_smart(t, !t->cfg.common.illum_smart);
		break;
	case PANE_TUNING:
		if (alloy_driver_is_keyboard(t->drv))
			activate_keyboard_controls(t);
		else if (sel == 3)
			tui_accel_set_enabled(t, !t->accel_running);
		break;
	case PANE_LEVELS:
		if (!alloy_driver_is_mouse(t->drv))
			break;
		if (sel < t->cfg.mouse.dpi_count)
			set_active_dpi_preset(t, sel);
		else
			create_dpi_preset(t);
		break;
	case PANE_FOOTER:
		footer_activate(t);
		break;
	default:
		break;
	}
}

/* step focus by dir, skipping panes that hold no items (e.g. POWER on wired) */
static void focus_step(struct tui *t, int dir)
{
	do {
		t->focus = (enum tui_pane)((t->focus + dir + PANE_COUNT) %
					   PANE_COUNT);
	} while (tui_pane_item_count(t, t->focus) == 0);
}

void tui_handle_key(struct tui *t, int ch)
{
	int count;

	switch (ch) {
	case 'q':
		if (t->dirty)
			tui_modal_confirm_quit(t);
		else
			t->quit = 1;
		return;
	case 's':
		tui_save(t);
		return;
	case 'p':
		/* PAIR button in the DEVICE box: only when a mouse can be bound */
		if (tui_device_needs_pairing(t))
			tui_modal_pair(t);
		return;
	case '\t':
		focus_step(t, 1);
		return;
	case KEY_BTAB:
		focus_step(t, -1);
		return;
	case KEY_RESIZE:
		return;
	default:
		break;
	}

	count = tui_pane_item_count(t, t->focus);

	switch (ch) {
	case KEY_UP:
	case 'k':
		t->cursor[t->focus] = (t->cursor[t->focus] + count - 1) % count;
		break;
	case KEY_DOWN:
	case 'j':
		t->cursor[t->focus] = (t->cursor[t->focus] + 1) % count;
		break;
	case KEY_LEFT:
	case 'h':
		pane_adjust(t, -1, 0);
		break;
	case KEY_RIGHT:
	case 'l':
		pane_adjust(t, 1, 0);
		break;
	case 'H':
		pane_adjust(t, -1, 1);
		break;
	case 'L':
		pane_adjust(t, 1, 1);
		break;
	case 'a':
		if (t->focus == PANE_LEVELS)
			set_active_dpi_preset(t, t->cursor[PANE_LEVELS]);
		break;
	case '\n':
	case KEY_ENTER:
		pane_activate(t);
		break;
	default:
		break;
	}
}
