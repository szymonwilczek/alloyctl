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

#include "accel.h"
#include "hid.h"
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

/*
 * @with_dpi: push the DPI table, which also latches the active CPI level.
 * Off at startup only: the active level is live device state the user drives
 * with the physical CPI button, and there is no read-back to recover it
 * (0xAD notification is push-only).
 * Every other apply path - SAVE, REVERT, live-preview - is explicit user
 * action and does push it.
 */
static void tui_apply_all_impl(struct tui *t, int with_dpi)
{
	const struct alloy_driver_ops *ops = t->drv->ops;
	/*
	 * while High-Efficiency is on, the mode owns polling, the illumination
	 * level and the LED colors (it forces 125 Hz and blanks the LEDs)
	 * do not push the user's values for those here or save/revert would fight
	 * the mode and undo it;
	 * the rest of the config still applies normally
	 */
	int high_eff = t->cfg.high_efficiency != 0;

	if (with_dpi)
		tui_apply(t, ops->apply_dpi, "dpi");
	if (!high_eff)
		tui_apply(t, ops->apply_polling, "polling");
	if (!high_eff)
		tui_apply(t, ops->apply_colors, "colors");
	if ((t->drv->caps & (ALLOY_CAP_BRIGHTNESS | ALLOY_CAP_BATTERY)) &&
	    !high_eff)
		tui_apply(t, ops->apply_brightness, "brightness");
	tui_apply(t, ops->apply_buttons, "buttons");
	if (t->drv->caps & ALLOY_CAP_BATTERY)
		tui_apply(t, ops->apply_sleep, "sleep");

	/*
	 * High-Efficiency itself is deliberately NOT re-pushed here:
	 * toggling it drops the 2.4 GHz link for second, so pushing it on every
	 * save / revert / startup sync would stall those paths.
	 * It is applied on its own toggle (see set_higheff); later save just
	 * commits the live state, which already carries the mode.
	 */
}

void tui_apply_all(struct tui *t)
{
	tui_apply_all_impl(t, 1);
}

/*
 * Push everything to the mouse, commit to onboard flash and persist the host baseline.
 * One path for the footer button, the save shortcut and the quit guard.
 * Returns 0 on success.
 */
int tui_save(struct tui *t)
{
	tui_apply_all(t);
	if (t->drv->ops->save(t->dev)) {
		tui_status(t, "save failed: no device ACK");
		return -1;
	}
	t->baseline = t->cfg;
	if (alloy_state_store(t->drv, &t->cfg))
		tui_status(t, "saved to mouse; baseline file not writable");
	else
		tui_status(t, "saved to mouse flash + baseline");
	if (t->accel_running)
		alloy_accel_reload(t->drv->vendor_id, t->drv->product_id);
	t->dirty = 0;
	return 0;
}

/*
 * Roll the working config back to the session baseline and push it to
 * the mouse, undoing every live-previewed change since startup.
 * Shared by the REVERT button and by quit-without-saving.
 * Only SAVE ever writes the on-disk baseline; this never does.
 */
void tui_revert(struct tui *t)
{
	t->cfg = t->baseline;
	tui_apply_all(t);
	if (t->accel_running) {
		alloy_state_store(t->drv, &t->cfg);
		alloy_accel_reload(t->drv->vendor_id, t->drv->product_id);
	}
	t->dirty = 0;
}

/*
 * Drain unsolicited device events so the ACTIVE level indicator tracks
 * the physical CPI button.
 * Hardware switch is the user acting on the device itself, not a pending edit:
 * it lands in the baseline too, so REVERT and the quit guard never fight the button.
 */
static void tui_poll_device_events(struct tui *t)
{
	uint8_t buf[ALLOY_HID_REPORT_SIZE];
	int n;

	if (!t->drv->ops->parse_event || t->dev->ev.fd < 0)
		return;
	while ((n = alloy_hid_poll(&t->dev->ev, buf, sizeof(buf))) > 0) {
		if (!t->drv->ops->parse_event(buf, (size_t)n, &t->cfg))
			continue;
		t->baseline.dpi_active = t->cfg.dpi_active;
		t->dirty = memcmp(&t->cfg, &t->baseline, sizeof(t->cfg)) != 0;
		tui_status(t, "level %u active (mouse button)",
			   t->cfg.dpi_active + 1);
	}
}

