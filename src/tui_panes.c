// SPDX-License-Identifier: GPL-2.0-only
/*
 * Pane layout and rendering.
 *
 * Screen layout (percentages of the usable width):
 *
 *   +----------+----------------+------------+------------+
 *   | ACTIONS  |                | SENS 1     | ACCEL /    |
 *   |          |   mouse art    | SENS 2     | DECEL      |
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

static void compute_layout(void)
{
	int main_h = LINES - 3;
	int left_w = COLS * 24 / 100;
	int sens_w = COLS * 22 / 100;
	int tune_w = COLS * 24 / 100;
	int center_w = COLS - left_w - sens_w - tune_w;

	layout[PANE_ACTIONS] = (struct rect){ 0, 0, main_h, left_w };
	layout[PANE_CENTER] = (struct rect){ 0, left_w, main_h, center_w };
	layout[PANE_SENSITIVITY] =
		(struct rect){ 0, left_w + center_w, main_h, sens_w };
	layout[PANE_TUNING] =
		(struct rect){ 0, left_w + center_w + sens_w, main_h, tune_w };
	layout[PANE_FOOTER] = (struct rect){ LINES - 3, 0, 2, COLS };
}

void tui_zone_color_pairs(const struct tui *t)
{
	uint8_t i;

	if (COLORS < 256)
		return;

	for (i = 0; i < t->drv->num_zones && i < ALLOY_MAX_LED_ZONES; i++) {
		const struct alloy_rgb *c = &t->cfg.zone_color[i];
		short cube = (short)(16 + 36 * (c->r / 51) + 6 * (c->g / 51) +
				     (c->b / 51));

		init_pair((short)(CLR_ZONE_BASE + i), cube, -1);
	}
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
 * Every lighting control lives in the illumination view;
 * center pane is just the mouse portrait and the way in.
 */
