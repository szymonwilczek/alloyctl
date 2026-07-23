// SPDX-License-Identifier: GPL-2.0-only
/*
 * Pane layout and rendering.
 *
 * Screen layout (percentages of the usable width):
 *
 *   +----------+----------------+------------+------------+
 *   | ACTIONS  |                | LVL 1      | ACCEL /    |
 *   |          |   mouse art    | LVL 2      | DECEL      |
 *   | (3/4 h)  |                |            | ANGLE SNAP |
 *   +----------+                |            | POLLING    |
 *   | MACRO    |  ILLUMINATION  |            |            |
 *   +----------+----------------+------------+------------+
 *   |  LIVE PREVIEW [ON]            REVERT  SAVE  (footer)|
 *   |  status line                                        |
 *   +-----------------------------------------------------+
 */
#include <stdio.h>
#include <string.h>

#include "accel.h"
#include "tui_internal.h"
#include "default_art.h"

#define MIN_COLS 100
#define MIN_LINES 28

struct rect {
	int y;
	int x;
	int h;
	int w;
};

static struct rect layout[PANE_COUNT];

/* height of the POWER box carved off the bottom of the CPI LEVELS column */
#define POWER_H 11

static void compute_layout(const struct tui *t)
{
	int main_h = LINES - 3;
	int left_w = COLS * 24 / 100;
	int sens_w = COLS * 22 / 100;
	int tune_w = COLS * 24 / 100;
	int center_w = COLS - left_w - sens_w - tune_w;
	int sens_x = left_w + center_w;
	/* wireless mice give the bottom of the sensitivity column to POWER */
	int power_h = (t->drv->caps & ALLOY_CAP_BATTERY) ? POWER_H : 0;
	int levels_h = main_h - power_h;

	layout[PANE_ACTIONS] = (struct rect){ 0, 0, main_h, left_w };
	layout[PANE_CENTER] = (struct rect){ 0, left_w, main_h, center_w };
	layout[PANE_LEVELS] = (struct rect){ 0, sens_x, levels_h, sens_w };
	layout[PANE_POWER] = (struct rect){ levels_h, sens_x, power_h, sens_w };
	layout[PANE_TUNING] =
		(struct rect){ 0, left_w + center_w + sens_w, main_h, tune_w };
	layout[PANE_FOOTER] = (struct rect){ LINES - 3, 0, 2, COLS };
}

void tui_zone_color_pairs(const struct tui *t)
{
	/*
	 * Same animated preview the illumination view uses, so the center-pane
	 * portrait breathes, cycles and tracks color live on the main screen
	 * too - not just frozen snapshot of the base zone colors.
	 */
	tui_zone_fx_pairs(t, tui_now_ms());
}

void tui_draw_pane_box(int y, int x, int h, int w, const char *title,
		       int focused)
{
	int attr = COLOR_PAIR(focused ? CLR_FRAME_FOCUS : CLR_FRAME);
	int i;

	if (focused)
		attr |= A_BOLD;
	attron(attr);
	mvaddch(y, x, ACS_ULCORNER);
	mvaddch(y, x + w - 1, ACS_URCORNER);
	mvaddch(y + h - 1, x, ACS_LLCORNER);
	mvaddch(y + h - 1, x + w - 1, ACS_LRCORNER);
	mvhline(y, x + 1, ACS_HLINE, w - 2);
	mvhline(y + h - 1, x + 1, ACS_HLINE, w - 2);
	mvvline(y + 1, x, ACS_VLINE, h - 2);
	mvvline(y + 1, x + w - 1, ACS_VLINE, h - 2);
	attroff(attr);

	/* clear the interior so stale content never bleeds through */
	for (i = 1; i < h - 1; i++)
		mvhline(y + i, x + 1, ' ', w - 2);

	if (title) {
		attron(COLOR_PAIR(CLR_TITLE) | A_BOLD);
		mvprintw(y, x + 2, " %s ", title);
		attroff(COLOR_PAIR(CLR_TITLE) | A_BOLD);
	}
}

