// SPDX-License-Identifier: GPL-2.0-only
/*
 * Color services.
 *
 * Two of them are offered to driver-side code:
 * full picker modal (mode row, R/G/B steppers with value bars, preset palette
 * and a hex field) and the bare hex prompt the inline color widget uses.
 *
 * Both edit the caller's struct alloy_rgb in place and call back on every
 * keystroke, so whoever asked for the color decides what a change means
 * and how it reaches the hardware.
 *
 * Also here: the mapping from true RGB onto whatever palette the terminal has,
 * which every other painter goes through.
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

/* Shared with the inline color widget of the generic renderer */
const struct alloy_rgb tui_palette[TUI_PALETTE_SIZE] = {
	{ 0xFF, 0xFF, 0xFF }, { 0xFF, 0x00, 0x00 }, { 0xFF, 0x66, 0x00 },
	{ 0xFF, 0xCC, 0x00 }, { 0x00, 0xFF, 0x00 }, { 0x00, 0xFF, 0x99 },
	{ 0x00, 0xFF, 0xFF }, { 0x00, 0x99, 0xFF }, { 0x00, 0x00, 0xFF },
	{ 0x66, 0x00, 0xFF }, { 0xCC, 0x00, 0xFF }, { 0xFF, 0x00, 0x99 },
	{ 0xFF, 0x99, 0x99 }, { 0x99, 0x66, 0x33 }, { 0x66, 0x66, 0x66 },
	{ 0x00, 0x00, 0x00 },
};

/*
 * Map RGB color onto whatever palette the terminal offers:
 * 6x6x6 cube on 256-color terminals,
 * 16 ANSI colors with the bright bit where available,
 * or the base 8 as a last resort.
 */
short tui_rgb_to_color(const struct alloy_rgb *c)
{
	short idx;

	if (COLORS >= 256)
		return (short)(16 + 36 * (c->r / 51) + 6 * (c->g / 51) +
			       (c->b / 51));

	idx = (short)((c->r > 0x60 ? COLOR_RED : 0) |
		      (c->g > 0x60 ? COLOR_GREEN : 0) |
		      (c->b > 0x60 ? COLOR_BLUE : 0));
	if (COLORS >= 16 && (c->r > 0xC0 || c->g > 0xC0 || c->b > 0xC0))
		idx += 8;
	return idx;
}

int tui_hex_digit(int ch)
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
 * Parse an RRGGBB buffer into rgb;
 * three-digit shorthand expands CSS-style (F80 -> FF8800).
 * Returns -1 when the buffer is not a valid color.
 */
int tui_parse_hex_color(char *buf, size_t len, struct alloy_rgb *rgb)
{
	unsigned val;

	if (len == 3) {
		char full[7];

		full[0] = full[1] = buf[0];
		full[2] = full[3] = buf[1];
		full[4] = full[5] = buf[2];
		full[6] = '\0';
		memcpy(buf, full, sizeof(full));
		len = 6;
	}
	if (len != 6 || sscanf(buf, "%6x", &val) != 1)
		return -1;

	rgb->r = (val >> 16) & 0xFF;
	rgb->g = (val >> 8) & 0xFF;
	rgb->b = val & 0xFF;
	return 0;
}

/*
 * Hex entry against the live screen: the typed digits are exposed through
 * ui->hexbuf so whatever widget owns the color shows them as they arrive.
 * Enter commits (three-digit shorthand expands), esc abandons.
 * Returns 0 when @rgb was updated.
 */
