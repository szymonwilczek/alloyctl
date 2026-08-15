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

static void adjust_fx_mode(struct tui *t, int delta)
{
	int count = t->drv->num_fx;
	int cur;

	if (!(t->drv->caps & ALLOY_CAP_FX_GLOBAL) || count <= 0)
		return;

	cur = (int)t->cfg.common.zone_fx[0] + delta;
	while (cur < 0)
		cur += count;
	cur %= count;
	t->cfg.common.zone_fx[0] = (uint8_t)cur;
	mark_dirty(t);
	if (t->live_preview && t->drv->ops->apply_colors)
		tui_apply(t, t->drv->ops->apply_colors, "effect");
}

static void adjust_fx_speed(struct tui *t, int delta)
{
	int spd = (int)t->cfg.common.zone_fx_speed[0] + delta;

	spd = ALLOY_CLAMP(spd, 1, 3);
	t->cfg.common.zone_fx_speed[0] = (uint8_t)spd;
	mark_dirty(t);
	if (t->live_preview && t->drv->ops->apply_colors)
		tui_apply(t, t->drv->ops->apply_colors, "effect");
}

/*
 * High-Efficiency Mode toggle.
 * Unlike the other steppers this is a deliberate hardware mode switch that GG
 * applies the instant it is clicked, so it is pushed immediately regardless of
 * the live-preview flag.
 * The op forces the device bundle (0x68 + LEDs off + 125 Hz) and blocks until
 * the link, which the toggle briefly drops, comes back - so a following save
 * finds a live link.
 * It is not re-pushed by tui_apply_all; later save just commits the live state,
 * which already carries the mode.
 */
static void set_higheff(struct tui *t, int on)
{
	if (!alloy_driver_is_mouse(t->drv))
		return;
	on = on ? 1 : 0;
	if (t->cfg.mouse.high_efficiency == on)
		return;
	t->cfg.mouse.high_efficiency = (uint8_t)on;
	mark_dirty(t);
	tui_apply(t, t->drv->ops->apply_high_efficiency, "high-efficiency");
	tui_status(t, on ? "high-efficiency on (link re-synced)" :
			   "high-efficiency off (link re-synced)");
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
			/* h/l flips the toggle by direction */
			set_smart(t, dir > 0);
		else if (sel == POWER_DIM)
			adjust_dim(t, dir * (big ? ALLOY_ILLUM_DIM_STEP * 4 :
						   ALLOY_ILLUM_DIM_STEP));
		else /* POWER_HIGHEFF: h/l flips the toggle by direction */
			set_higheff(t, dir > 0);
		break;
	case PANE_TUNING:
		if (alloy_driver_is_keyboard(t->drv)) {
			int item = 0;

			if (t->drv->caps & ALLOY_CAP_BRIGHTNESS) {
				if (sel == item) {
					adjust_brightness(t,
							  dir * (big ? 25 : 5));
					break;
				}
				item++;
			}

			if (t->drv->caps & ALLOY_CAP_FX_GLOBAL) {
				if (sel == item) {
					adjust_fx_mode(t, dir);
					break;
				}
				item++;

				if ((t->drv->caps & ALLOY_CAP_FX_SPEED) &&
				    t->cfg.common.zone_fx[0] > 0) {
					if (sel == item) {
						adjust_fx_speed(t, dir);
						break;
					}
					item++;
				}
			}

			if (t->drv->num_polling_rates > 0) {
				if (sel == item) {
					adjust_polling(t, dir, big);
					break;
				}
				item++;
			}
			break;
		}
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
		break;

	default:
		break;
	}
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
		else if (sel == POWER_HIGHEFF && alloy_driver_is_mouse(t->drv))
			set_higheff(t, !t->cfg.mouse.high_efficiency);
		break;
	case PANE_TUNING:
		if (sel == 3)
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