static void draw_box(const struct rect *r, const char *title, int focused)
{
	tui_draw_pane_box(r->y, r->x, r->h, r->w, title, focused);
}

static const char *action_label(const struct alloy_action *act, char *buf,
				size_t len)
{
	switch (act->type) {
	case ALLOY_ACT_MOUSE:
		snprintf(buf, len, "Button %u", act->value);
		break;
	case ALLOY_ACT_DPI_CYCLE:
		snprintf(buf, len, "CPI Toggle");
		break;
	case ALLOY_ACT_SCROLL_UP:
		snprintf(buf, len, "Scroll Up");
		break;
	case ALLOY_ACT_SCROLL_DOWN:
		snprintf(buf, len, "Scroll Down");
		break;
	case ALLOY_ACT_KEYBOARD:
		snprintf(buf, len, "Key 0x%02X", act->value);
		break;
	case ALLOY_ACT_MEDIA:
		snprintf(buf, len, "Media 0x%02X", act->value);
		break;
	case ALLOY_ACT_DISABLED:
	default:
		snprintf(buf, len, "Disabled");
		break;
	}
	return buf;
}

static void draw_actions_pane(struct tui *t)
{
	const struct rect *r = &layout[PANE_ACTIONS];
	struct rect actions = *r;
	struct rect macro = *r;
	char label[32];
	int focused = t->focus == PANE_ACTIONS;
	int sel = t->cursor[PANE_ACTIONS];
	int launch_idx = t->drv->num_buttons;
	int i;

	actions.h = r->h * 3 / 4;
	macro.y = r->y + actions.h;
	macro.h = r->h - actions.h;

	draw_box(&actions, "ACTIONS", focused && sel < launch_idx);
	for (i = 0; i < t->drv->num_buttons; i++) {
		int row = actions.y + 2 + i * 2;

		if (row >= actions.y + actions.h - 1)
			break;
		if (focused && sel == i)
			attron(COLOR_PAIR(CLR_SELECTED));
		mvprintw(row, actions.x + 2, "%-*s",
			 ALLOY_MIN(actions.w - 4, 20), t->drv->buttons[i].name);
		if (focused && sel == i)
			attroff(COLOR_PAIR(CLR_SELECTED));

		attron(COLOR_PAIR(CLR_ACCENT));
		mvprintw(row + 1, actions.x + 4, "-> %s",
			 action_label(&t->cfg.buttons[i], label,
				      sizeof(label)));
		attroff(COLOR_PAIR(CLR_ACCENT));
	}

	draw_box(&macro, "MACRO EDITOR", focused && sel == launch_idx);
	if (focused && sel == launch_idx)
		attron(COLOR_PAIR(CLR_SELECTED));
	else
		attron(COLOR_PAIR(CLR_BUTTON));
	mvprintw(macro.y + macro.h / 2, macro.x + (macro.w - 10) / 2,
		 "  LAUNCH  ");
	attroff(COLOR_PAIR(CLR_SELECTED));
	attroff(COLOR_PAIR(CLR_BUTTON));
}

/*
 * Wireless status strip:
 * 2.4 GHz and Bluetooth link logos (lit in their own color when connected,
 * dimmed to grey otherwise) and a drawn battery whose fill is banded
 * green -> white -> yellow -> red as it drains.
 * Shown only for drivers that report a battery (ALLOY_CAP_BATTERY).
 * Wired mice never see it.
 */