int tui_prompt_hex(struct alloy_ui *ui, struct alloy_rgb *rgb)
{
	char buf[7] = "";
	size_t len = 0;
	int ch;

	ui->hexbuf = buf;
	for (;;) {
		tui_draw(ui);
		ch = getch();
		if (ch == 27) {
			ui->hexbuf = NULL;
			return -1;
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
	ui->hexbuf = NULL;

	if (tui_parse_hex_color(buf, len, rgb)) {
		tui_status(ui, "invalid hex color");
		return -1;
	}
	return 0;
}

struct picker {
	const struct alloy_ui_color_req *req;
	int greyed;
};

static void picker_pairs(const struct alloy_rgb *rgb)
{
	size_t i;

	if (COLORS < 8)
		return;

	init_pair(CLR_PICKER_PREVIEW, COLOR_BLACK, tui_rgb_to_color(rgb));
	for (i = 0; i < TUI_PALETTE_SIZE; i++)
		init_pair((short)(CLR_PICKER_SWATCH + i),
			  tui_rgb_to_color(&tui_palette[i]), -1);
}

static void draw_channel(int y, int x, const char *name, uint8_t val,
			 int selected, int disabled)
{
	int bar = (int)val * 20 / 255;
	int attr = selected ? COLOR_PAIR(CLR_SELECTED) :
			      (disabled ? COLOR_PAIR(CLR_DISABLED) : A_NORMAL);
	int i;

	attron(attr);
	mvprintw(y, x, "%s < %3u >", name, val);
	attroff(attr);

	move(y, x + 12);
	for (i = 0; i < 20; i++)
		addch(i < bar ? (chtype)(ACS_CKBOARD | A_BOLD) :
				(chtype)ACS_BULLET);
}

static void picker_draw(struct alloy_ui *ui, const struct picker *p, int row,
			int swatch, const char *hexbuf)
{
	const struct alloy_ui_color_req *req = p->req;
	struct alloy_rgb *rgb = req->rgb;
	char hex[8];
	int y;
	int x;
	size_t i;

	/*
	 * Modal floats over whichever screen invoked it:
	 * render the background without flushing so the single refresh below
	 * composites picker over screen in one frame
	 */
	tui_render(ui);
	picker_pairs(rgb);
	tui_modal_frame(ROW_COUNT + 8, PICKER_W, &y, &x, req->title);

	if (req->modes && req->mode) {
		if (row == ROW_MODE)
			attron(COLOR_PAIR(CLR_SELECTED));
		mvprintw(y + 2, x + 3, "Mode");
		if (row == ROW_MODE)
			attroff(COLOR_PAIR(CLR_SELECTED));
		mvprintw(y + 2, x + 12, "< %-14s >", req->modes[*req->mode]);
	}

	draw_channel(y + 4, x + 3, "R", rgb->r, row == ROW_R, p->greyed);
	draw_channel(y + 5, x + 3, "G", rgb->g, row == ROW_G, p->greyed);
	draw_channel(y + 6, x + 3, "B", rgb->b, row == ROW_B, p->greyed);

	if (row == ROW_PALETTE)
		attron(COLOR_PAIR(CLR_SELECTED));
	mvprintw(y + 8, x + 3, "Palette");
	if (row == ROW_PALETTE)
		attroff(COLOR_PAIR(CLR_SELECTED));
	for (i = 0; i < TUI_PALETTE_SIZE; i++) {
		int sx = x + 12 + (int)i * 2 - (int)(i / 8) * 16;
		int sy = y + 8 + (int)(i / 8);

		if (row == ROW_PALETTE && (int)i == swatch)
			mvaddch(sy, sx - 1, '[' | A_BOLD);
		if (COLORS >= 8)
			attron(COLOR_PAIR(CLR_PICKER_SWATCH + i) | A_BOLD);
		mvaddch(sy, sx, ACS_DIAMOND);
		if (COLORS >= 8)
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
		snprintf(hex, sizeof(hex), "%02X%02X%02X", rgb->r, rgb->g,
			 rgb->b);
		mvprintw(y + 11, x + 12, "#%s", hex);
	}

	if (COLORS >= 8) {
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

static void picker_changed(struct alloy_ui *ui, const struct picker *p)
{
	if (p->req->changed)
		p->req->changed(ui, p->req->data);
}

static void picker_hex_input(struct alloy_ui *ui, const struct picker *p)
{
	char buf[7] = "";
	size_t len = 0;
	int ch;

	for (;;) {
		picker_draw(ui, p, ROW_HEX, 0, buf);
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
		if (len < 6 && tui_hex_digit(ch) >= 0) {
			buf[len++] = (char)ch;
			buf[len] = '\0';
		}
	}

	if (tui_parse_hex_color(buf, len, p->req->rgb)) {
		tui_status(ui, "invalid hex color");
		return;
	}
	picker_changed(ui, p);
}

static void picker_adjust_channel(struct alloy_ui *ui, const struct picker *p,
				  int row, int delta)
{
	struct alloy_rgb *rgb = p->req->rgb;
	uint8_t *chan;
	int val;

	switch (row) {
	case ROW_R:
		chan = &rgb->r;
		break;
	case ROW_G:
		chan = &rgb->g;
		break;
	case ROW_B:
		chan = &rgb->b;
		break;
	default:
		return;
	}

	val = *chan + delta;
	*chan = (uint8_t)ALLOY_CLAMP(val, 0, 255);
	picker_changed(ui, p);
}

int tui_pick_color(struct alloy_ui *ui, const struct alloy_ui_color_req *req)
{
	struct picker p;
	int has_mode;
	int row;
	int swatch = 0;
	int ch;

	if (!req || !req->rgb)
		return -1;

	memset(&p, 0, sizeof(p));
	p.req = req;
	has_mode = req->modes && req->mode;
	p.greyed = has_mode && req->mode_greys &&
		   req->mode_greys(ui, *req->mode);
	row = has_mode ? ROW_MODE : ROW_R;

	for (;;) {
		picker_draw(ui, &p, row, swatch, NULL);
		ch = getch();

		switch (ch) {
		case 27:
		case 'q':
			return 0;
		case KEY_UP:
		case 'k':
			do {
				row = (row + ROW_COUNT - 1) % ROW_COUNT;
			} while (row == ROW_MODE && !has_mode);
			break;
		case KEY_DOWN:
		case 'j':
			do {
				row = (row + 1) % ROW_COUNT;
			} while (row == ROW_MODE && !has_mode);
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

			if (row == ROW_MODE && has_mode) {
				*req->mode = (uint8_t)((*req->mode +
							req->num_modes + dir) %
						       req->num_modes);
				p.greyed = req->mode_greys &&
					   req->mode_greys(ui, *req->mode);
				picker_changed(ui, &p);
			} else if (row >= ROW_R && row <= ROW_B && !p.greyed) {
				picker_adjust_channel(ui, &p, row,
						      dir * (big ? 16 : 1));
			} else if (row == ROW_PALETTE) {
				swatch = (swatch + TUI_PALETTE_SIZE + dir) %
					 TUI_PALETTE_SIZE;
			}
			break;
		}
		case '\n':
		case KEY_ENTER:
			if (row == ROW_PALETTE) {
				*req->rgb = tui_palette[swatch];
				picker_changed(ui, &p);
			} else if (row == ROW_HEX) {
				picker_hex_input(ui, &p);
			}
			break;
		case 'x':
			row = ROW_HEX;
			picker_hex_input(ui, &p);
			break;
		default:
			break;
		}
	}
}
