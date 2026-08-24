// SPDX-License-Identifier: GPL-2.0-only
/*
 * Front-end core:
 * terminal lifecycle, color setup, the main loop, and the plumbing between
 * configuration edits and the driver's apply steps.
 *
 * What this file deliberately does not contain is any notion of what
 * the configuration means.
 * Which aspects exist is the driver's step table, what the screens hold is its
 * struct alloy_ui_desc, and when the device is reachable is desc->ready()
 */
#include <locale.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "tui_internal.h"

long tui_now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

void tui_status(struct alloy_ui *ui, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(ui->status, sizeof(ui->status), fmt, ap);
	va_end(ap);
}

void tui_mark_dirty(struct alloy_ui *ui)
{
	ui->dirty = !alloy_config_equal(ui->cfg, ui->baseline);
}

/*
 * Push one named aspect of the working configuration.
 * Used both by live preview (on every edit) and by SAVE/REVERT.
 */
int tui_push(struct alloy_ui *ui, const char *step)
{
	int ret;

	if (!step)
		return 0;

	ret = alloy_driver_apply(ui->dev, ui->cfg, step);
	if (ret == -2)
		tui_status(ui, "%s: device did not acknowledge", step);
	else if (ret)
		tui_status(ui, "%s: I/O error", step);
	return ret;
}

static void apply_report(void *ctx, const char *what, int err)
{
	struct alloy_ui *ui = ctx;

	if (err == -2)
		tui_status(ui, "%s: device did not acknowledge", what);
	else if (err)
		tui_status(ui, "%s: I/O error", what);
}

void tui_apply_all(struct alloy_ui *ui)
{
	alloy_driver_apply_all(ui->dev, ui->cfg, 0, apply_report, ui);
}

/*
 * Push everything, commit to onboard storage and persist the host baseline.
 * One path for the footer button, the save shortcut and the quit guard.
 * Returns 0 on success.
 */
int tui_save(struct alloy_ui *ui)
{
	const struct alloy_driver_ops *ops = ui->drv->ops;
	const char *kind = alloy_driver_kind(ui->drv);
	void (*saved)(struct alloy_ui *);

	tui_apply_all(ui);
	if (ops && ops->save && ops->save(ui->dev)) {
		tui_status(ui, "save failed: no device ACK");
		return -1;
	}
	alloy_config_copy(ui->baseline, ui->cfg);
	if (alloy_state_store(ui->drv, ui->cfg))
		tui_status(ui, "saved to %s; baseline file not writable", kind);
	else
		tui_status(ui, "saved to %s + baseline", kind);

	saved = TUI_HOOK(ui, saved);
	if (saved)
		saved(ui);
	ui->dirty = 0;
	return 0;
}

/*
 * Roll the working configuration back to the session baseline and push it,
 * undoing every live-previewed change since startup.
 * Shared by the REVERT button and by quit-without-saving.
 * Only SAVE ever writes the on-disk baseline; this never does.
 */
void tui_revert(struct alloy_ui *ui)
{
	void (*reverted)(struct alloy_ui *) = TUI_HOOK(ui, reverted);

	alloy_config_copy(ui->cfg, ui->baseline);
	tui_apply_all(ui);
	if (reverted)
		reverted(ui);
	ui->dirty = 0;
}

/*
 * Drain unsolicited device reports, so indicators track what the user does
 * on the hardware itself.
 * Hardware change is the user acting on the device, not a pending edit,
 * so the driver gets to fold it into the baseline through desc->event()
 */
static void tui_poll_device_events(struct alloy_ui *ui)
{
	void (*event)(struct alloy_ui *, struct alloy_config *);
	uint8_t buf[256];
	int n;

	if (!ui->drv->ops || !ui->drv->ops->parse_event)
		return;

	event = TUI_HOOK(ui, event);
	while ((n = alloy_dev_poll_event(ui->dev, buf, sizeof(buf))) > 0) {
		if (!ui->drv->ops->parse_event(ui->dev, buf, (size_t)n,
					       ui->cfg))
			continue;
		if (event)
			event(ui, ui->baseline);
		tui_mark_dirty(ui);
	}
}

/*
 * One-shot device handshake:
 * Read the firmware string and push the working configuration.
 * Deferred until the driver says the device is reachable, because on a link
 * that is not up every command burns its full retry budget waiting for answer
 * that never comes.
 * Safe to call every frame: it runs the work exactly once.
 */
