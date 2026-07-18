// SPDX-License-Identifier: GPL-2.0-only
/*
 * ASCII art rendering with zone color markup.
 *
 * Art strings may prefix any character with "$N" (N = 1..8) to paint that single
 * character in the live color of LED zone N.
 * "$$" renders literal dollar.
 *
 * Markers take no cell, so the 40 column art budget counts rendered characters only.
 * Marker naming a zone the device lacks renders its character unpainted, which keeps
 * one piece of art valid across mouse variants with fewer zones.
 *
 * Zone color pairs (CLR_ZONE_BASE + zone) are maintained by the caller:
 * the main view keeps them at the configured colors, the illumination view animates them,
 * so the same art reacts to both.
 */
#include "tui_internal.h"

/* does the marker at p start zone paint? */
static int is_zone_marker(const char *p)
{
	return p[0] == '$' && p[1] >= '1' && p[1] <= '8' && p[2] != '\0' &&
	       p[2] != '\n';
}

int tui_art_has_markup(const char *art)
{
	const char *p;

	for (p = art; *p; p++) {
		if (is_zone_marker(p))
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
		if (is_zone_marker(p))
			p++; /* skip the digit; the char counts below */
		else if (p[0] == '$' && p[1] == '$')
			p++; /* literal dollar renders one cell */
		cur++;
	}

	*lines = n;
	*width = ALLOY_MAX(w, cur);
}

void tui_art_draw(const struct tui *t, const char *art, int y, int x, int max_y,
		  int hl_zone)
{
	const char *p;
	int zone;

	move(y, x);
	for (p = art; *p && y < max_y; p++) {
		if (*p == '\n') {
			y++;
			move(y, x);
			continue;
		}

		zone = -1;
		if (is_zone_marker(p)) {
			zone = p[1] - '1';
			p += 2;
		} else if (p[0] == '$' && p[1] == '$') {
			p++;
		}

		if (zone >= 0 && zone < t->drv->num_zones && COLORS >= 8) {
			int attr = COLOR_PAIR(CLR_ZONE_BASE + zone);

			if (zone == hl_zone)
				attr |= A_BOLD;
			addch((chtype)*p | (chtype)attr);
		} else {
			addch((chtype)*p);
		}
	}
}