/*
 * Refresh the wireless battery gauge on slow cadence - the query is device round-trip,
 * so it runs every few seconds rather than every frame.
 * battery_pct is left negative when the receiver reports no reading
 * (the mouse is asleep or unlinked), so the footer shows "--" instead of bogus 0%
 */
void tui_poll_battery(struct tui *t)
{
	long now;

	if (!(t->drv->caps & ALLOY_CAP_BATTERY) || !t->drv->ops->battery)
		return;

	now = tui_now_ms();
	if (now < t->battery_next_ms)
		return;
	t->battery_next_ms = now + 8000;

	/*
	 * single idle poll is not proof the mouse is gone - 2.4 GHz link micro-sleeps
	 * between reports
	 * leep the last reading across couple of misses so the gauge stays put instead
	 * of flickering to "--", and only declare "no link" once the misses persist
	 */
	if (t->drv->ops->battery(t->dev, &t->battery_pct,
				 &t->battery_charging)) {
		if (++t->battery_misses >= TUI_BATTERY_MAX_MISSES)
			t->battery_pct = -1;
	} else {
		t->battery_misses = 0;
	}

	/* Bluetooth link: mouse shows up on bus 0x05 while paired over BT */
	t->bt_present = t->drv->bt_product_id &&
			alloy_hid_present_bus(0x05, t->drv->bt_product_id);
}

/*
 * Is there a mouse we can actually talk to right now?
 * Wired mice are always reachable; wireless receiver (ALLOY_CAP_BATTERY) only counts
 * once 2.4 GHz battery reading or Bluetooth link says a mouse is on the other end.
 * Used to gate the startup handshake so bare dongle does not block the UI.
 */
static int tui_device_linked(const struct tui *t)
{
	if (!(t->drv->caps & ALLOY_CAP_BATTERY))
		return 1;
	return t->battery_pct >= 0 || t->bt_present;
}

/*
 * One-shot device handshake:
 * Read the firmware string and push the working config to the mouse.
 * Deferred until mouse is reachable, because on bare 2.4 GHz receiver every
 * command burns the full wake-retry budget (~seconds each) waiting for echo
 * that never comes.
 * Safe to call every frame: it runs the work exactly once, when the link first
 * appears (including right after fresh pair), and is no-op otherwise.
 */
static void tui_sync_device(struct tui *t)
{
	if (t->device_synced || !tui_device_linked(t))
		return;

	if (t->drv->ops->firmware_version &&
	    (t->drv->caps & ALLOY_CAP_FIRMWARE_VERSION)) {
		if (t->drv->ops->firmware_version(t->dev, t->firmware,
						  sizeof(t->firmware)))
			t->firmware[0] = '\0';
	}

	tui_apply_all_impl(t, 0);
	t->device_synced = 1;
}

/*
 * Whether to offer the PAIR button: the driver can bind a mouse to its receiver
 * and nothing is currently linked (no 2.4 GHz battery reading, not on Bluetooth).
 * This is a proxy - a paired-but-powered-off mouse looks the same as an unpaired
 * one from the host - and stands in until a real "is a mouse bound?" query is
 * reverse-engineered from the receiver. Good enough to surface the button
 * exactly when there is no mouse to talk to.
 */
int tui_device_needs_pairing(const struct tui *t)
{
	return (t->drv->caps & ALLOY_CAP_PAIRING) && t->drv->ops->pair &&
	       t->battery_pct < 0 && !t->bt_present;
}

/*
 * Pointer-transform value changed (acceleration/deceleration/angle snapping).
 * Push it to running daemon for live preview by rewriting the config it watches
 * and poking it to re-read.
 * Engine is enabled/disabled separately via tui_accel_set_enabled().
 */
void tui_accel_changed(struct tui *t)
{
	t->dirty = memcmp(&t->cfg, &t->baseline, sizeof(t->cfg)) != 0;
	if (t->live_preview && t->accel_running) {
		alloy_state_store(t->drv, &t->cfg);
		alloy_accel_reload(t->drv->vendor_id, t->drv->product_id);
	}
}

/*
 * Turn the host-side transform engine on or off.
 * This is immediate, committed action (like the LIVE PREVIEW toggle),
 * not part of the dirty/SAVE flow:
 * it spawns or stops the daemon, persists the intent and installs
 * or removes the autostart entry so the choice survives a reboot.
 */