static void tui_sync_device(struct alloy_ui *ui)
{
	const struct alloy_driver_ops *ops = ui->drv->ops;
	int (*ready)(struct alloy_ui *) = TUI_HOOK(ui, ready);

	if (ui->device_synced)
		return;
	if (ready && !ready(ui))
		return;

	if (ops && ops->firmware_version) {
		if (ops->firmware_version(ui->dev, ui->firmware,
					  sizeof(ui->firmware)))
			ui->firmware[0] = '\0';
	}

	/*
	 * Steps the driver marked ALLOY_APPLY_SKIP_SYNC stay out of this one
	 * path - they carry live device state the host cannot recover.
	 */
	if (!ui->probed_hw)
		alloy_driver_apply_all(ui->dev, ui->cfg, ALLOY_APPLY_SKIP_SYNC,
				       apply_report, ui);
	ui->device_synced = 1;
}

static void tui_init_colors(void)
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
	init_pair(CLR_GOOD, COLOR_GREEN, -1);
	init_pair(CLR_WARN, COLOR_YELLOW, -1);
	init_pair(CLR_BAD, COLOR_RED, -1);
	init_pair(CLR_RGB_FALLBACK, COLOR_WHITE, -1);

	if (COLORS >= 256) {
		for (short c = 16; c < 232; c++)
			init_pair((short)(CLR_RGB_CUBE_BASE + (c - 16)), c, -1);
	}
}

/*
 * Standalone chooser shown before the main interface when more than one
 * supported device is plugged in.
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
		int len = (int)strlen(drivers[i]->name) + 13;

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

/* park the focus on the first pane that actually offers something */
static void focus_first(struct alloy_ui *ui)
{
	const struct alloy_ui_screen *sc = tui_screen(ui);
	int i;

	for (i = 0; i < sc->num_panes; i++) {
		if (tui_pane_slot_count(ui, i) > 0) {
			ui->focus = i;
			return;
		}
	}
	ui->focus = TUI_FOOTER_PANE;
}

int alloy_tui_run(struct alloy_device *dev)
{
	static struct alloy_ui ui;
	const struct alloy_driver_ops *ops = dev->drv->ops;
	void (*enter)(struct alloy_ui *);
	void (*tick)(struct alloy_ui *, long);
	int (*ready)(struct alloy_ui *);
	int used_defaults = 0;
	int ret = 0;
	int ch;

	memset(&ui, 0, sizeof(ui));
	ui.dev = dev;
	ui.drv = dev->drv;
	ui.desc = dev->drv->ui;
	ui.live_preview = 1;

	if (!ui.desc || !ui.desc->num_screens) {
		fprintf(stderr,
			"alloyctl: %s exposes no interactive interface\n",
			ui.drv->name);
		return 1;
	}

	ui.cfg = alloy_config_alloc(ui.drv);
	ui.baseline = alloy_config_alloc(ui.drv);
	if (!ui.cfg || !ui.baseline) {
		fprintf(stderr, "alloyctl: out of memory\n");
		ret = 1;
		goto out;
	}

	alloy_ui_host_register(&tui_ui_host);

	/*
	 * Let the driver's UI wake up first:
	 * it decides, through ready(), whether the device can be talked to at all
	 */
	enter = TUI_HOOK(&ui, enter);
	tick = TUI_HOOK(&ui, tick);
	ready = TUI_HOOK(&ui, ready);
	if (enter)
		enter(&ui);
	if (tick)
		tick(&ui, tui_now_ms());

	/* try probing the live hardware state straight from the device */
	if (ops && ops->read_config && (!ready || ready(&ui))) {
		alloy_config_defaults(ui.drv, ui.baseline);
		if (ops->read_config(dev, ui.baseline) == 0)
			ui.probed_hw = 1;
	}
	if (!ui.probed_hw)
		used_defaults = alloy_state_load(ui.drv, ui.baseline);
	alloy_config_copy(ui.cfg, ui.baseline);

	setlocale(LC_ALL, "");
	initscr();
	cbreak();
	noecho();
	curs_set(0);
	keypad(stdscr, TRUE);
	if (has_colors())
		tui_init_colors();

	tui_sync_device(&ui);
	focus_first(&ui);

	if (ui.probed_hw)
		tui_status(&ui, "hardware state probed directly from device");
	else if (used_defaults)
		tui_status(&ui, "no saved baseline - using driver defaults");
	else
		tui_status(&ui, "baseline loaded from disk");

	/*
	 * device portrait animates, so getch runs on a timeout and ERR
	 * ticks just trigger a redraw that advances the animation clock
	 */
	while (!ui.quit) {
		tui_poll_device_events(&ui);
		if (tick)
			tick(&ui, tui_now_ms());
		tui_sync_device(&ui);
		timeout(TUI_FRAME_MS);
		tui_draw(&ui);
		ch = getch();
		if (ch != ERR)
			tui_handle_key(&ui, ch);
	}

	endwin();
	alloy_ui_host_register(NULL);
out:
	alloy_config_free(ui.cfg);
	alloy_config_free(ui.baseline);
	ui.cfg = NULL;
	ui.baseline = NULL;
	return ret;
}
