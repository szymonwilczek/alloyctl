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
#include <stdio.h>
#include <string.h>

#include "tui_internal.h"
#include "default_art.h"

/* Items of the EFFECTS pane, top to bottom */
enum illum_item {
	ILL_EFFECT,
	ILL_FREQ,
	ILL_SPEED,
	ILL_R,
	ILL_G,
	ILL_B,
	ILL_PALETTE,
	ILL_HEX,
	ILL_COUNT,
};

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

static void draw_rate_row(struct tui *t, int y, int x, const char *name,
			  uint8_t val, int selected)
{
	int rate_disabled = !t->cfg.zone_fx[t->illum_zone];
	int i;

	if (selected)
		attron(COLOR_PAIR(CLR_SELECTED));
	else if (rate_disabled)
		attron(COLOR_PAIR(CLR_DISABLED));
	mvprintw(y, x, "%-10s", name);
	if (selected)
		attroff(COLOR_PAIR(CLR_SELECTED));
	else if (rate_disabled)
		attroff(COLOR_PAIR(CLR_DISABLED));

	if (rate_disabled) {
		attron(COLOR_PAIR(CLR_DISABLED));
		mvprintw(y, x + 11, "steady");
		attroff(COLOR_PAIR(CLR_DISABLED));
		return;
	}

	mvprintw(y, x + 11, "< %2u >", val);
	move(y, x + 18);
	for (i = ALLOY_FX_RATE_MIN; i <= ALLOY_FX_RATE_MAX; i++)
		addch(i <= val ? (chtype)(ACS_CKBOARD | A_BOLD) :
				 (chtype)ACS_BULLET);
}

static void draw_color_channel(int y, int x, int w, const char *name,
			       uint8_t val, int selected, int disabled)
{
	int span = ALLOY_CLAMP(w - 16, 4, 20);
	int bar = (int)val * span / 255;
	int i;

	if (selected)
		attron(COLOR_PAIR(CLR_SELECTED));
	else if (disabled)
		attron(COLOR_PAIR(CLR_DISABLED));
	mvprintw(y, x, "%s < %3u >", name, val);
	if (selected)
		attroff(COLOR_PAIR(CLR_SELECTED));
	else if (disabled)
		attroff(COLOR_PAIR(CLR_DISABLED));

	move(y, x + 12);
	for (i = 0; i < span; i++)
		addch(i < bar ? (chtype)(ACS_CKBOARD | A_BOLD) :
				(chtype)ACS_BULLET);
}

/*
 * COLORS section is the old zone picker modal laid flat into the pane:
 * R/G/B steppers, shared palette and the hex field.
 * R/G/B grey out while the zone runs a color-cycling effect but
 * the palette and hex stay editable so color can be prepared in advance.
 */
static void draw_colors_section(struct tui *t, int y, int x, int w, int focused)
{
	struct alloy_rgb *rgb = &t->cfg.zone_color[t->illum_zone];
	int sel = t->illum_cursor;
	int greyed =
		tui_fx_ignores_color(t->drv, t->cfg.zone_fx[t->illum_zone]);
	char hex[8];
	size_t i;

	if (COLORS >= 256) {
		init_pair(CLR_PICKER_PREVIEW, COLOR_BLACK,
			  tui_rgb_to_cube(rgb));
		for (i = 0; i < TUI_PALETTE_SIZE; i++)
			init_pair((short)(CLR_PICKER_SWATCH + i),
				  tui_rgb_to_cube(&tui_palette[i]), -1);
	}

	attron(COLOR_PAIR(CLR_TITLE) | A_BOLD);
	mvprintw(y, x + 2, "COLORS");
	attroff(COLOR_PAIR(CLR_TITLE) | A_BOLD);

	draw_color_channel(y + 2, x + 2, w - 4, "R", rgb->r,
			   focused && sel == ILL_R, greyed);
	draw_color_channel(y + 3, x + 2, w - 4, "G", rgb->g,
			   focused && sel == ILL_G, greyed);
	draw_color_channel(y + 4, x + 2, w - 4, "B", rgb->b,
			   focused && sel == ILL_B, greyed);

	if (focused && sel == ILL_PALETTE)
		attron(COLOR_PAIR(CLR_SELECTED));
	mvprintw(y + 6, x + 2, "PALETTE");
	if (focused && sel == ILL_PALETTE)
		attroff(COLOR_PAIR(CLR_SELECTED));
	for (i = 0; i < TUI_PALETTE_SIZE; i++) {
		int sx = x + 12 + (int)i * 2 - (int)(i / 8) * 16;
		int sy = y + 6 + (int)(i / 8);

		if (focused && sel == ILL_PALETTE && (int)i == t->illum_swatch)
			mvaddch(sy, sx - 1, '[' | A_BOLD);
		if (COLORS >= 256)
			attron(COLOR_PAIR(CLR_PICKER_SWATCH + i) | A_BOLD);
		mvaddch(sy, sx, ACS_DIAMOND);
		if (COLORS >= 256)
			attroff(COLOR_PAIR(CLR_PICKER_SWATCH + i) | A_BOLD);
		if (focused && sel == ILL_PALETTE && (int)i == t->illum_swatch)
			mvaddch(sy, sx + 1, ']' | A_BOLD);
	}

	/* hex field: typed buffer while editing, live value otherwise */
	if (focused && sel == ILL_HEX)
		attron(COLOR_PAIR(CLR_SELECTED));
	mvprintw(y + 9, x + 2, "HEX");
	if (focused && sel == ILL_HEX)
		attroff(COLOR_PAIR(CLR_SELECTED));
	if (t->illum_hexbuf) {
		attron(A_BOLD);
		mvprintw(y + 9, x + 12, "#%-6s_", t->illum_hexbuf);
		attroff(A_BOLD);
	} else {
		snprintf(hex, sizeof(hex), "%02X%02X%02X", rgb->r, rgb->g,
			 rgb->b);
		mvprintw(y + 9, x + 12, "#%s", hex);
	}
	if (COLORS >= 256) {
		attron(COLOR_PAIR(CLR_PICKER_PREVIEW));
		mvprintw(y + 9, x + w - 9, "      ");
		attroff(COLOR_PAIR(CLR_PICKER_PREVIEW));
	}
}

