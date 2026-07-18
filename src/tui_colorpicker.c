// SPDX-License-Identifier: GPL-2.0-only
/*
 * Color picker modal.
 *
 * One picker serves both the LED zones and the reactive click color:
 * mode row (static/rainbow for zones, on/off for reactive),
 * R/G/B steppers with value bars, a preset palette, and hex field
 * that both displays the current color and accepts typed RRGGBB input.
 *
 * Every change is pushed to the mouse immediately while live preview is on,
 * so the picked color shows up on the hardware as it is dialed in.
 */
#include <stdio.h>
#include <string.h>

#include "tui_internal.h"

#define PICKER_W 44

/* Rows inside the picker, top to bottom */
enum picker_row {
	ROW_MODE,
	ROW_R,
	ROW_G,
	ROW_B,
	ROW_PALETTE,
	ROW_HEX,
	ROW_COUNT,
};

struct picker {
	const char *title;
	struct alloy_rgb *rgb;

	/* Mode row;
	 * labels == NULL hides the row entirely */
	const char *const *mode_labels;
	int num_modes;
	uint8_t *mode;

	/*
	 * Returns nonzero for mode values that grey out the R/G/B rows
	 * (color-cycling zone effect, reactive off);
	 * NULL for never.
	 * Rows stay editable via the palette/hex so color can be prepared in advance
	 */
	int (*mode_greys)(const struct tui *t, uint8_t mode);
	int color_rows_disabled;
};

static const struct alloy_rgb palette[] = {
	{ 0xFF, 0xFF, 0xFF }, { 0xFF, 0x00, 0x00 }, { 0xFF, 0x66, 0x00 },
	{ 0xFF, 0xCC, 0x00 }, { 0x00, 0xFF, 0x00 }, { 0x00, 0xFF, 0x99 },
	{ 0x00, 0xFF, 0xFF }, { 0x00, 0x99, 0xFF }, { 0x00, 0x00, 0xFF },
	{ 0x66, 0x00, 0xFF }, { 0xCC, 0x00, 0xFF }, { 0xFF, 0x00, 0x99 },
	{ 0xFF, 0x99, 0x99 }, { 0x99, 0x66, 0x33 }, { 0x66, 0x66, 0x66 },
	{ 0x00, 0x00, 0x00 },
};

static short rgb_to_cube(const struct alloy_rgb *c)
{
	return (short)(16 + 36 * (c->r / 51) + 6 * (c->g / 51) + (c->b / 51));
}

static void picker_pairs(const struct picker *p)
{
	size_t i;

	if (COLORS < 256)
		return;

	init_pair(CLR_PICKER_PREVIEW, COLOR_BLACK, rgb_to_cube(p->rgb));
	for (i = 0; i < ALLOY_ARRAY_SIZE(palette); i++)
		init_pair((short)(CLR_PICKER_SWATCH + i),
			  rgb_to_cube(&palette[i]), -1);
}

static void draw_channel(int y, int x, const char *name, uint8_t val,
			 int selected, int disabled)
{
	int bar = (int)val * 20 / 255;
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
	for (i = 0; i < 20; i++)
		addch(i < bar ? (chtype)(ACS_CKBOARD | A_BOLD) :
				(chtype)ACS_BULLET);
}