static void draw_device_status(struct tui *t, const struct rect *r)
{
	int rf_on = t->battery_pct >= 0; /* receiver has a linked mouse */
	int cap = 10;
	int x = r->x + 3;
	int y = r->y + 1;
	int fill;
	int band;
	int i;

	tui_draw_pane_box(r->y, r->x, r->h, r->w, "DEVICE", 0);

	/* 2.4 GHz signal logo */
	attron(rf_on ? COLOR_PAIR(CLR_LINK_RF) | A_BOLD :
		       COLOR_PAIR(CLR_LINK_OFF) | A_DIM);
	mvaddstr(y, x, "▂▄▆ 2.4G");
	attrset(A_NORMAL);

	/* Bluetooth rune logo */
	attron(t->bt_present ? COLOR_PAIR(CLR_LINK_BT) | A_BOLD :
			       COLOR_PAIR(CLR_LINK_OFF) | A_DIM);
	mvaddstr(y, x + 13, "ᛒ BT");
	attrset(A_NORMAL);

	y = r->y + 3;
	if (!rf_on) {
		/*
		 * No mouse on the 2.4 GHz link.
		 * If the receiver can bind one, offer the PAIR button (opened with 'p');
		 * otherwise just note that the battery is only readable over the 2.4 GHz link
		 */
		if (tui_device_needs_pairing(t)) {
			attron(COLOR_PAIR(CLR_BUTTON) | A_BOLD);
			mvaddstr(y, x, " [p] PAIR ");
			attrset(A_NORMAL);
			attron(COLOR_PAIR(CLR_LINK_OFF) | A_DIM);
			addstr("  no mouse paired");
			attrset(A_NORMAL);
			return;
		}
		/* battery is only readable over the 2.4 GHz link */
		attron(COLOR_PAIR(CLR_LINK_OFF) | A_DIM);
		mvaddstr(y, x,
			 t->bt_present ? "battery  -- (on Bluetooth)" :
					 "battery  -- (no 2.4G link)");
		attrset(A_NORMAL);
		return;
	}

	if (t->battery_pct > 75)
		band = CLR_BAT_HIGH;
	else if (t->battery_pct > 50)
		band = CLR_BAT_GOOD;
	else if (t->battery_pct > 25)
		band = CLR_BAT_MID;
	else
		band = CLR_BAT_LOW;

	fill = (t->battery_pct * cap + 50) / 100;

	attron(COLOR_PAIR(CLR_FRAME));
	mvaddstr(y, x, "[");
	attrset(A_NORMAL);
	for (i = 0; i < cap; i++) {
		if (i < fill)
			attron(COLOR_PAIR(band) | A_BOLD);
		else
			attron(COLOR_PAIR(CLR_LINK_OFF) | A_DIM);
		addstr(i < fill ? "█" : "░");
		attrset(A_NORMAL);
	}
	attron(COLOR_PAIR(CLR_FRAME));
	addstr("]▮");
	attrset(A_NORMAL);

	attron(COLOR_PAIR(band) | A_BOLD);
	printw(" %d%%", t->battery_pct);
	attrset(A_NORMAL);
	if (t->battery_charging) {
		attron(COLOR_PAIR(CLR_BAT_HIGH) | A_BOLD);
		addstr(" CHG");
		attrset(A_NORMAL);
	}
}

static void draw_center_pane(struct tui *t)
{
	const struct rect *full = &layout[PANE_CENTER];
	struct rect r = *full;
	const char *art = t->drv->ascii_art ? t->drv->ascii_art :
					      alloy_default_mouse_art;
	int focused = t->focus == PANE_CENTER;
	int art_lines;
	int art_width;
	int y;
	int x;

	/* carve DEVICE status box off the top of the column for wireless mice */
	if (t->drv->caps & ALLOY_CAP_BATTERY) {
		struct rect dev = *full;

		dev.h = 5;
		draw_device_status(t, &dev);
		r.y = full->y + dev.h;
		r.h = full->h - dev.h;
	}

	draw_box(&r, t->drv->name, focused);

	tui_art_measure(art, &art_lines, &art_width);
	y = r.y + ALLOY_MAX(1, (r.h - 4 - art_lines) / 2);
	x = r.x + ALLOY_MAX(1, (r.w - art_width) / 2);
	tui_art_draw(t, art, y, x, r.y + r.h - 4, -1);

	/*
	 * gateway to the illumination view, centered under the art -
	 * only for devices with LED zones
	 * (see tui_pane_item_count: the pane is unfocusable without them)
	 */
	if (t->drv->num_zones) {
		y = r.y + r.h - 3;
		x = r.x + (r.w - 16) / 2;
		if (focused)
			attron(COLOR_PAIR(CLR_SELECTED));
		else
			attron(COLOR_PAIR(CLR_BUTTON));
		mvprintw(y, x, "  ILLUMINATION  ");
		attroff(COLOR_PAIR(CLR_SELECTED));
		attroff(COLOR_PAIR(CLR_BUTTON));
	}
}