void tui_accel_set_enabled(struct tui *t, int on)
{
	uint16_t vid = t->drv->vendor_id;
	uint16_t pid = t->drv->product_id;

	if (on) {
		t->cfg.accel_enabled = 1;
		alloy_state_store(t->drv, &t->cfg);
		if (alloy_accel_spawn(vid, pid) == 0) {
			t->accel_running = 1;
			alloy_accel_autostart_set(vid, pid, 1);
			tui_status(t, "accel engine on");
		} else {
			/* do not persist the intent or install autostart for engine
			 * that could not start */
			t->accel_running = 0;
			t->cfg.accel_enabled = 0;
			alloy_state_store(t->drv, &t->cfg);
			tui_status(t, "engine failed: no access to /dev/input "
				      "or /dev/uinput (install the udev rule "
				      "and replug, or re-login after "
				      "usermod -aG input)");
		}
	} else {
		alloy_accel_stop(vid, pid);
		alloy_accel_autostart_set(vid, pid, 0);
		t->accel_running = 0;
		t->cfg.accel_enabled = 0;
		alloy_state_store(t->drv, &t->cfg);
		tui_status(t, "accel engine off - motion back to normal");
	}
	t->baseline.accel_enabled = t->cfg.accel_enabled;
	t->dirty = memcmp(&t->cfg, &t->baseline, sizeof(t->cfg)) != 0;
}