static void picker_draw(struct tui *t, const struct picker *p, int row,
			int swatch, const char *hexbuf)
{
	char hex[8];
	int y;
	int x;
	size_t i;

	tui_draw(t);
	picker_pairs(p);
	tui_modal_frame(ROW_COUNT + 8, PICKER_W, &y, &x, p->title);

	/* mode row */
	if (p->mode_labels) {
		if (row == ROW_MODE)
			attron(COLOR_PAIR(CLR_SELECTED));
		mvprintw(y + 2, x + 3, "Mode");
		if (row == ROW_MODE)
			attroff(COLOR_PAIR(CLR_SELECTED));
		mvprintw(y + 2, x + 12, "< %-14s >", p->mode_labels[*p->mode]);
	}

	draw_channel(y + 4, x + 3, "R", p->rgb->r, row == ROW_R,
		     p->color_rows_disabled);
	draw_channel(y + 5, x + 3, "G", p->rgb->g, row == ROW_G,
		     p->color_rows_disabled);
	draw_channel(y + 6, x + 3, "B", p->rgb->b, row == ROW_B,
		     p->color_rows_disabled);

	/* palette */
	if (row == ROW_PALETTE)
		attron(COLOR_PAIR(CLR_SELECTED));
	mvprintw(y + 8, x + 3, "Palette");
	if (row == ROW_PALETTE)
		attroff(COLOR_PAIR(CLR_SELECTED));
	for (i = 0; i < ALLOY_ARRAY_SIZE(palette); i++) {
		int sx = x + 12 + (int)i * 2 - (int)(i / 8) * 16;
		int sy = y + 8 + (int)(i / 8);

		if (row == ROW_PALETTE && (int)i == swatch)
			mvaddch(sy, sx - 1, '[' | A_BOLD);
		if (COLORS >= 256)
			attron(COLOR_PAIR(CLR_PICKER_SWATCH + i) | A_BOLD);
		mvaddch(sy, sx, ACS_DIAMOND);
		if (COLORS >= 256)
			attroff(COLOR_PAIR(CLR_PICKER_SWATCH + i) | A_BOLD);
		if (row == ROW_PALETTE && (int)i == swatch)
			mvaddch(sy, sx + 1, ']' | A_BOLD);
	}

	/* hex field: typed buffer while editing, live value otherwise */
	if (row == ROW_HEX)
		attron(COLOR_PAIR(CLR_SELECTED));
	mvprintw(y + 11, x + 3, "HEX");
	if (row == ROW_HEX)
		attroff(COLOR_PAIR(CLR_SELECTED));
	if (hexbuf) {
		attron(A_BOLD);
		mvprintw(y + 11, x + 12, "#%-6s_", hexbuf);
		attroff(A_BOLD);
	} else {
		snprintf(hex, sizeof(hex), "%02X%02X%02X", p->rgb->r, p->rgb->g,
			 p->rgb->b);
		mvprintw(y + 11, x + 12, "#%s", hex);
	}

	/* preview block */
	if (COLORS >= 256) {
		attron(COLOR_PAIR(CLR_PICKER_PREVIEW));
		mvprintw(y + 11, x + PICKER_W - 10, "      ");
		attroff(COLOR_PAIR(CLR_PICKER_PREVIEW));
	}

	attron(COLOR_PAIR(CLR_DISABLED));
	mvprintw(y + ROW_COUNT + 7, x + 2,
		 " h/l: adjust  H/L: fast  esc: close ");
	attroff(COLOR_PAIR(CLR_DISABLED));
	refresh();
}

static void picker_changed(struct tui *t)
{
	t->dirty = memcmp(&t->cfg, &t->baseline, sizeof(t->cfg)) != 0;
	if (t->live_preview)
		tui_apply(t, t->drv->ops->apply_colors, "lighting");
}

static int hex_digit(int ch)
{
	if (ch >= '0' && ch <= '9')
		return ch - '0';
	if (ch >= 'a' && ch <= 'f')
		return ch - 'a' + 10;
	if (ch >= 'A' && ch <= 'F')
		return ch - 'A' + 10;
	return -1;
}

/*
 * Hex entry loop:
 * type up to six digits, enter commits (three-digit shorthand expands CSS-style),
 * esc abandons.
 */
static void picker_hex_input(struct tui *t, const struct picker *p)
{
	char buf[7] = "";
	size_t len = 0;
	unsigned rgb;
	int ch;

	for (;;) {
		picker_draw(t, p, ROW_HEX, 0, buf);
		ch = getch();
		if (ch == 27)
			return;
		if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
			if (len)
				buf[--len] = '\0';
			continue;
		}
		if (ch == '\n' || ch == KEY_ENTER)
			break;
		if (len < 6 && hex_digit(ch) >= 0) {
			buf[len++] = (char)ch;
			buf[len] = '\0';
		}
	}

	if (len == 3) {
		/* expand shorthand: F80 -> FF8800 */
		char full[7];

		full[0] = full[1] = buf[0];
		full[2] = full[3] = buf[1];
		full[4] = full[5] = buf[2];
		full[6] = '\0';
		memcpy(buf, full, sizeof(full));
		len = 6;
	}
	if (len != 6 || sscanf(buf, "%6x", &rgb) != 1) {
		tui_status(t, "invalid hex color");
		return;
	}

	p->rgb->r = (rgb >> 16) & 0xFF;
	p->rgb->g = (rgb >> 8) & 0xFF;
	p->rgb->b = rgb & 0xFF;
	picker_changed(t);
}