static void draw_slider(int y, int x, int w, uint16_t min, uint16_t max,
			uint16_t val)
{
	int span = w - 2;
	int pos = (int)((long)(val - min) * (span - 1) / (max - min));
	int i;

	mvaddch(y, x, '[');
	for (i = 0; i < span; i++)
		addch(i == pos ? (chtype)(ACS_CKBOARD | A_BOLD) :
				 (chtype)ACS_HLINE);
	addch(']');
}

/*
 * Presets are drawn from the working config, one compact block each,
 * so the pane holds every preset the mouse supports;
 * CREATE button follows the last preset until the driver limit is reached.
 */
static void draw_levels_pane(struct tui *t)
{
	const struct rect *r = &layout[PANE_LEVELS];
	int focused = t->focus == PANE_LEVELS;
	int sel = t->cursor[PANE_LEVELS];
	int y = r->y + 2;
	int i;

	draw_box(r, "CPI LEVELS", focused);

	for (i = 0; i < t->cfg.dpi_count; i++) {
		int active = t->cfg.dpi_active == i;

		/*
		 * Cursor keeps its usual highlight;
		 * active preset stands out in the hot green even while
		 * the cursor sits elsewhere
		 */
		if (focused && sel == i)
			attron(COLOR_PAIR(CLR_SELECTED));
		else if (active)
			attron(COLOR_PAIR(CLR_BUTTON_HOT) | A_BOLD);
		mvprintw(y, r->x + 2, "LEVEL %d", i + 1);
		attroff(COLOR_PAIR(CLR_SELECTED));
		attroff(COLOR_PAIR(CLR_BUTTON_HOT) | A_BOLD);

		attron(COLOR_PAIR(CLR_ACCENT) | A_BOLD);
		mvprintw(y + 1, r->x + 2, "%5u CPI", t->cfg.dpi[i][0]);
		attroff(COLOR_PAIR(CLR_ACCENT) | A_BOLD);
		if (active) {
			attron(COLOR_PAIR(CLR_BUTTON_HOT));
			mvprintw(y + 1, r->x + 12, " ACTIVE ");
			attroff(COLOR_PAIR(CLR_BUTTON_HOT));
		}

		draw_slider(y + 2, r->x + 2, r->w - 4, t->drv->dpi.min,
			    t->drv->dpi.max, t->cfg.dpi[i][0]);
		y += 4;
	}

	if (t->cfg.dpi_count < tui_dpi_preset_limit(t)) {
		if (focused && sel == t->cfg.dpi_count)
			attron(COLOR_PAIR(CLR_SELECTED));
		else
			attron(COLOR_PAIR(CLR_BUTTON));
		mvprintw(y + 1, r->x + (r->w - 10) / 2, "  CREATE  ");
		attroff(COLOR_PAIR(CLR_SELECTED));
		attroff(COLOR_PAIR(CLR_BUTTON));
	}

	attron(COLOR_PAIR(CLR_DISABLED));
	mvprintw(r->y + r->h - 3, r->x + 2, "h/l: Adjust  H/L: Fast Adjust");
	mvprintw(r->y + r->h - 2, r->x + 2, "Enter: Set Active");
	attroff(COLOR_PAIR(CLR_DISABLED));
}

/*
 * Wireless power controls, sat under the CPI LEVELS column:
 * Battery Saver is the inactivity sleep-timer stepper (Off..20 min)
 * and Smart Illum toggles the idle lighting dim.
 * Shown only for drivers that report a battery (ALLOY_CAP_BATTERY).
 */
/* one POWER-pane label, highlighted when it is the focused item */
static void draw_power_label(int y, int x, const char *s, int selected)
{
	if (selected)
		attron(COLOR_PAIR(CLR_SELECTED));
	mvprintw(y, x, "%s", s);
	if (selected)
		attroff(COLOR_PAIR(CLR_SELECTED));
}