static void draw_effects_pane(struct tui *t, int y, int x, int h, int w)
{
	int focused = t->illum_focus == ILLUM_FOCUS_EFFECTS;
	int sel = t->illum_cursor;
	const char *fx_name = "STEADY";

	tui_draw_pane_box(y, x, h, w, "EFFECTS", focused);

	attron(COLOR_PAIR(CLR_ACCENT) | A_BOLD);
	mvprintw(y + 2, x + 2, "ZONE Z%d: %s", t->illum_zone + 1,
		 t->drv->zones[t->illum_zone].name);
	attroff(COLOR_PAIR(CLR_ACCENT) | A_BOLD);

	if (t->drv->num_fx)
		fx_name = t->drv->fx_names[t->cfg.zone_fx[t->illum_zone] %
					   t->drv->num_fx];

	if (focused && sel == ILL_EFFECT)
		attron(COLOR_PAIR(CLR_SELECTED));
	mvprintw(y + 4, x + 2, "%-10s", "EFFECT");
	if (focused && sel == ILL_EFFECT)
		attroff(COLOR_PAIR(CLR_SELECTED));
	attron(COLOR_PAIR(CLR_ACCENT) | A_BOLD);
	mvprintw(y + 4, x + 13, "< %s >", fx_name);
	attroff(COLOR_PAIR(CLR_ACCENT) | A_BOLD);

	draw_rate_row(t, y + 6, x + 2, "FREQUENCY",
		      t->cfg.zone_fx_freq[t->illum_zone],
		      focused && sel == ILL_FREQ);
	draw_rate_row(t, y + 7, x + 2, "SPEED",
		      t->cfg.zone_fx_speed[t->illum_zone],
		      focused && sel == ILL_SPEED);

	draw_colors_section(t, y + 9, x, w, focused);

	attron(COLOR_PAIR(CLR_DISABLED));
	mvprintw(y + h - 2, x + 2, "j/k: item  h/l: adjust");
	attroff(COLOR_PAIR(CLR_DISABLED));
}

/* Modal listing the driver's effects for the edited zone */
static void illum_effect_modal(struct tui *t)
{
	const int count = t->drv->num_fx;
	int sel;
	int y;
	int x;
	int i;
	int ch;

	if (count < 2) {
		tui_modal_message("EFFECT", "only STEADY on this device");
		return;
	}
	sel = t->cfg.zone_fx[t->illum_zone] % count;

	for (;;) {
		tui_illum_draw(t);
		tui_modal_frame(count + 4, 30, &y, &x,
				t->drv->zones[t->illum_zone].name);

		for (i = 0; i < count; i++) {
			if (i == sel)
				attron(COLOR_PAIR(CLR_SELECTED));
			mvprintw(y + 2 + i, x + 3, "%-24s",
				 t->drv->fx_names[i]);
			if (i == sel)
				attroff(COLOR_PAIR(CLR_SELECTED));
		}
		attron(COLOR_PAIR(CLR_DISABLED));
		mvprintw(y + count + 3, x + 2, " enter: pick  esc: cancel ");
		attroff(COLOR_PAIR(CLR_DISABLED));
		refresh();

		ch = getch();
		switch (ch) {
		case KEY_UP:
		case 'k':
			sel = (sel + count - 1) % count;
			break;
		case KEY_DOWN:
		case 'j':
			sel = (sel + 1) % count;
			break;
		case 27:
			return;
		case '\n':
		case KEY_ENTER:
			t->cfg.zone_fx[t->illum_zone] = (uint8_t)sel;
			tui_lighting_changed(t);
			return;
		default:
			break;
		}
	}
}

