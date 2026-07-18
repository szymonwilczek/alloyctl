// SPDX-License-Identifier: GPL-2.0-only
/*
 * Input dispatch:
 * pane navigation, steppers and footer actions.
 */
#include <string.h>

#include "tui_internal.h"

static void mark_dirty(struct tui *t)
{
	t->dirty = memcmp(&t->cfg, &t->baseline, sizeof(t->cfg)) != 0;
}

static void adjust_dpi(struct tui *t, int preset, int delta)
{
	const struct alloy_driver *drv = t->drv;
	int dpi = t->cfg.dpi[preset][0] + delta;

	dpi = ALLOY_CLAMP(dpi, drv->dpi.min, drv->dpi.max);
	dpi = dpi / drv->dpi.step * drv->dpi.step;
	t->cfg.dpi[preset][0] = (uint16_t)dpi;
	t->cfg.dpi[preset][1] = (uint16_t)dpi;
	mark_dirty(t);
	if (t->live_preview)
		tui_apply(t, drv->ops->apply_dpi, "dpi");
}

static void adjust_polling(struct tui *t, int dir)
{
	const struct alloy_driver *drv = t->drv;
	int i;

	for (i = 0; i < drv->num_polling_rates; i++) {
		if (drv->polling_rates[i] == t->cfg.polling_hz)
			break;
	}
	if (i == drv->num_polling_rates)
		i = 0;
	/* rates are stored descending:
	 * previous entry is faster */
	else if (dir > 0)
		i = ALLOY_MAX(i - 1, 0);
	else
		i = ALLOY_MIN(i + 1, drv->num_polling_rates - 1);

	t->cfg.polling_hz = drv->polling_rates[i];
	mark_dirty(t);
	if (t->live_preview)
		tui_apply(t, drv->ops->apply_polling, "polling");
}

static void adjust_brightness(struct tui *t, int delta)
{
	int val = t->cfg.brightness + delta;

	t->cfg.brightness = (uint8_t)ALLOY_CLAMP(val, 0, 100);
	mark_dirty(t);
	if (t->live_preview)
		tui_apply(t, t->drv->ops->apply_brightness, "brightness");
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
		t->cfg = t->baseline;
		tui_apply_all(t);
		t->dirty = 0;
		tui_status(t, "reverted to session baseline");
		break;
	case FOOTER_SAVE:
		tui_apply_all(t);
		if (t->drv->ops->save(t->dev)) {
			tui_status(t, "save failed: no device ACK");
			break;
		}
		t->baseline = t->cfg;
		if (alloy_state_store(t->drv, &t->cfg))
			tui_status(t, "saved to mouse; baseline file "
				      "not writable");
		else
			tui_status(t, "saved to mouse flash + baseline");
		t->dirty = 0;
		break;
	default:
		break;
	}
}

static void apply_lighting(struct tui *t)
{
	tui_lighting_changed(t);
}

static void pane_adjust(struct tui *t, int dir, int big)
{
	int sel = t->cursor[t->focus];
	uint8_t fx;
	uint8_t i;

	switch (t->focus) {
	case PANE_CENTER:
		if (sel == tui_center_idx_brightness(t)) {
			adjust_brightness(t, dir * (big ? 20 : 5));
		} else if (sel == tui_center_idx_fx(t)) {
			/* device-wide stepper: move every zone together */
			fx = (uint8_t)((t->cfg.zone_fx[0] + t->drv->num_fx +
					dir) %
				       t->drv->num_fx);
			for (i = 0; i < t->drv->num_zones; i++)
				t->cfg.zone_fx[i] = fx;
			apply_lighting(t);
		} else if (sel == tui_center_idx_reactive(t)) {
			t->cfg.reactive_enabled = !t->cfg.reactive_enabled;
			apply_lighting(t);
		} else if (sel == tui_center_idx_startup(t)) {
			t->cfg.startup_fx =
				(uint8_t)((t->cfg.startup_fx + 4 + dir) % 4);
			apply_lighting(t);
		} else if (sel < t->drv->num_zones && t->drv->num_fx > 1) {
			t->cfg.zone_fx[sel] = (uint8_t)((t->cfg.zone_fx[sel] +
							 t->drv->num_fx + dir) %
							t->drv->num_fx);
			apply_lighting(t);
		}
		break;
	case PANE_SENSITIVITY:
		adjust_dpi(t, sel, dir * (big ? 10 : 1) * t->drv->dpi.step);
		break;
	case PANE_TUNING:
		if (sel == 3) {
			adjust_polling(t, dir);
		} else {
			tui_status(t, "not supported by this device");
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
		if (sel < t->drv->num_zones)
			tui_modal_color_zone(t, sel);
		else if (sel == tui_center_idx_reactive(t))
			tui_modal_color_reactive(t);
		else if (sel == tui_center_idx_illum(t))
			tui_illum_enter(t);
		break;
	case PANE_FOOTER:
		footer_activate(t);
		break;
	default:
		break;
	}
}

void tui_handle_key(struct tui *t, int ch)
{
	int count;

	switch (ch) {
	case 'q':
		t->quit = 1;
		return;
	case '\t':
		t->focus = (enum tui_pane)((t->focus + 1) % PANE_COUNT);
		return;
	case KEY_BTAB:
		t->focus = (enum tui_pane)((t->focus + PANE_COUNT - 1) %
					   PANE_COUNT);
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
		if (t->focus == PANE_SENSITIVITY) {
			t->cfg.dpi_active =
				(uint8_t)t->cursor[PANE_SENSITIVITY];
			t->dirty = 1;
			if (t->live_preview)
				tui_apply(t, t->drv->ops->apply_dpi, "dpi");
		}
		break;
	case '\n':
	case KEY_ENTER:
		pane_activate(t);
		break;
	default:
		break;
	}
}