/* ON/OFF value row for a POWER-pane toggle */
static void draw_power_toggle(int y, int x, int on)
{
	attron(COLOR_PAIR(on ? CLR_BUTTON_HOT : CLR_DISABLED) | A_BOLD);
	mvprintw(y, x, "< %s >", on ? "ON " : "OFF");
	attroff(COLOR_PAIR(on ? CLR_BUTTON_HOT : CLR_DISABLED) | A_BOLD);
}

static void draw_power_pane(struct tui *t)
{
	const struct rect *r = &layout[PANE_POWER];
	int focused = t->focus == PANE_POWER;
	int sel = t->cursor[PANE_POWER];
	int has_higheff = (t->drv->caps & ALLOY_CAP_HIGH_EFFICIENCY) != 0;
	int y = r->y + 1;

	draw_box(r, "POWER", focused);

	/* Battery Saver: inactivity sleep timer */
	draw_power_label(y, r->x + 2, "Battery Saver",
			 focused && sel == POWER_SLEEP);
	attron(COLOR_PAIR(CLR_ACCENT) | A_BOLD);
	if (t->cfg.sleep_min)
		mvprintw(y + 1, r->x + 4, "< %2d min >", t->cfg.sleep_min);
	else
		mvprintw(y + 1, r->x + 4, "<  Off   >");
	attroff(COLOR_PAIR(CLR_ACCENT) | A_BOLD);

	/* Smart Illum: blank the LEDs while moving */
	y += 2;
	draw_power_label(y, r->x + 2, "Smart Illum",
			 focused && sel == POWER_SMART);
	draw_power_toggle(y + 1, r->x + 4, t->cfg.illum_smart);

	/* Dim Timer: dim the LEDs after N seconds idle */
	y += 2;
	draw_power_label(y, r->x + 2, "Dim Timer", focused && sel == POWER_DIM);
	attron(COLOR_PAIR(CLR_ACCENT) | A_BOLD);
	if (t->cfg.illum_dim_s)
		mvprintw(y + 1, r->x + 4, "< %4d s >", t->cfg.illum_dim_s);
	else
		mvprintw(y + 1, r->x + 4, "<  Off   >");
	attroff(COLOR_PAIR(CLR_ACCENT) | A_BOLD);

	/* High-Efficiency Mode: only for drivers that advertise it */
	if (has_higheff) {
		y += 2;
		draw_power_label(y, r->x + 2, "High-Efficiency",
				 focused && sel == POWER_HIGHEFF);
		draw_power_toggle(y + 1, r->x + 4, t->cfg.high_efficiency);
	}

	attron(COLOR_PAIR(CLR_DISABLED));
	mvprintw(r->y + r->h - 2, r->x + 2, "h/l: Adjust  Enter: Toggle");
	attroff(COLOR_PAIR(CLR_DISABLED));
}

/*
 * Square wave of the polling rate, drawn with real edges:
 * top-row plateaus for the high level, bottom-row plateaus for the low level,
 * vertical lines joining them.
 * Faster rate shortens the period, so the pulses pack tighter and more full
 * cycles fit across the width the closer we get to the driver's fastest rate.
 */
static void draw_poll_wave(int y, int x, int w, int h, uint16_t hz,
			   uint16_t max_hz)
{
	int half = (hz && max_hz) ? ALLOY_MAX(2, 2 * (int)max_hz / (int)hz) : 2;
	int period = half * 2;
	int i, row;

	for (i = 0; i < w; i++) {
		int phase = i % period;
		int col = x + i;

		if (phase == 0 && i > 0) {
			/* rising edge: low plateau (left) up to high (right) */
			mvaddch(y, col, ACS_ULCORNER);
			for (row = 1; row < h - 1; row++)
				mvaddch(y + row, col, ACS_VLINE);
			mvaddch(y + h - 1, col, ACS_LRCORNER);
		} else if (phase == half) {
			/* falling edge: high plateau (left) down to low (right) */
			mvaddch(y, col, ACS_URCORNER);
			for (row = 1; row < h - 1; row++)
				mvaddch(y + row, col, ACS_VLINE);
			mvaddch(y + h - 1, col, ACS_LLCORNER);
		} else if (phase < half) {
			mvaddch(y, col, ACS_HLINE); /* high plateau */
		} else {
			mvaddch(y + h - 1, col, ACS_HLINE); /* low plateau */
		}
	}
}

