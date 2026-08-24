/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Drawing surface handed to driver-side code.
 *
 * Canvas is a clipped rectangle of the terminal:
 * driver code paints with relative coordinates and semantic styles,
 * and never sees curses.
 * This is the whole of the front-end's rendering ABI, so a driver can draw
 * battery gauge or a response curve without the front-end knowing what either
 * of those is.
 */
#include <string.h>

#include "tui_internal.h"

struct alloy_ui_canvas {
	int y;
	int x;
	int h;
	int w;
};

/*
 * Canvases are stack-scoped and short-lived (one item paint, one modal frame),
 * but a driver-owned modal can paint while a pane paints underneath it,
 * so a tiny ring keeps the outer binding alive.
 */
#define TUI_CANVAS_SLOTS 4
static struct alloy_ui_canvas canvas_pool[TUI_CANVAS_SLOTS];
static unsigned canvas_next;

struct alloy_ui_canvas *tui_canvas_bind(int y, int x, int h, int w)
{
	struct alloy_ui_canvas *c =
		&canvas_pool[canvas_next++ % TUI_CANVAS_SLOTS];

	c->y = y;
	c->x = x;
	c->h = ALLOY_MAX(h, 0);
	c->w = ALLOY_MAX(w, 0);
	return c;
}

int tui_style_attr(enum alloy_ui_style style)
{
	switch (style) {
	case ALLOY_UI_ST_DIM:
		return COLOR_PAIR(CLR_DISABLED);
	case ALLOY_UI_ST_FRAME:
		return COLOR_PAIR(CLR_FRAME);
	case ALLOY_UI_ST_TITLE:
		return COLOR_PAIR(CLR_TITLE) | A_BOLD;
	case ALLOY_UI_ST_ACCENT:
		return COLOR_PAIR(CLR_ACCENT) | A_BOLD;
	case ALLOY_UI_ST_SELECTED:
		return COLOR_PAIR(CLR_SELECTED);
	case ALLOY_UI_ST_BUTTON:
		return COLOR_PAIR(CLR_BUTTON);
	case ALLOY_UI_ST_HOT:
		return COLOR_PAIR(CLR_BUTTON_HOT) | A_BOLD;
	case ALLOY_UI_ST_GOOD:
		return COLOR_PAIR(CLR_GOOD) | A_BOLD;
	case ALLOY_UI_ST_WARN:
		return COLOR_PAIR(CLR_WARN) | A_BOLD;
	case ALLOY_UI_ST_BAD:
		return COLOR_PAIR(CLR_BAD) | A_BOLD;
	case ALLOY_UI_ST_INFO:
		return COLOR_PAIR(CLR_INFO);
	case ALLOY_UI_ST_NORMAL:
	default:
		return A_NORMAL;
	}
}

static chtype glyph_ch(enum alloy_ui_glyph g)
{
	switch (g) {
	case ALLOY_UI_G_HLINE:
		return ACS_HLINE;
	case ALLOY_UI_G_VLINE:
		return ACS_VLINE;
	case ALLOY_UI_G_ULCORNER:
		return ACS_ULCORNER;
	case ALLOY_UI_G_URCORNER:
		return ACS_URCORNER;
	case ALLOY_UI_G_LLCORNER:
		return ACS_LLCORNER;
	case ALLOY_UI_G_LRCORNER:
		return ACS_LRCORNER;
	case ALLOY_UI_G_LTEE:
		return ACS_LTEE;
	case ALLOY_UI_G_RTEE:
		return ACS_RTEE;
	case ALLOY_UI_G_DIAMOND:
		return ACS_DIAMOND;
	case ALLOY_UI_G_BULLET:
		return ACS_BULLET;
	case ALLOY_UI_G_CKBOARD:
		return ACS_CKBOARD;
	case ALLOY_UI_G_BLOCK:
		return ACS_BLOCK;
	case ALLOY_UI_G_SHADE:
		return ACS_BOARD;
	case ALLOY_UI_G_LARROW:
		return ACS_LARROW;
	case ALLOY_UI_G_RARROW:
		return ACS_RARROW;
	case ALLOY_UI_G_UARROW:
		return ACS_UARROW;
	case ALLOY_UI_G_DARROW:
		return ACS_DARROW;
	default:
		return ' ';
	}
}

static int canvas_h(struct alloy_ui_canvas *c)
{
	return c ? c->h : 0;
}