static void illum_adjust(struct tui *t, int dir, int big)
{
	int zone = t->illum_zone;
	uint8_t *chan = NULL;
	uint8_t *rate;
	int val;

	switch (t->illum_cursor) {
	case ILL_EFFECT:
		if (t->drv->num_fx < 2)
			return;
		t->cfg.zone_fx[zone] = (uint8_t)((t->cfg.zone_fx[zone] +
						  t->drv->num_fx + dir) %
						 t->drv->num_fx);
		tui_lighting_changed(t);
		return;
	case ILL_FREQ:
	case ILL_SPEED:
		if (!t->cfg.zone_fx[zone])
			return; /* steady has no rate */
		rate = t->illum_cursor == ILL_FREQ ?
			       &t->cfg.zone_fx_freq[zone] :
			       &t->cfg.zone_fx_speed[zone];
		val = *rate + dir;
		*rate = (uint8_t)ALLOY_CLAMP(val, ALLOY_FX_RATE_MIN,
					     ALLOY_FX_RATE_MAX);
		tui_lighting_changed(t);
		return;
	case ILL_R:
		chan = &t->cfg.zone_color[zone].r;
		break;
	case ILL_G:
		chan = &t->cfg.zone_color[zone].g;
		break;
	case ILL_B:
		chan = &t->cfg.zone_color[zone].b;
		break;
	case ILL_PALETTE:
		t->illum_swatch = (t->illum_swatch + TUI_PALETTE_SIZE + dir) %
				  TUI_PALETTE_SIZE;
		return;
	default:
		return;
	}

	if (tui_fx_ignores_color(t->drv, t->cfg.zone_fx[zone]))
		return;
	val = *chan + dir * (big ? 16 : 1);
	*chan = (uint8_t)ALLOY_CLAMP(val, 0, 255);
	tui_lighting_changed(t);
}

/*
 * Hex entry loop:
 * type up to six digits, enter commits (three-digit shorthand expands CSS-style),
 * esc abandons.
 * Buffer is exposed through illum_hexbuf so the redraw shows the digits as they are typed.
 */
static void illum_hex_input(struct tui *t)
{
	char buf[7] = "";
	size_t len = 0;
	int ch;

	t->illum_hexbuf = buf;
	for (;;) {
		tui_illum_draw(t);
		ch = getch();
		if (ch == 27) {
			t->illum_hexbuf = NULL;
			return;
		}
		if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
			if (len)
				buf[--len] = '\0';
			continue;
		}
		if (ch == '\n' || ch == KEY_ENTER)
			break;
		if (len < 6 && tui_hex_digit(ch) >= 0) {
			buf[len++] = (char)ch;
			buf[len] = '\0';
		}
	}
	t->illum_hexbuf = NULL;

	if (tui_parse_hex_color(buf, len, &t->cfg.zone_color[t->illum_zone])) {
		tui_status(t, "invalid hex color");
		return;
	}
	tui_lighting_changed(t);
}

static void illum_activate(struct tui *t)
{
	switch (t->illum_cursor) {
	case ILL_EFFECT:
		illum_effect_modal(t);
		return;
	case ILL_PALETTE:
		t->cfg.zone_color[t->illum_zone] = tui_palette[t->illum_swatch];
		tui_lighting_changed(t);
		return;
	case ILL_HEX:
		illum_hex_input(t);
		return;
	default:
		return;
	}
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
		} else {
			illum_activate(t);
		}
		return;
	default:
		break;
	}

	if (t->illum_focus != ILLUM_FOCUS_EFFECTS)
		return;

	switch (ch) {
	case KEY_UP:
	case 'k':
		t->illum_cursor = (t->illum_cursor + ILL_COUNT - 1) % ILL_COUNT;
		break;
	case KEY_DOWN:
	case 'j':
		t->illum_cursor = (t->illum_cursor + 1) % ILL_COUNT;
		break;
	case KEY_LEFT:
	case 'h':
		illum_adjust(t, -1, 0);
		break;
	case KEY_RIGHT:
	case 'l':
		illum_adjust(t, 1, 0);
		break;
	case 'H':
		illum_adjust(t, -1, 1);
		break;
	case 'L':
		illum_adjust(t, 1, 1);
		break;
	case 'x':
		t->illum_cursor = ILL_HEX;
		illum_hex_input(t);
		break;
	default:
		break;
	}
}
