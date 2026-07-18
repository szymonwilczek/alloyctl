// SPDX-License-Identifier: GPL-2.0-only
/*
 * Illumination view.
 *
 * Full-screen lighting editor reached through the ILLUMINATION button:
 * the left third is the EFFECTS pane editing one zone, the right two thirds
 * preview the mouse with every zone drawn in its current color.
 *
 * Zone selection lives in the preview pane:
 *	TAB cycles the zone tabs and ENTER commits the choice,
 *	dropping focus straight into the EFFECTS pane so the zone
 *	can be edited without extra keystrokes.
 *	ESC leaves the view.
 */
#include <string.h>

#include "tui_internal.h"
#include "default_art.h"

void tui_illum_enter(struct tui *t)
{
	t->view = VIEW_ILLUM;
	t->illum_zone = 0; /* smallest zone is the default */
	t->illum_tab = 0;
	t->illum_cursor = 0;
	t->illum_focus = ILLUM_FOCUS_PREVIEW;
}

static void draw_zone_tabs(struct tui *t, int y, int x, int w)
{
	int focused = t->illum_focus == ILLUM_FOCUS_PREVIEW;
	int i;

	(void)w;
	for (i = 0; i < t->drv->num_zones; i++) {
		int active = t->illum_zone == i;
		int hot = focused && t->illum_tab == i;

		if (hot)
			attron(COLOR_PAIR(CLR_SELECTED));
		else if (active)
			attron(COLOR_PAIR(CLR_ACCENT) | A_BOLD);
		else
			attron(COLOR_PAIR(CLR_BUTTON));
		mvprintw(y, x, " Z%d:%s ", i + 1, t->drv->zones[i].name);
		attroff(COLOR_PAIR(CLR_SELECTED));
		attroff(COLOR_PAIR(CLR_ACCENT) | A_BOLD);
		attroff(COLOR_PAIR(CLR_BUTTON));

		x += (int)strlen(t->drv->zones[i].name) + 6;
	}
}

/*
 * Art carries no zone coordinates, so the preview tints it in horizontal bands:
 * line N of the art belongs to zone N * num_zones / art_lines,
 * matching the top-to-bottom zone order every supported mouse uses.
 */
static void draw_mouse_preview(struct tui *t, int py, int px, int ph, int pw)
{
	const char *art = t->drv->ascii_art ? t->drv->ascii_art :
					      alloy_default_mouse_art;
	const char *p;
	int art_lines = 0;
	int art_width = 0;
	int cur = 0;
	int line = 0;
	int zones = ALLOY_MAX(t->drv->num_zones, 1);
	int y;
	int x;

	for (p = art; *p; p++) {
		if (*p == '\n') {
			art_lines++;
			art_width = ALLOY_MAX(art_width, cur);
			cur = 0;
		} else {
			cur++;
		}
	}

	y = py + ALLOY_MAX(1, (ph - art_lines) / 2);
	x = px + ALLOY_MAX(1, (pw - art_width) / 2);

	move(y, x);
	for (p = art; *p && y < py + ph; p++) {
		if (*p == '\n') {
			line++;
			y++;
			move(y, x);
		} else {
			int zone = line * zones / ALLOY_MAX(art_lines, 1);
			int pair = COLORS >= 256 ? CLR_ZONE_BASE + zone :
						   CLR_FRAME;

			if (zone == t->illum_zone)
				addch((chtype)*p | COLOR_PAIR(pair) | A_BOLD);
			else
				addch((chtype)*p | COLOR_PAIR(pair));
		}
	}
}

static void draw_effects_pane(struct tui *t, int y, int x, int h, int w)
{
	int focused = t->illum_focus == ILLUM_FOCUS_EFFECTS;

	tui_draw_pane_box(y, x, h, w, "EFFECTS", focused);

	attron(COLOR_PAIR(CLR_ACCENT) | A_BOLD);
	mvprintw(y + 2, x + 2, "ZONE Z%d: %s", t->illum_zone + 1,
		 t->drv->zones[t->illum_zone].name);
	attroff(COLOR_PAIR(CLR_ACCENT) | A_BOLD);
}

void tui_illum_draw(struct tui *t)
{
	int main_h = LINES - 2;
	int left_w = COLS / 3;
	int right_w = COLS - left_w;

	erase();
	tui_zone_color_pairs(t);

	draw_effects_pane(t, 0, 0, main_h, left_w);
	tui_draw_pane_box(0, left_w, main_h, right_w, t->drv->name,
			  t->illum_focus == ILLUM_FOCUS_PREVIEW);
	draw_zone_tabs(t, 2, left_w + 3, right_w - 4);
	draw_mouse_preview(t, 3, left_w + 1, main_h - 4, right_w - 2);

	mvhline(LINES - 2, 0, ACS_HLINE, COLS);
	attron(COLOR_PAIR(CLR_DISABLED));
	mvprintw(LINES - 1, 2, "%s", t->status);
	mvprintw(LINES - 1, COLS - 44,
		 "tab: zone  enter: edit zone  esc: back");
	attroff(COLOR_PAIR(CLR_DISABLED));

	refresh();
}

void tui_illum_handle_key(struct tui *t, int ch)
{
	switch (ch) {
	case 27:
	case 'q':
		t->view = VIEW_MAIN;
		return;
	case '\t':
		if (t->illum_focus == ILLUM_FOCUS_PREVIEW)
			t->illum_tab = (t->illum_tab + 1) % t->drv->num_zones;
		else
			t->illum_focus = ILLUM_FOCUS_PREVIEW;
		return;
	case KEY_BTAB:
		if (t->illum_focus == ILLUM_FOCUS_PREVIEW)
			t->illum_tab = (t->illum_tab + t->drv->num_zones - 1) %
				       t->drv->num_zones;
		return;
	case '\n':
	case KEY_ENTER:
		if (t->illum_focus == ILLUM_FOCUS_PREVIEW) {
			t->illum_zone = t->illum_tab;
			t->illum_focus = ILLUM_FOCUS_EFFECTS;
		}
		return;
	case KEY_RESIZE:
	default:
		return;
	}
}
