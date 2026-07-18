// SPDX-License-Identifier: GPL-2.0-only
/*
 * TUI core:
 * terminal lifecycle, color setup, main loop and the plumbing between
 * configuration edits and the driver ops.
 */
#include <locale.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "tui_internal.h"

void tui_status(struct tui *t, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(t->status, sizeof(t->status), fmt, ap);
	va_end(ap);
}

/*
 * Push one aspect of the working config to the mouse.
 * Used both by live preview (on every edit) and by SAVE/REVERT (for all aspects)
 */
void tui_apply(struct tui *t,
	       int (*op)(struct alloy_device *, const struct alloy_config *),
	       const char *what)
{
	int ret;

	if (!op)
		return;

	ret = op(t->dev, &t->cfg);
	if (ret == -2)
		tui_status(t, "%s: device did not acknowledge", what);
	else if (ret)
		tui_status(t, "%s: I/O error", what);
}

void tui_apply_all(struct tui *t)
{
	const struct alloy_driver_ops *ops = t->drv->ops;

	tui_apply(t, ops->apply_dpi, "dpi");
	tui_apply(t, ops->apply_polling, "polling");
	tui_apply(t, ops->apply_colors, "colors");
	if (t->drv->caps & ALLOY_CAP_BRIGHTNESS)
		tui_apply(t, ops->apply_brightness, "brightness");
	tui_apply(t, ops->apply_buttons, "buttons");
}

/*
 * Effects that cycle their own hues ignore the configured zone color;
 * classified by display-name convention shared across the drivers.
 */
int tui_fx_ignores_color(const struct alloy_driver *drv, uint8_t fx)
{
	const char *name;

	if (!fx || fx >= drv->num_fx)
		return 0;
	name = drv->fx_names[fx];
	return strstr(name, "RAINBOW") != NULL || strstr(name, "DISCO") != NULL;
}

int tui_center_idx_brightness(const struct tui *t)
{
	if (!(t->drv->caps & ALLOY_CAP_BRIGHTNESS))
		return -1;
	return t->drv->num_zones;
}

int tui_center_idx_fx(const struct tui *t)
{
	if (!(t->drv->caps & ALLOY_CAP_FX_GLOBAL))
		return -1;
	return t->drv->num_zones +
	       ((t->drv->caps & ALLOY_CAP_BRIGHTNESS) ? 1 : 0);
}

int tui_center_idx_reactive(const struct tui *t)
{
	if (!(t->drv->caps & ALLOY_CAP_FX_REACTIVE))
		return -1;
	return t->drv->num_zones +
	       ((t->drv->caps & ALLOY_CAP_BRIGHTNESS) ? 1 : 0) +
	       ((t->drv->caps & ALLOY_CAP_FX_GLOBAL) ? 1 : 0);
}

int tui_center_idx_startup(const struct tui *t)
{
	if (!(t->drv->caps & ALLOY_CAP_FX_STARTUP))
		return -1;
	return t->drv->num_zones +
	       ((t->drv->caps & ALLOY_CAP_BRIGHTNESS) ? 1 : 0) +
	       ((t->drv->caps & ALLOY_CAP_FX_GLOBAL) ? 1 : 0) +
	       ((t->drv->caps & ALLOY_CAP_FX_REACTIVE) ? 1 : 0);
}

/* ILLUMINATION button sits after every capability-gated item */
int tui_center_idx_illum(const struct tui *t)
{
	return t->drv->num_zones +
	       ((t->drv->caps & ALLOY_CAP_BRIGHTNESS) ? 1 : 0) +
	       ((t->drv->caps & ALLOY_CAP_FX_GLOBAL) ? 1 : 0) +
	       ((t->drv->caps & ALLOY_CAP_FX_REACTIVE) ? 1 : 0) +
	       ((t->drv->caps & ALLOY_CAP_FX_STARTUP) ? 1 : 0);
}

int tui_pane_item_count(const struct tui *t, enum tui_pane pane)
{
	switch (pane) {
	case PANE_ACTIONS:
		/* one entry per button plus the Macro Editor LAUNCH */
		return t->drv->num_buttons + 1;
	case PANE_CENTER:
		/* zones, brightness, effect, reactive, startup, ILLUMINATION */
		return tui_center_idx_illum(t) + 1;
	case PANE_SENSITIVITY:
		/* CPI 1 slider, CPI 2 slider */
		return 2;
	case PANE_TUNING:
		/* acceleration, deceleration, angle snapping, polling */
		return 4;
	case PANE_FOOTER:
		return FOOTER_COUNT;
	default:
		return 0;
	}
}

static void tui_init_colors(struct tui *t)
{
	start_color();
	use_default_colors();

	init_pair(CLR_FRAME, COLOR_WHITE, -1);
	init_pair(CLR_FRAME_FOCUS, COLOR_YELLOW, -1);
	init_pair(CLR_TITLE, COLOR_CYAN, -1);
	init_pair(CLR_SELECTED, COLOR_BLACK, COLOR_YELLOW);
	init_pair(CLR_ACCENT, COLOR_YELLOW, -1);
	init_pair(CLR_DISABLED, COLOR_BLUE, -1);
	init_pair(CLR_BUTTON, COLOR_BLACK, COLOR_WHITE);
	init_pair(CLR_BUTTON_HOT, COLOR_BLACK, COLOR_GREEN);

	tui_zone_color_pairs(t);
}

int alloy_tui_run(struct alloy_device *dev)
{
	struct tui t;
	int used_defaults;
	int ch;

	memset(&t, 0, sizeof(t));
	t.dev = dev;
	t.drv = dev->drv;
	t.live_preview = 1;

	used_defaults = alloy_state_load(t.drv, &t.baseline);
	t.cfg = t.baseline;

	if (t.drv->ops->firmware_version &&
	    (t.drv->caps & ALLOY_CAP_FIRMWARE_VERSION)) {
		if (t.drv->ops->firmware_version(dev, t.firmware,
						 sizeof(t.firmware)))
			t.firmware[0] = '\0';
	}

	setlocale(LC_ALL, "");
	initscr();
	cbreak();
	noecho();
	curs_set(0);
	keypad(stdscr, TRUE);
	if (has_colors())
		tui_init_colors(&t);

	/*
	 * make the mouse state match the working config
	 * so what the user sees on screen is what the hardware runs
	 */
	tui_apply_all(&t);
	tui_status(&t, used_defaults ?
			       "no saved baseline - using driver defaults" :
			       "baseline loaded from disk");

	while (!t.quit) {
		if (t.view == VIEW_ILLUM) {
			tui_illum_draw(&t);
			ch = getch();
			tui_illum_handle_key(&t, ch);
		} else {
			tui_draw(&t);
			ch = getch();
			tui_handle_key(&t, ch);
		}
	}

	endwin();
	return 0;
}