/*
 * Dotted pointer trajectory:
 * wobbly hand motion that straightens out as the snapping cone widens.
 * Three rows tall; diamonds are the start and end of the stroke.
 */
static void draw_snap_wave(int y, int x, int w, uint8_t snap)
{
	/* one period of sin() scaled to +-100, 16 samples */
	static const int8_t sine[16] = { 0, 38,	 71,  92,  100,	 92,  71,  38,
					 0, -38, -71, -92, -100, -92, -71, -38 };
	int amp = 100 - (int)snap * 100 / ALLOY_SNAP_MAX;
	int i;

	mvaddch(y + 1, x, ACS_DIAMOND);
	mvaddch(y + 1, x + w - 1, ACS_DIAMOND);
	for (i = 1; i < w - 1; i++) {
		int v = sine[(i * 32 / w) % 16] * amp / 100;
		int row = v > 50 ? 0 : v < -50 ? 2 : 1;

		mvaddch(y + row, x + i, ACS_BULLET);
	}
}

/*
 * Map a fixed-point gain onto a graph row.
 * Visible range is exactly what the transform can reach at the reference speed:
 * 0.25x (bottom row) through 1.00x (middle) to 1.75x (top row).
 */
static int gain_to_row(int32_t g, int graph_h)
{
	const int32_t top = ALLOY_ACCEL_FP * 7 / 4;
	const int32_t span = ALLOY_ACCEL_FP * 3 / 2;
	int row = (int)(((int64_t)(top - g) * (graph_h - 1) + span / 2) / span);

	return ALLOY_CLAMP(row, 0, graph_h - 1);
}