static int canvas_w(struct alloy_ui_canvas *c)
{
	return c ? c->w : 0;
}

/* clipped write of one already-formatted string */
static void canvas_text(struct alloy_ui_canvas *c, int y, int x,
			enum alloy_ui_style style, const char *s)
{
	int attr;
	int room;

	if (!c || !s || y < 0 || y >= c->h || x >= c->w)
		return;

	/* left-clip is not worth the complexity: negative x is a caller bug */
	if (x < 0)
		x = 0;
	room = c->w - x;
	if (room <= 0)
		return;

	attr = tui_style_attr(style);
	attron(attr);
	mvprintw(c->y + y, c->x + x, "%.*s", room, s);
	attroff(attr);
}

static void canvas_glyph(struct alloy_ui_canvas *c, int y, int x,
			 enum alloy_ui_glyph g, enum alloy_ui_style style)
{
	int attr;

	if (!c || y < 0 || y >= c->h || x < 0 || x >= c->w)
		return;

	attr = tui_style_attr(style);
	mvaddch(c->y + y, c->x + x, glyph_ch(g) | (chtype)attr);
}

static void canvas_cell(struct alloy_ui_canvas *c, int y, int x, char ch,
			const struct alloy_rgb *color)
{
	if (!c || y < 0 || y >= c->h || x < 0 || x >= c->w)
		return;

	if (COLORS >= 8 && color)
		mvaddch(c->y + y, c->x + x,
			(chtype)ch | (chtype)tui_rgb_attr(color));
	else
		mvaddch(c->y + y, c->x + x, (chtype)ch);
}

static struct alloy_config *host_config(struct alloy_ui *ui)
{
	return ui->cfg;
}

static const struct alloy_driver *host_driver(struct alloy_ui *ui)
{
	return ui->drv;
}

static struct alloy_device *host_device(struct alloy_ui *ui)
{
	return ui->dev;
}

static int host_live_preview(struct alloy_ui *ui)
{
	return ui->live_preview;
}

static void host_status(struct alloy_ui *ui, const char *msg)
{
	tui_status(ui, "%s", msg);
}

static void host_changed(struct alloy_ui *ui, const char *step)
{
	tui_mark_dirty(ui);
	if (ui->live_preview)
		tui_push(ui, step);
}

static int host_push(struct alloy_ui *ui, const char *step)
{
	return tui_push(ui, step);
}

static int host_var(struct alloy_ui *ui, int slot)
{
	if (slot < 0 || slot >= ALLOY_UI_MAX_VARS)
		return 0;
	return ui->vars[slot];
}

static void host_set_var(struct alloy_ui *ui, int slot, int val)
{
	if (slot < 0 || slot >= ALLOY_UI_MAX_VARS)
		return;
	ui->vars[slot] = val;
}

static void host_goto_screen(struct alloy_ui *ui, uint32_t screen_id)
{
	void (*changed)(struct alloy_ui *, uint32_t);
	uint8_t i;

	for (i = 0; i < ui->desc->num_screens; i++) {
		if (ui->desc->screens[i].id != screen_id)
			continue;
		ui->screen = i;
		ui->focus = 0;
		memset(ui->cursor, 0, sizeof(ui->cursor));
		changed = TUI_HOOK(ui, screen_changed);
		if (changed)
			changed(ui, screen_id);
		return;
	}
}

static uint32_t host_screen(struct alloy_ui *ui)
{
	return tui_screen(ui)->id;
}

const struct alloy_ui_host tui_ui_host = {
	.config = host_config,
	.driver = host_driver,
	.device = host_device,
	.live_preview = host_live_preview,
	.now_ms = tui_now_ms,
	.status = host_status,
	.mark_dirty = tui_mark_dirty,
	.changed = host_changed,
	.push = host_push,
	.var = host_var,
	.set_var = host_set_var,
	.goto_screen = host_goto_screen,
	.screen = host_screen,
	.message = tui_modal_message,
	.menu = tui_modal_menu,
	.pick_color = tui_pick_color,
	.prompt_hex = tui_prompt_hex,
	.capture_key = tui_modal_capture_key,
	.run_modal = tui_modal_run,
	.canvas_h = canvas_h,
	.canvas_w = canvas_w,
	.canvas_text = canvas_text,
	.canvas_glyph = canvas_glyph,
	.canvas_cell = canvas_cell,
};