static void picker_adjust_channel(struct tui *t, const struct picker *p,
				  int row, int delta)
{
	uint8_t *chan;
	int val;

	switch (row) {
	case ROW_R:
		chan = &p->rgb->r;
		break;
	case ROW_G:
		chan = &p->rgb->g;
		break;
	case ROW_B:
		chan = &p->rgb->b;
		break;
	default:
		return;
	}

	val = *chan + delta;
	*chan = (uint8_t)ALLOY_CLAMP(val, 0, 255);
	picker_changed(t);
}

static void picker_run(struct tui *t, struct picker *p)
{
	int row = p->mode_labels ? ROW_MODE : ROW_R;
	int swatch = 0;
	int ch;

	for (;;) {
		picker_draw(t, p, row, swatch, NULL);
		ch = getch();

		switch (ch) {
		case 27:
		case 'q':
			return;
		case KEY_UP:
		case 'k':
			do {
				row = (row + ROW_COUNT - 1) % ROW_COUNT;
			} while (row == ROW_MODE && !p->mode_labels);
			break;
		case KEY_DOWN:
		case 'j':
			do {
				row = (row + 1) % ROW_COUNT;
			} while (row == ROW_MODE && !p->mode_labels);
			break;
		case KEY_LEFT:
		case 'h':
		case KEY_RIGHT:
		case 'l':
		case 'H':
		case 'L': {
			int dir = (ch == KEY_LEFT || ch == 'h' || ch == 'H') ?
					  -1 :
					  1;
			int big = (ch == 'H' || ch == 'L');

			if (row == ROW_MODE && p->mode_labels) {
				*p->mode = (uint8_t)((*p->mode + p->num_modes +
						      dir) %
						     p->num_modes);
				p->color_rows_disabled =
					p->mode_greys &&
					p->mode_greys(t, *p->mode);
				picker_changed(t);
			} else if (row >= ROW_R && row <= ROW_B &&
				   !p->color_rows_disabled) {
				picker_adjust_channel(t, p, row,
						      dir * (big ? 16 : 1));
			} else if (row == ROW_PALETTE) {
				int count = (int)ALLOY_ARRAY_SIZE(palette);

				swatch = (swatch + count + dir) % count;
			}
			break;
		}
		case '\n':
		case KEY_ENTER:
			if (row == ROW_PALETTE) {
				*p->rgb = palette[swatch];
				picker_changed(t);
			} else if (row == ROW_HEX) {
				picker_hex_input(t, p);
			}
			break;
		case 'x':
			row = ROW_HEX;
			picker_hex_input(t, p);
			break;
		default:
			break;
		}
	}
}

static int zone_mode_greys(const struct tui *t, uint8_t mode)
{
	return tui_fx_ignores_color(t->drv, mode);
}

void tui_modal_color_zone(struct tui *t, int zone)
{
	char title[48];
	struct picker p;

	memset(&p, 0, sizeof(p));
	snprintf(title, sizeof(title), "ZONE: %s", t->drv->zones[zone].name);
	p.title = title;
	p.rgb = &t->cfg.zone_color[zone];

	if (t->drv->num_fx > 1) {
		p.mode_labels = t->drv->fx_names;
		p.num_modes = t->drv->num_fx;
		p.mode = &t->cfg.zone_fx[zone];
		p.mode_greys = zone_mode_greys;
		p.color_rows_disabled = zone_mode_greys(t, *p.mode);
	}
	picker_run(t, &p);
}

static int reactive_mode_greys(const struct tui *t, uint8_t mode)
{
	(void)t;
	return !mode; /* color rows greyed while OFF */
}

void tui_modal_color_reactive(struct tui *t)
{
	static const char *const reactive_modes[] = { "OFF", "ON" };
	struct picker p;

	memset(&p, 0, sizeof(p));
	p.title = "REACTIVE CLICK COLOR";
	p.rgb = &t->cfg.reactive_color;
	p.mode_labels = reactive_modes;
	p.num_modes = 2;
	p.mode = &t->cfg.reactive_enabled;
	p.mode_greys = reactive_mode_greys;
	p.color_rows_disabled = !t->cfg.reactive_enabled;
	picker_run(t, &p);
}