static void draw_tuning_pane(struct tui *t)
{
	const struct rect *r = &layout[PANE_TUNING];
	struct alloy_accel_params ap;
	int focused = t->focus == PANE_TUNING;
	int sel = t->cursor[PANE_TUNING];
	int graph_h = 5;
	int gx = r->x + 9;
	int gw = r->w - 14;
	int prev = -1;
	int y = r->y + 2;
	int i;

	draw_box(r, "TUNING", focused);

	mvprintw(y, r->x + 2, "ACCELERATION / DECELERATION");
	y++;

	/*
	 * Y-axis title:
	 * curses cannot rotate glyphs and the graph is too short for one letter per row,
	 * so it sits right-aligned above the curve
	 */
	attron(COLOR_PAIR(CLR_DISABLED));
	mvprintw(y, gx + gw - (int)strlen("SENSITIVITY"), "SENSITIVITY");
	attroff(COLOR_PAIR(CLR_DISABLED));
	mvaddch(y, r->x + 8, ACS_UARROW);
	y++;

	/* gain ticks on the Y axis */
	for (i = 0; i < graph_h; i++)
		mvaddch(y + i, r->x + 8, ACS_VLINE);
	attron(COLOR_PAIR(CLR_DISABLED));
	mvprintw(y, r->x + 2, "1.75x");
	mvprintw(y + graph_h / 2, r->x + 2, "1.00x");
	mvprintw(y + graph_h - 1, r->x + 2, "0.25x");
	attroff(COLOR_PAIR(CLR_DISABLED));
	mvaddch(y, r->x + 8, ACS_RTEE);
	mvaddch(y + graph_h / 2, r->x + 8, ACS_RTEE);
	mvaddch(y + graph_h - 1, r->x + 8, ACS_RTEE);
	mvaddch(y + graph_h, r->x + 8, ACS_LLCORNER);
	mvhline(y + graph_h, gx, ACS_HLINE, gw);
	mvaddch(y + graph_h, r->x + r->w - 5, ACS_RARROW);
	attron(COLOR_PAIR(CLR_DISABLED));
	mvprintw(y + graph_h + 1, r->x + 4, "%.*s", r->w - 6,
		 "SPEED OF HAND MOVEMENT");
	attroff(COLOR_PAIR(CLR_DISABLED));

	/*
	 * Curve is the exact speed-to-gain response the daemon applies (alloy_accel_gain_fp):
	 * flat 1.0x when neutral, ramping up with acceleration or down with
	 * deceleration until the ramp saturates.
	 * Hand speed sweeps 0..30 counts/event across the axis,
	 * so saturation (20 counts/event) lands two thirds of the way along.
	 * Row steps are joined with corner and vertical glyphs to keep the line continuous.
	 */
	alloy_accel_from_config(&t->cfg, &ap);
	attron(COLOR_PAIR(CLR_ACCENT));
	for (i = 0; i < gw; i++) {
		int s = i * 30 / (gw - 1);
		int row = gain_to_row(alloy_accel_gain_fp(&ap, (int64_t)s * s),
				      graph_h);
		int rr;

		if (prev < 0 || row == prev) {
			mvaddch(y + row, gx + i, ACS_HLINE);
		} else if (row < prev) { /* gain rising: turn upward */
			mvaddch(y + prev, gx + i, ACS_LRCORNER);
			for (rr = row + 1; rr < prev; rr++)
				mvaddch(y + rr, gx + i, ACS_VLINE);
			mvaddch(y + row, gx + i, ACS_ULCORNER);
		} else { /* gain falling: turn downward */
			mvaddch(y + prev, gx + i, ACS_URCORNER);
			for (rr = prev + 1; rr < row; rr++)
				mvaddch(y + rr, gx + i, ACS_VLINE);
			mvaddch(y + row, gx + i, ACS_LLCORNER);
		}
		prev = row;
	}
	attroff(COLOR_PAIR(CLR_ACCENT));

	y += graph_h + 3;

	for (i = 0; i < 2; i++) {
		const char *name = i == 0 ? "Acceleration" : "Deceleration";
		int8_t val = i == 0 ? t->cfg.acceleration : t->cfg.deceleration;

		if (focused && sel == i)
			attron(COLOR_PAIR(CLR_SELECTED));
		mvprintw(y, r->x + 2, "%-13s", name);
		if (focused && sel == i)
			attroff(COLOR_PAIR(CLR_SELECTED));
		mvprintw(y, r->x + 16, "< %3d >", val);
		y++;
	}
	y += 2;
	mvprintw(y, r->x + 2, "ANGLE SNAPPING");
	y++;
	attron(COLOR_PAIR(CLR_ACCENT));
	draw_snap_wave(y, r->x + 3, r->w - 6, t->cfg.angle_snapping);
	attroff(COLOR_PAIR(CLR_ACCENT));
	y += 4;
	if (focused && sel == 2)
		attron(COLOR_PAIR(CLR_SELECTED));
	mvprintw(y, r->x + 2, "%-13s", "Snapping");
	if (focused && sel == 2)
		attroff(COLOR_PAIR(CLR_SELECTED));
	mvprintw(y, r->x + 16, "< %3u deg >", t->cfg.angle_snapping);
	y++;

	if (focused && sel == 3)
		attron(COLOR_PAIR(CLR_SELECTED));
	mvprintw(y, r->x + 2, "%-13s", "Engine");
	if (focused && sel == 3)
		attroff(COLOR_PAIR(CLR_SELECTED));
	attron(COLOR_PAIR(t->accel_running ? CLR_BUTTON_HOT : CLR_DISABLED));
	mvprintw(y, r->x + 16, " %s ", t->accel_running ? "ON " : "OFF");
	attroff(COLOR_PAIR(t->accel_running ? CLR_BUTTON_HOT : CLR_DISABLED));
	y += 2;

	/*
	 * Polling rate, only for devices that expose it
	 * (Bluetooth locks it out, so the row is absent there -
	 * see tui_pane_item_count for PANE_TUNING).
	 */
	if (!t->drv->num_polling_rates)
		return;

	mvprintw(y, r->x + 2, "POLLING RATE");
	y++;
	attron(COLOR_PAIR(CLR_ACCENT));
	draw_poll_wave(y, r->x + 3, r->w - 6, 3, t->cfg.polling_hz,
		       t->drv->polling_rates[0]);
	attroff(COLOR_PAIR(CLR_ACCENT));
	y += 4; /* chart height + blank line before the stepper */

	/*
	 * stepper mirrors the CPI level entries:
	 * highlighted label, bold accent value and slider over the ladder;
	 * h/l steps, H/L jumps
	 */
	if (focused && sel == 4)
		attron(COLOR_PAIR(CLR_SELECTED));
	mvprintw(y, r->x + 2, "%-13s", "Rate");
	if (focused && sel == 4)
		attroff(COLOR_PAIR(CLR_SELECTED));
	attron(COLOR_PAIR(CLR_ACCENT) | A_BOLD);
	mvprintw(y, r->x + 16, "%4u Hz", t->cfg.polling_hz);
	attroff(COLOR_PAIR(CLR_ACCENT) | A_BOLD);
	y++;
	if (t->drv->num_polling_rates > 1)
		draw_slider(
			y, r->x + 2, r->w - 4,
			t->drv->polling_rates[t->drv->num_polling_rates - 1],
			t->drv->polling_rates[0], t->cfg.polling_hz);
}