/* Every lighting edit funnels through here: dirty tracking + live push */
void tui_lighting_changed(struct tui *t)
{
	t->dirty = memcmp(&t->cfg, &t->baseline, sizeof(t->cfg)) != 0;
	if (t->live_preview)
		tui_apply(t, t->drv->ops->apply_colors, "lighting");
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

/*
 * How many DPI presets this device can hold:
 * whatever the driver advertises, capped by the static config storage.
 */
int tui_dpi_preset_limit(const struct tui *t)
{
	return ALLOY_MIN(t->drv->dpi.max_presets, ALLOY_MAX_DPI_PRESETS);
}

int tui_pane_item_count(const struct tui *t, enum tui_pane pane)
{
	switch (pane) {
	case PANE_ACTIONS:
		/* one entry per button plus the Macro Editor LAUNCH */
		return t->drv->num_buttons + 1;
	case PANE_CENTER:
		/* ILLUMINATION button is all the pane offers */
		return 1;
	case PANE_LEVELS:
		/* one item per preset plus CREATE below the limit */
		return t->cfg.dpi_count +
		       (t->cfg.dpi_count < tui_dpi_preset_limit(t) ? 1 : 0);
	case PANE_POWER: {
		/*
		 * Wireless-only pane.
		 * Empty (and skipped) on wired mice.
		 * SLEEP/SMART/DIM ride ALLOY_CAP_BATTERY;
		 * trailing HIGHEFF item appears only with ALLOY_CAP_HIGH_EFFICIENCY.
		 */
		int n = (t->drv->caps & ALLOY_CAP_BATTERY) ? POWER_HIGHEFF : 0;

		if (t->drv->caps & ALLOY_CAP_HIGH_EFFICIENCY)
			n++;
		return n;
	}
	case PANE_TUNING:
		/* acceleration, deceleration, angle snapping, engine, polling */
		return 5;
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
	init_pair(CLR_INFO, COLOR_CYAN, -1);
	init_pair(CLR_BAT_HIGH, COLOR_GREEN, -1);
	init_pair(CLR_BAT_GOOD, COLOR_WHITE, -1);
	init_pair(CLR_BAT_MID, COLOR_YELLOW, -1);
	init_pair(CLR_BAT_LOW, COLOR_RED, -1);
	init_pair(CLR_LINK_BT, COLOR_BLUE, -1);
	init_pair(CLR_LINK_RF, COLOR_WHITE, -1);
	init_pair(CLR_LINK_OFF, COLOR_WHITE, -1); /* rendered dim for grey */

	tui_zone_color_pairs(t);
}

/*
 * Standalone chooser shown before the main interface when more than one
 * supported mouse is plugged in.
 * Runs its own curses session because no device is bound yet.
 */
int alloy_tui_select_device(const struct alloy_driver *const *drivers,
			    int count)
{
	const char *const hint = "enter: select   esc/q: quit";
	int sel = 0;
	int w;
	int h;
	int y;
	int x;
	int i;
	int ch;
	int chosen = -1;

	w = (int)strlen(hint);
	for (i = 0; i < count; i++) {
		int len = (int)strlen(drivers[i]->name) +
			  13; /* + "  ffff:ffff" */

		if (len > w)
			w = len;
	}
	w += 6;
	h = count + 5;

	setlocale(LC_ALL, "");
	initscr();
	cbreak();
	noecho();
	curs_set(0);
	keypad(stdscr, TRUE);
	if (has_colors()) {
		start_color();
		use_default_colors();
		init_pair(CLR_FRAME_FOCUS, COLOR_YELLOW, -1);
		init_pair(CLR_TITLE, COLOR_CYAN, -1);
		init_pair(CLR_SELECTED, COLOR_BLACK, COLOR_YELLOW);
		init_pair(CLR_DISABLED, COLOR_BLUE, -1);
	}

	for (;;) {
		erase();
		y = (LINES - h) / 2;
		x = (COLS - w) / 2;

		attron(COLOR_PAIR(CLR_FRAME_FOCUS) | A_BOLD);
		mvaddch(y, x, ACS_ULCORNER);
		mvaddch(y, x + w - 1, ACS_URCORNER);
		mvaddch(y + h - 1, x, ACS_LLCORNER);
		mvaddch(y + h - 1, x + w - 1, ACS_LRCORNER);
		mvhline(y, x + 1, ACS_HLINE, w - 2);
		mvhline(y + h - 1, x + 1, ACS_HLINE, w - 2);
		mvvline(y + 1, x, ACS_VLINE, h - 2);
		mvvline(y + 1, x + w - 1, ACS_VLINE, h - 2);
		attroff(COLOR_PAIR(CLR_FRAME_FOCUS) | A_BOLD);

		attron(COLOR_PAIR(CLR_TITLE) | A_BOLD);
		mvprintw(y, x + 2, " SELECT DEVICE ");
		attroff(COLOR_PAIR(CLR_TITLE) | A_BOLD);

		for (i = 0; i < count; i++) {
			if (i == sel)
				attron(COLOR_PAIR(CLR_SELECTED));
			mvprintw(y + 2 + i, x + 3, "%-*s  %04x:%04x", w - 16,
				 drivers[i]->name, drivers[i]->vendor_id,
				 drivers[i]->product_id);
			if (i == sel)
				attroff(COLOR_PAIR(CLR_SELECTED));
		}

		attron(COLOR_PAIR(CLR_DISABLED));
		mvprintw(y + h - 2, x + 3, "%s", hint);
		attroff(COLOR_PAIR(CLR_DISABLED));
		refresh();

		ch = getch();
		if (ch == KEY_UP || ch == 'k') {
			sel = (sel + count - 1) % count;
		} else if (ch == KEY_DOWN || ch == 'j') {
			sel = (sel + 1) % count;
		} else if (ch == 27 || ch == 'q') {
			break;
		} else if (ch == '\n' || ch == KEY_ENTER) {
			chosen = sel;
			break;
		}
	}

	endwin();
	return chosen;
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
	t.battery_pct = -1; /* unknown until the first poll */

	used_defaults = alloy_state_load(t.drv, &t.baseline);
	tui_fx_global_normalize(&t, &t.baseline);
	t.cfg = t.baseline;

	t.accel_running =
		alloy_accel_is_running(t.drv->vendor_id, t.drv->product_id);

	setlocale(LC_ALL, "");
	initscr();
	cbreak();
	noecho();
	curs_set(0);
	keypad(stdscr, TRUE);
	if (has_colors())
		tui_init_colors(&t);

	/*
	 */
	tui_poll_battery(&t);
	tui_sync_device(&t);
	tui_status(&t, used_defaults ?
			       "no saved baseline - using driver defaults" :
			       "baseline loaded from disk");

	/*
	 * both views animate the mouse portrait, so getch runs on timeout
	 * and ERR ticks just trigger redraw that advances the animation
	 * clock;
	 * real keys are dispatched to the active view
	 */
	while (!t.quit) {
		tui_poll_device_events(&t);
		tui_poll_battery(&t);
		tui_sync_device(&t);
		timeout(TUI_ILLUM_FRAME_MS);
		if (t.view == VIEW_ILLUM) {
			tui_illum_draw(&t);
			ch = getch();
			if (ch != ERR)
				tui_illum_handle_key(&t, ch);
		} else {
			tui_draw(&t);
			ch = getch();
			if (ch != ERR)
				tui_handle_key(&t, ch);
		}
	}

	endwin();
	return 0;
}