static void draw_center_pane(struct tui *t)
{
	const struct rect *r = &layout[PANE_CENTER];
	const char *art = t->drv->ascii_art ? t->drv->ascii_art :
					      alloy_default_mouse_art;
	const char *p = art;
	int focused = t->focus == PANE_CENTER;
	int art_lines = 0;
	int art_width = 0;
	int cur = 0;
	int y;
	int x;

	draw_box(r, t->drv->name, focused);

	for (p = art; *p; p++) {
		if (*p == '\n') {
			art_lines++;
			art_width = ALLOY_MAX(art_width, cur);
			cur = 0;
		} else {
			cur++;
		}
	}

	y = r->y + ALLOY_MAX(1, (r->h - 4 - art_lines) / 2);
	x = r->x + ALLOY_MAX(1, (r->w - art_width) / 2);

	move(y, x);
	for (p = art; *p && y < r->y + r->h - 4; p++) {
		if (*p == '\n') {
			y++;
			move(y, x);
		} else {
			addch((chtype)*p);
		}
	}

	/* gateway to the illumination view, centered under the art */
	y = r->y + r->h - 3;
	x = r->x + (r->w - 16) / 2;
	if (focused)
		attron(COLOR_PAIR(CLR_SELECTED));
	else
		attron(COLOR_PAIR(CLR_BUTTON));
	mvprintw(y, x, "  ILLUMINATION  ");
	attroff(COLOR_PAIR(CLR_SELECTED));
	attroff(COLOR_PAIR(CLR_BUTTON));
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

static void draw_sensitivity_pane(struct tui *t)
{
	const struct rect *r = &layout[PANE_SENSITIVITY];
	int focused = t->focus == PANE_SENSITIVITY;
	int sel = t->cursor[PANE_SENSITIVITY];
	int i;

	draw_box(r, "SENSITIVITY", focused);

	for (i = 0; i < 2; i++) {
		int y = r->y + 2 + i * 6;
		int active = t->cfg.dpi_active == i;

		if (focused && sel == i)
			attron(COLOR_PAIR(CLR_SELECTED));
		mvprintw(y, r->x + 2, "SENSITIVITY %d (CPI %d)", i + 1, i + 1);
		if (focused && sel == i)
			attroff(COLOR_PAIR(CLR_SELECTED));

		attron(COLOR_PAIR(CLR_ACCENT) | A_BOLD);
		mvprintw(y + 2, r->x + (r->w - 16) / 2, "%5u",
			 t->cfg.dpi[i][0]);
		attroff(COLOR_PAIR(CLR_ACCENT) | A_BOLD);
		printw(" CPI");
		if (active) {
			attron(COLOR_PAIR(CLR_ACCENT) | A_BOLD);
			printw("  ACTIVE");
			attroff(COLOR_PAIR(CLR_ACCENT) | A_BOLD);
		}

		draw_slider(y + 3, r->x + 2, r->w - 4, t->drv->dpi.min,
			    t->drv->dpi.max, t->cfg.dpi[i][0]);
		mvprintw(y + 4, r->x + 2, "%u", t->drv->dpi.min);
		mvprintw(y + 4, r->x + r->w - 2 - 4, "%u", t->drv->dpi.max);
	}

	attron(COLOR_PAIR(CLR_DISABLED));
	mvprintw(r->y + r->h - 3, r->x + 2, "h/l: adjust  H/L: fast");
	mvprintw(r->y + r->h - 2, r->x + 2, "a: set active preset");
	attroff(COLOR_PAIR(CLR_DISABLED));
}

static void draw_wave(int y, int x, int w, uint16_t hz)
{
	/* square wave; higher polling rate = denser pulses */
	int period = hz >= 1000 ? 2 : hz >= 500 ? 3 : hz >= 250 ? 5 : 8;
	int i;

	for (i = 0; i < w; i++) {
		int in_high = (i % period) < (period + 1) / 2;

		mvaddch(y, x + i, in_high ? ACS_S1 : ' ');
		mvaddch(y + 1, x + i, in_high ? ' ' : ACS_S9);
	}
}

static void draw_tuning_pane(struct tui *t)
{
	const struct rect *r = &layout[PANE_TUNING];
	int focused = t->focus == PANE_TUNING;
	int sel = t->cursor[PANE_TUNING];
	int has_accel = t->drv->caps & ALLOY_CAP_ACCELERATION;
	int has_decel = t->drv->caps & ALLOY_CAP_DECELERATION;
	int has_snap = t->drv->caps & ALLOY_CAP_ANGLE_SNAPPING;
	int graph_h = 6;
	int y = r->y + 2;
	int i;

	draw_box(r, "TUNING", focused);

	mvprintw(y, r->x + 2, "ACCELERATION / DECELERATION");
	y++;

	/* SPEED OF HAND MOVEMENT vs SENSITIVITY graph */
	for (i = 0; i < graph_h; i++)
		mvaddch(y + i, r->x + 3, i == 0 ? ACS_UARROW : ACS_VLINE);
	mvaddch(y + graph_h, r->x + 3, ACS_LLCORNER);
	mvhline(y + graph_h, r->x + 4, ACS_HLINE, r->w - 9);
	mvaddch(y + graph_h, r->x + r->w - 5, ACS_RARROW);
	attron(COLOR_PAIR(CLR_DISABLED));
	mvprintw(y + graph_h + 1, r->x + 4, "SPEED OF HAND MOVEMENT");
	attroff(COLOR_PAIR(CLR_DISABLED));

	if (has_accel || has_decel) {
		/* curve bends up with accel, down with decel */
		int mid = graph_h / 2 - t->cfg.acceleration / 4 +
			  t->cfg.deceleration / 4;

		attron(COLOR_PAIR(CLR_ACCENT));
		mvhline(y + ALLOY_CLAMP(mid, 0, graph_h - 1), r->x + 4,
			ACS_HLINE, r->w - 9);
		attroff(COLOR_PAIR(CLR_ACCENT));
	} else {
		attron(COLOR_PAIR(CLR_ACCENT));
		mvhline(y + graph_h / 2, r->x + 4, ACS_HLINE, r->w - 9);
		attroff(COLOR_PAIR(CLR_ACCENT));
	}

	y += graph_h + 2;

	for (i = 0; i < 2; i++) {
		const char *name = i == 0 ? "Acceleration" : "Deceleration";
		int supported = i == 0 ? has_accel : has_decel;
		int8_t val = i == 0 ? t->cfg.acceleration : t->cfg.deceleration;

		if (focused && sel == i)
			attron(COLOR_PAIR(CLR_SELECTED));
		mvprintw(y, r->x + 2, "%-13s", name);
		if (focused && sel == i)
			attroff(COLOR_PAIR(CLR_SELECTED));
		if (supported) {
			mvprintw(y, r->x + 16, "< %3d >", val);
		} else {
			attron(COLOR_PAIR(CLR_DISABLED));
			mvprintw(y, r->x + 16, "N/A (device)");
			attroff(COLOR_PAIR(CLR_DISABLED));
		}
		y++;
	}

	y++;
	mvprintw(y, r->x + 2, "ANGLE SNAPPING");
	y++;
	if (focused && sel == 2)
		attron(COLOR_PAIR(CLR_SELECTED));
	mvprintw(y, r->x + 2, "%-13s", "Snapping");
	if (focused && sel == 2)
		attroff(COLOR_PAIR(CLR_SELECTED));
	if (has_snap) {
		mvprintw(y, r->x + 16, "< %3u >", t->cfg.angle_snapping);
	} else {
		attron(COLOR_PAIR(CLR_DISABLED));
		mvprintw(y, r->x + 16, "N/A (device)");
		attroff(COLOR_PAIR(CLR_DISABLED));
	}
	y += 2;

	mvprintw(y, r->x + 2, "POLLING RATE");
	y++;
	attron(COLOR_PAIR(CLR_ACCENT));
	draw_wave(y, r->x + 3, r->w - 6, t->cfg.polling_hz);
	attroff(COLOR_PAIR(CLR_ACCENT));
	y += 2;
	if (focused && sel == 3)
		attron(COLOR_PAIR(CLR_SELECTED));
	mvprintw(y, r->x + 2, "%-13s", "Rate");
	if (focused && sel == 3)
		attroff(COLOR_PAIR(CLR_SELECTED));
	mvprintw(y, r->x + 16, "< %4u Hz >", t->cfg.polling_hz);
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
	mvprintw(LINES - 1, COLS - 34, "tab: pane  enter: select  q: quit");
	attroff(COLOR_PAIR(CLR_DISABLED));
}

void tui_draw(struct tui *t)
{
	erase();

	if (COLS < MIN_COLS || LINES < MIN_LINES) {
		mvprintw(0, 0, "terminal too small: need at least %dx%d",
			 MIN_COLS, MIN_LINES);
		refresh();
		return;
	}

	compute_layout();
	tui_zone_color_pairs(t);

	draw_actions_pane(t);
	draw_center_pane(t);
	draw_sensitivity_pane(t);
	draw_tuning_pane(t);
	draw_footer(t);

	refresh();
}
