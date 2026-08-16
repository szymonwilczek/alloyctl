// SPDX-License-Identifier: GPL-2.0-only
/*
 * ASCII art rendering with paint-group markup.
 *
 * Art strings may prefix any character with "$N" (N = 1..8) to hand that single
 * character to the driver for coloring:
 * the front-end calls desc->art_cell() with N - 1 as the group and the cell's
 * position in the art.
 *
 * What a group means - LED zone, a key row, a battery segment - is entirely
 * the driver's affair; this file only asks and paints.
 *
 * "$i" paints the single following character in the front-end's accent tint
 * and "$$" renders a literal dollar.
 * Markers take no cell, so the art's rendered width counts visible characters only.
 *
 * Art without markup is offered to the driver too, with group -1,
 * so a device that tints its whole portrait need not mark every character.
 */
#include "tui_internal.h"

/* does the marker at p start a paint group? */
static int is_group_marker(const char *p)
{
	return p[0] == '$' && p[1] >= '1' && p[1] <= '8' && p[2] != '\0' &&
	       p[2] != '\n';
}

/* "$i" paints the single following character in the accent tint */
static int is_info_marker(const char *p)
{
	return p[0] == '$' && p[1] == 'i' && p[2] != '\0' && p[2] != '\n';
}

int tui_art_has_markup(const char *art)
{
	const char *p;

	for (p = art; *p; p++) {
		if (is_group_marker(p) || is_info_marker(p))
			return 1;
		if (p[0] == '$' && p[1] == '$')
			p++;
	}
	return 0;
}

/* rendered geometry: markers are invisible and take no width */
void tui_art_measure(const char *art, int *lines, int *width)
{
	const char *p;
	int w = 0;
	int cur = 0;
	int n = 0;

	for (p = art; *p; p++) {
		if (*p == '\n') {
			n++;
			w = ALLOY_MAX(w, cur);
			cur = 0;
			continue;
		}
		if (is_group_marker(p) || is_info_marker(p))
			p += 2; /* skip the selector; the char counts below */
		else if (p[0] == '$' && p[1] == '$')
			p++; /* literal dollar renders one cell */
		cur++;
	}

	*lines = n;
	*width = ALLOY_MAX(w, cur);
}

void tui_art_draw(struct alloy_ui *ui, const char *art, int y, int x, int max_y,
		  int max_x)
{
	int (*art_cell)(struct alloy_ui *, int, int, int, long,
			struct alloy_rgb *) = TUI_HOOK(ui, art_cell);
	int marked = tui_art_has_markup(art);
	long ms = tui_now_ms();
	const char *p;
	int cur_y = y;
	int cur_row = 0;
	int cur_col = 0;
	int group;
	int info;

	move(cur_y, x);
	for (p = art; *p && cur_y < max_y; p++) {
		struct alloy_rgb rgb;

		if (*p == '\n') {
			cur_y++;
			cur_row++;
			cur_col = 0;
			move(cur_y, x);
			continue;
		}

		/* unmarked art is offered wholesale, as group -1 */
		group = marked ? -2 : -1;
		info = 0;
		if (is_group_marker(p)) {
			group = p[1] - '1';
			p += 2;
		} else if (is_info_marker(p)) {
			info = 1;
			p += 2;
		} else if (p[0] == '$' && p[1] == '$') {
			p++;
		}

		if (x + cur_col >= max_x) {
			cur_col++;
			continue;
		}

		/*
		 * Tab would smear the art past the pane border,
		 * because the measured geometry counts it as one column while
		 * the terminal expands it.
		 * Render it as the single cell it was measured as.
		 */
		if (*p == '\t') {
			addch(' ');
			cur_col++;
			continue;
		}

		if (info && COLORS >= 8) {
			addch((chtype)*p | (chtype)COLOR_PAIR(CLR_INFO));
		} else if (group != -2 && COLORS >= 8 && art_cell &&
			   art_cell(ui, group, cur_row, cur_col, ms, &rgb)) {
			addch((chtype)*p | (chtype)tui_rgb_attr(&rgb));
		} else {
			addch((chtype)*p);
		}
		cur_col++;
	}
}