static void draw_footer(struct tui *t)
{
	const struct rect *r = &layout[PANE_FOOTER];
	int focused = t->focus == PANE_FOOTER;
	int sel = t->cursor[PANE_FOOTER];
	int x;

	mvhline(r->y, r->x, ACS_HLINE, r->w);

	if (focused && sel == FOOTER_LIVE_PREVIEW)
		attron(COLOR_PAIR(CLR_SELECTED));
	mvprintw(r->y + 1, 2, " LIVE PREVIEW %s ",
		 t->live_preview ? "[ON] " : "[OFF]");
	if (focused && sel == FOOTER_LIVE_PREVIEW)
		attroff(COLOR_PAIR(CLR_SELECTED));

	if (t->firmware[0])
		mvprintw(r->y + 1, 24, "fw %s", t->firmware);

	x = COLS - 22;
	if (focused && sel == FOOTER_REVERT)
		attron(COLOR_PAIR(CLR_SELECTED));
	else
		attron(COLOR_PAIR(CLR_BUTTON));
	mvprintw(r->y + 1, x, " REVERT ");
	attroff(COLOR_PAIR(CLR_SELECTED));
	attroff(COLOR_PAIR(CLR_BUTTON));

	x += 10;
	if (focused && sel == FOOTER_SAVE)
		attron(COLOR_PAIR(CLR_SELECTED));
	else
		attron(COLOR_PAIR(CLR_BUTTON_HOT));
	mvprintw(r->y + 1, x, " SAVE%s ", t->dirty ? "*" : " ");
	attroff(COLOR_PAIR(CLR_SELECTED));
	attroff(COLOR_PAIR(CLR_BUTTON_HOT));

	mvhline(LINES - 1, 0, ' ', COLS);
	attron(COLOR_PAIR(CLR_DISABLED));
	mvprintw(LINES - 1, 2, "%s", t->status);
	mvprintw(LINES - 1, COLS - 44,
		 "Tab: Pane  Enter: Select  s: Save  q: Quit");
	attroff(COLOR_PAIR(CLR_DISABLED));
}

/*
 * Paint the whole main screen into the curses virtual screen but do NOT refresh
 * - the caller decides when to flush.
 * Modals render the background this way and then draw themselves on top,
 * so single refresh() composites the two atomically;
 */
void tui_render(struct tui *t)
{
	erase();

	if (COLS < MIN_COLS || LINES < MIN_LINES) {
		mvprintw(0, 0, "Terminal too small: need at least %dx%d",
			 MIN_COLS, MIN_LINES);
		return;
	}

	compute_layout(t);
	tui_zone_color_pairs(t);

	draw_actions_pane(t);
	draw_center_pane(t);
	draw_levels_pane(t);
	if (t->drv->caps & ALLOY_CAP_BATTERY)
		draw_power_pane(t);
	draw_tuning_pane(t);
	draw_footer(t);
}

void tui_draw(struct tui *t)
{
	tui_render(t);
	refresh();
}
