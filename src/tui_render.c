/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Generic renderer.
 *
 * Everything drawn here comes from the driver's struct alloy_ui_desc:
 * the screen supplies panes, each pane supplies a list of struct alloy_ui_item,
 * and this file turns those into boxes, labels, steppers, sliders, palettes
 * and cursors.
 * It contains no device vocabulary at all - only widgets.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "tui_internal.h"

#define MIN_COLS 100
#define MIN_LINES 28

/*
 * Label column inside a pane.
 * Narrow panes shrink it so the value still has room;
 * there is a floor, below which the label is simply clipped.
 */
#define LABEL_W 13
#define LABEL_MIN 6

static int label_w(int w)
{
	return ALLOY_CLAMP(w - 12, LABEL_MIN, LABEL_W);
}

static int value_x(int w)
{
	return label_w(w) + 2;
}

/*
 * Color block is one item spanning several rows, with five stops the cursor
 * can land on: the three channels, the palette and the hex field.
 */
#define COLOR_ROWS 8
#define COLOR_SUBS 5

const struct alloy_ui_screen *tui_screen(const struct alloy_ui *ui)
{
	int idx = ALLOY_CLAMP(ui->screen, 0, ui->desc->num_screens - 1);

	return &ui->desc->screens[idx];
}

/*
 * Ask the description chain for a pane's controls.
 * Description that does not recognize the pane returns 0, which lets the one
 * it inherits from answer instead - that is how a driver reuses a shared
 * front-end and still adds panes of its own.
 */
size_t tui_pane_items(struct alloy_ui *ui, const struct alloy_ui_pane *pane,
		      struct alloy_ui_item *out, size_t max)
{
	const struct alloy_ui_desc *d;

	memset(out, 0, sizeof(*out) * max);
	for (d = ui->desc; d; d = d->parent) {
		size_t n;

		if (!d->items)
			continue;
		n = d->items(ui, pane, out, max);
		if (n)
			return ALLOY_MIN(n, max);
	}
	return 0;
}

int tui_item_rows(const struct alloy_ui_item *it)
{
	if (it->rows)
		return it->rows;

	switch (it->kind) {
	case ALLOY_UI_SLIDER:
		return (it->flags & ALLOY_UI_F_NOBAR) ? 1 : 2;
	case ALLOY_UI_COLOR:
		return COLOR_ROWS;
	case ALLOY_UI_CUSTOM:
		return 1;
	default:
		return 1;
	}
}

static int item_selectable(const struct alloy_ui_item *it)
{
	if (it->flags & ALLOY_UI_F_STATIC)
		return 0;
	switch (it->kind) {
	case ALLOY_UI_HEADING:
	case ALLOY_UI_SPACER:
	case ALLOY_UI_TEXT:
		return 0;
	case ALLOY_UI_CUSTOM:
		return it->activate || it->set;
	default:
		return 1;
	}
}

int tui_pane_slots(struct alloy_ui *ui, const struct alloy_ui_item *items,
		   size_t count, struct tui_slot *out, int max)
{
	int n = 0;
	size_t i;
	int s;

	(void)ui;
	for (i = 0; i < count && n < max; i++) {
		if (!item_selectable(&items[i]))
			continue;
		if (items[i].kind == ALLOY_UI_COLOR) {
			for (s = 0; s < COLOR_SUBS && n < max; s++) {
				out[n].item = (int)i;
				out[n].sub = s;
				n++;
			}
			continue;
		}
		out[n].item = (int)i;
		out[n].sub = 0;
		n++;
	}
	return n;
}

int tui_pane_slot_count(struct alloy_ui *ui, int pane)
{
	const struct alloy_ui_screen *sc = tui_screen(ui);
	struct alloy_ui_item items[ALLOY_UI_MAX_ITEMS];
	struct tui_slot slots[ALLOY_UI_MAX_ITEMS];
	size_t count;

	if (pane == TUI_FOOTER_PANE)
		return (sc->flags & ALLOY_UI_SCREEN_NOFOOTER) ? 0 :
								FOOTER_COUNT;
	if (pane < 0 || pane >= sc->num_panes)
		return 0;
	if (sc->panes[pane].visible &&
	    !sc->panes[pane].visible(ui, &sc->panes[pane]))
		return 0;

	count = tui_pane_items(ui, &sc->panes[pane], items,
			       ALLOY_ARRAY_SIZE(items));
	return tui_pane_slots(ui, items, count, slots,
			      (int)ALLOY_ARRAY_SIZE(slots));
}

static int pane_visible(struct alloy_ui *ui, const struct alloy_ui_pane *p)
{
	return !p->visible || p->visible(ui, p);
}

void tui_layout(struct alloy_ui *ui, struct tui_rect *out)
{
	const struct alloy_ui_screen *sc = tui_screen(ui);
	int main_h = (sc->flags & ALLOY_UI_SCREEN_NOFOOTER) ? LINES - 1 :
							      LINES - 3;
	int col_w[ALLOY_UI_MAX_PANES];
	int col_x[ALLOY_UI_MAX_PANES];
	int col_flex[ALLOY_UI_MAX_PANES];
	int ncols = 0;
	int fixed = 0;
	int nflex = 0;
	int x = 0;
	int i;
	int c;

	memset(col_w, 0, sizeof(col_w));
	memset(col_flex, 0, sizeof(col_flex));

	for (i = 0; i < sc->num_panes; i++) {
		const struct alloy_ui_pane *p = &sc->panes[i];
		int w;

		if (!pane_visible(ui, p))
			continue;
		c = ALLOY_CLAMP(p->col, 0, ALLOY_UI_MAX_PANES - 1);
		ncols = ALLOY_MAX(ncols, c + 1);
		if (!p->width_pct) {
			col_flex[c] = 1;
			continue;
		}
		w = COLS * p->width_pct / 100;
		if (p->min_width)
			w = ALLOY_MAX(w, p->min_width);
		if (p->max_width)
			w = ALLOY_MIN(w, p->max_width);
		col_w[c] = ALLOY_MAX(col_w[c], w);
	}

	for (c = 0; c < ncols; c++) {
		if (col_flex[c])
			nflex++;
		else
			fixed += col_w[c];
	}
	for (c = 0; c < ncols; c++) {
		if (col_flex[c])
			col_w[c] =
				nflex ? ALLOY_MAX((COLS - fixed) / nflex, 8) :
					0;
		col_x[c] = x;
		x += col_w[c];
	}
	/* give any rounding slack to the last column so the row fills the width */
	if (ncols)
		col_w[ncols - 1] += COLS - x;

	for (c = 0; c < ncols; c++) {
		int y = 0;
		int used = 0;
		int flex_panes = 0;

		for (i = 0; i < sc->num_panes; i++) {
			const struct alloy_ui_pane *p = &sc->panes[i];

			if (p->col != c || !pane_visible(ui, p))
				continue;
			if (p->height_rows)
				used += p->height_rows;
			else if (p->height_pct)
				used += main_h * p->height_pct / 100;
			else
				flex_panes++;
		}
		for (i = 0; i < sc->num_panes; i++) {
			const struct alloy_ui_pane *p = &sc->panes[i];
			int h;

			if (p->col != c)
				continue;
			if (!pane_visible(ui, p)) {
				memset(&out[i], 0, sizeof(out[i]));
				continue;
			}
			if (p->height_rows)
				h = p->height_rows;
			else if (p->height_pct)
				h = main_h * p->height_pct / 100;
			else
				h = flex_panes ? (main_h - used) / flex_panes :
						 0;
			out[i].y = y;
			out[i].x = col_x[c];
			out[i].h = h;
			out[i].w = col_w[c];
			y += h;
		}
		/* last pane of the column absorbs the remainder */
		for (i = sc->num_panes - 1; i >= 0; i--) {
			if (sc->panes[i].col != c ||
			    !pane_visible(ui, &sc->panes[i]))
				continue;
			out[i].h += main_h - y;
			break;
		}
	}
}

void tui_draw_pane_box(int y, int x, int h, int w, const char *title,
		       int focused)
{
	int attr = COLOR_PAIR(focused ? CLR_FRAME_FOCUS : CLR_FRAME);
	int i;

	if (h < 2 || w < 2)
		return;
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

static void put_label(int y, int x, int w, const char *s, int selected,
		      int disabled)
{
	int attr = selected ? COLOR_PAIR(CLR_SELECTED) :
			      (disabled ? COLOR_PAIR(CLR_DISABLED) : A_NORMAL);
	int room = ALLOY_MIN(label_w(w), w);

	if (room <= 0)
		return;
	attron(attr);
	mvprintw(y, x, "%-*.*s", room, room, s ? s : "");
	attroff(attr);
}

/* value text, clipped to the room left in the pane */
static void put_value(int y, int x, int w, int style, const char *fmt, ...)
{
	char buf[80];
	va_list ap;
	int attr = tui_style_attr((enum alloy_ui_style)style);

	if (w <= 0)
		return;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	attron(attr);
	mvprintw(y, x, "%.*s", w, buf);
	attroff(attr);
}

/* proportional bar over [min, max] */
static void draw_bar(int y, int x, int w, int min, int max, int val)
{
	int span = ALLOY_CLAMP(w - 2, 4, 40);
	int pos;
	int i;

	if (max <= min)
		return;
	pos = (int)((long)(val - min) * (span - 1) / (max - min));
	mvaddch(y, x, '[');
	for (i = 0; i < span; i++)
		addch(i == pos ? (chtype)(ACS_CKBOARD | A_BOLD) :
				 (chtype)ACS_HLINE);
	addch(']');
}

/* filled step ladder, one cell per step of a small range */
static void draw_ladder(int y, int x, int room, int min, int max, int val)
{
	int i;

	if (max - min > 24 || max - min >= room)
		return;
	move(y, x);
	for (i = min; i <= max; i++)
		addch(i <= val ? (chtype)(ACS_CKBOARD | A_BOLD) :
				 (chtype)ACS_BULLET);
}

static void draw_color_channel(int y, int x, int w, const char *name,
			       uint8_t val, int selected, int disabled)
{
	int span = ALLOY_CLAMP(w - 16, 4, 20);
	int bar = (int)val * span / 255;
	int attr = selected ? COLOR_PAIR(CLR_SELECTED) :
			      (disabled ? COLOR_PAIR(CLR_DISABLED) : A_NORMAL);
	int i;

	attron(attr);
	mvprintw(y, x, "%s < %3u >", name, val);
	attroff(attr);

	move(y, x + 12);
	for (i = 0; i < span; i++)
		addch(i < bar ? (chtype)(ACS_CKBOARD | A_BOLD) :
				(chtype)ACS_BULLET);
}

static void draw_color_item(struct alloy_ui *ui, const struct alloy_ui_item *it,
			    int y, int x, int w, int sel_sub)
{
	struct alloy_rgb *rgb = it->color ? it->color(ui, it) : NULL;
	struct alloy_rgb fallback = { 0, 0, 0 };
	int disabled = (it->flags & ALLOY_UI_F_DISABLED) != 0;
	char hex[8];
	size_t i;

	if (!rgb)
		rgb = &fallback;

	if (COLORS >= 8) {
		init_pair(CLR_PICKER_PREVIEW, COLOR_BLACK,
			  tui_rgb_to_color(rgb));
		for (i = 0; i < TUI_PALETTE_SIZE; i++)
			init_pair((short)(CLR_PICKER_SWATCH + i),
				  tui_rgb_to_color(&tui_palette[i]), -1);
	}

	draw_color_channel(y + 0, x, w, "R", rgb->r, sel_sub == 0, disabled);
	draw_color_channel(y + 1, x, w, "G", rgb->g, sel_sub == 1, disabled);
	draw_color_channel(y + 2, x, w, "B", rgb->b, sel_sub == 2, disabled);

	put_label(y + 4, x, w, "PALETTE", sel_sub == 3, disabled);
	for (i = 0; i < TUI_PALETTE_SIZE; i++) {
		int sx = x + 12 + (int)i * 2 - (int)(i / 8) * 16;
		int sy = y + 4 + (int)(i / 8);
		int marked = sel_sub == 3 && (int)i == ui->swatch;

		if (marked)
			mvaddch(sy, sx - 1, '[' | A_BOLD);
		if (COLORS >= 8 && !disabled)
			attron(COLOR_PAIR(CLR_PICKER_SWATCH + i) | A_BOLD);
		else
			attron(COLOR_PAIR(CLR_DISABLED));
		mvaddch(sy, sx, ACS_DIAMOND);
		if (COLORS >= 8 && !disabled)
			attroff(COLOR_PAIR(CLR_PICKER_SWATCH + i) | A_BOLD);
		else
			attroff(COLOR_PAIR(CLR_DISABLED));
		if (marked)
			mvaddch(sy, sx + 1, ']' | A_BOLD);
	}

	put_label(y + 7, x, w, "HEX", sel_sub == 4, disabled);
	if (ui->hexbuf && sel_sub == 4) {
		attron(A_BOLD);
		mvprintw(y + 7, x + 12, "#%-6s_", ui->hexbuf);
		attroff(A_BOLD);
	} else {
		int attr = disabled ? COLOR_PAIR(CLR_DISABLED) : A_NORMAL;

		snprintf(hex, sizeof(hex), "%02X%02X%02X", rgb->r, rgb->g,
			 rgb->b);
		attron(attr);
		mvprintw(y + 7, x + 12, "#%s", hex);
		attroff(attr);
	}
	if (COLORS >= 8 && w > 22) {
		attron(COLOR_PAIR(CLR_PICKER_PREVIEW));
		mvprintw(y + 7, x + w - 8, "      ");
		attroff(COLOR_PAIR(CLR_PICKER_PREVIEW));
	}
}

/* one item, at (y, x) with @w columns available; returns rows consumed */
static int draw_item(struct alloy_ui *ui, const struct alloy_ui_item *it, int y,
		     int x, int w, int selected, int sel_sub)
{
	int disabled = (it->flags & ALLOY_UI_F_DISABLED) != 0;
	int rows = tui_item_rows(it);
	int val = it->get ? it->get(ui, it) : 0;
	int value_style = disabled ? ALLOY_UI_ST_DIM :
				     ((it->flags & ALLOY_UI_F_HOT) ?
					      ALLOY_UI_ST_HOT :
					      ALLOY_UI_ST_ACCENT);
	char text[64];

	text[0] = '\0';
	if (it->text)
		it->text(ui, it, text, sizeof(text));

	switch (it->kind) {
	case ALLOY_UI_HEADING:
		put_value(y, x, w, ALLOY_UI_ST_TITLE, "%s",
			  it->label ? it->label : "");
		break;
	case ALLOY_UI_SPACER:
		break;
	case ALLOY_UI_TEXT:
		put_label(y, x, w, it->label, 0, disabled);
		put_value(y, x + value_x(w), w - value_x(w), value_style, "%s",
			  text);
		break;
	case ALLOY_UI_TOGGLE:
		put_label(y, x, w, it->label, selected, disabled);
		put_value(y, x + value_x(w), w - value_x(w),
			  val ? ALLOY_UI_ST_HOT : ALLOY_UI_ST_DIM, "< %s >",
			  text[0] ? text : (val ? "ON " : "OFF"));
		break;
	case ALLOY_UI_STEPPER:
	case ALLOY_UI_SLIDER:
		put_label(y, x, w, it->label, selected, disabled);
		if (text[0])
			put_value(y, x + value_x(w), w - value_x(w),
				  value_style, "< %s >", text);
		else
			put_value(y, x + value_x(w), w - value_x(w),
				  value_style, "< %d%s >", val,
				  it->unit ? it->unit : "");
		if (it->kind == ALLOY_UI_SLIDER &&
		    !(it->flags & ALLOY_UI_F_NOBAR))
			draw_bar(y + 1, x, w, it->min_val, it->max_val, val);
		break;
	case ALLOY_UI_GAUGE:
		put_label(y, x, w, it->label, selected, disabled);
		if (disabled) {
			put_value(y, x + value_x(w), w - value_x(w),
				  ALLOY_UI_ST_DIM, "%s",
				  text[0] ? text : "N/A");
			break;
		}
		put_value(y, x + value_x(w), w - value_x(w), value_style,
			  "< %2d >", val);
		if (!(it->flags & ALLOY_UI_F_NOBAR))
			draw_ladder(y, x + value_x(w) + 8,
				    ALLOY_MIN(w - value_x(w) - 8, 24),
				    it->min_val, it->max_val, val);
		break;
	case ALLOY_UI_CHOICE: {
		const char *label = text;

		if (!label[0])
			label = (it->choices && val >= 0 &&
				 val < (int)it->num_choices) ?
					it->choices[val] :
					"?";
		put_label(y, x, w, it->label, selected, disabled);
		put_value(y, x + value_x(w), w - value_x(w), value_style,
			  "< %s >", label);
		break;
	}
	case ALLOY_UI_BUTTON: {
		int len = (int)strlen(it->label ? it->label : "") + 4;
		int bx = x + ALLOY_MAX((w - len) / 2, 0);

		put_value(y, bx, w - (bx - x),
			  selected ? ALLOY_UI_ST_SELECTED :
				     (disabled ? ALLOY_UI_ST_DIM :
						 ALLOY_UI_ST_BUTTON),
			  "  %s  ", it->label ? it->label : "");
		break;
	}
	case ALLOY_UI_COLOR:
		draw_color_item(ui, it, y, x, w, selected ? sel_sub : -1);
		break;
	case ALLOY_UI_CUSTOM: {
		struct alloy_ui_item tmp = *it;

		if (selected)
			tmp.flags |= ALLOY_UI_F_FOCUSED;
		if (it->draw)
			it->draw(ui, &tmp, tui_canvas_bind(y, x, rows, w));
		break;
	}
	default:
		break;
	}
	return rows;
}

static void draw_tabs(struct alloy_ui *ui, const struct alloy_ui_item *items,
		      size_t count, int y, int x, int max_x, int focused,
		      int sel)
{
	size_t i;

	for (i = 0; i < count; i++) {
		const struct alloy_ui_item *it = &items[i];
		int active = it->get && it->get(ui, it);
		int hot = focused && sel == (int)i;
		int attr = hot ? COLOR_PAIR(CLR_SELECTED) :
				 (active ? COLOR_PAIR(CLR_ACCENT) | A_BOLD :
					   COLOR_PAIR(CLR_BUTTON));
		int len = (int)strlen(it->label ? it->label : "") + 2;

		if (x + len >= max_x)
			break;
		attron(attr);
		mvprintw(y, x, " %s ", it->label ? it->label : "");
		attroff(attr);
		x += len + 1;
	}
}

static void draw_art(struct alloy_ui *ui, const struct tui_rect *r, int top,
		     int bottom)
{
	const char *art = ui->drv->ascii_art;
	int lines;
	int width;
	int y;
	int x;

	if (!art)
		return;

	tui_art_measure(art, &lines, &width);
	y = top + ALLOY_MAX(1, (bottom - top - lines) / 2);
	x = r->x + ALLOY_MAX(1, (r->w - width) / 2);
	tui_art_draw(ui, art, y, x, bottom, r->x + r->w - 1);
}

static void draw_pane(struct alloy_ui *ui, const struct alloy_ui_pane *pane,
		      const struct tui_rect *r, int focused, int cursor)
{
	struct alloy_ui_item items[ALLOY_UI_MAX_ITEMS];
	struct tui_slot slots[ALLOY_UI_MAX_ITEMS];
	size_t count;
	int nslots;
	int ix = r->x + 2;
	int iw = r->w - 4;
	int y;
	int i;
	int hint_rows;

	if (r->h < 3 || r->w < 6)
		return;

	tui_draw_pane_box(r->y, r->x, r->h, r->w,
			  pane->dyn_title ? pane->dyn_title(ui, pane) :
					    pane->title,
			  focused);
	count = tui_pane_items(ui, pane, items, ALLOY_ARRAY_SIZE(items));
	nslots = tui_pane_slots(ui, items, count, slots,
				(int)ALLOY_ARRAY_SIZE(slots));
	if (cursor >= nslots)
		cursor = nslots ? nslots - 1 : 0;

	hint_rows = pane->hint ? 2 : 0;

	if (pane->flags & ALLOY_UI_PANE_TABS) {
		draw_tabs(ui, items, count, r->y + 2, ix, r->x + r->w - 2,
			  focused, nslots ? slots[cursor].item : -1);
		if (pane->flags & ALLOY_UI_PANE_ART)
			draw_art(ui, r, r->y + 3, r->y + r->h - 1 - hint_rows);
		goto hint;
	}

	if (pane->flags & ALLOY_UI_PANE_ART) {
		int rows = 0;

		for (i = 0; i < (int)count; i++)
			rows += tui_item_rows(&items[i]);
		draw_art(ui, r, r->y + 1,
			 r->y + r->h - 1 - hint_rows - rows - 1);
		y = r->y + r->h - 1 - hint_rows - rows;
	} else {
		y = r->y + 2;
	}

	for (i = 0; i < (int)count; i++) {
		const struct alloy_ui_item *it = &items[i];
		int rows = tui_item_rows(it);
		int selected = 0;
		int sub = 0;

		if (y + rows > r->y + r->h - 1 - hint_rows)
			break;
		if (focused && nslots && slots[cursor].item == i) {
			selected = 1;
			sub = slots[cursor].sub;
		}
		draw_item(ui, it, y, ix, iw, selected, sub);
		y += rows;
	}

hint:
	if (pane->hint) {
		attron(COLOR_PAIR(CLR_DISABLED));
		mvprintw(r->y + r->h - 2, ix, "%.*s", iw, pane->hint);
		attroff(COLOR_PAIR(CLR_DISABLED));
	}
}

static void draw_footer(struct alloy_ui *ui)
{
	int focused = ui->focus == TUI_FOOTER_PANE;
	int sel = ui->cursor[TUI_FOOTER_PANE];
	int y = LINES - 3;
	int x;

	mvhline(y, 0, ACS_HLINE, COLS);

	if (focused && sel == FOOTER_LIVE_PREVIEW)
		attron(COLOR_PAIR(CLR_SELECTED));
	mvprintw(y + 1, 2, " LIVE PREVIEW %s ",
		 ui->live_preview ? "[ON] " : "[OFF]");
	if (focused && sel == FOOTER_LIVE_PREVIEW)
		attroff(COLOR_PAIR(CLR_SELECTED));

	if (ui->firmware[0])
		mvprintw(y + 1, 24, "fw %s", ui->firmware);

	x = COLS - 22;
	if (focused && sel == FOOTER_REVERT)
		attron(COLOR_PAIR(CLR_SELECTED));
	else
		attron(COLOR_PAIR(CLR_BUTTON));
	mvprintw(y + 1, x, " REVERT ");
	attroff(COLOR_PAIR(CLR_SELECTED));
	attroff(COLOR_PAIR(CLR_BUTTON));

	x += 10;
	if (focused && sel == FOOTER_SAVE)
		attron(COLOR_PAIR(CLR_SELECTED));
	else
		attron(COLOR_PAIR(CLR_BUTTON_HOT));
	mvprintw(y + 1, x, " SAVE%s ", ui->dirty ? "*" : " ");
	attroff(COLOR_PAIR(CLR_SELECTED));
	attroff(COLOR_PAIR(CLR_BUTTON_HOT));
}

void tui_render(struct alloy_ui *ui)
{
	const struct alloy_ui_screen *sc = tui_screen(ui);
	struct tui_rect rects[ALLOY_UI_MAX_PANES];
	const char *hint = sc->hint;
	int i;

	erase();

	if (COLS < MIN_COLS || LINES < MIN_LINES) {
		mvprintw(0, 0, "Terminal too small: need at least %dx%d",
			 MIN_COLS, MIN_LINES);
		return;
	}

	tui_layout(ui, rects);

	for (i = 0; i < sc->num_panes; i++) {
		if (rects[i].h <= 0 || rects[i].w <= 0)
			continue;
		draw_pane(ui, &sc->panes[i], &rects[i], ui->focus == i,
			  ui->cursor[i]);
	}

	if (!(sc->flags & ALLOY_UI_SCREEN_NOFOOTER))
		draw_footer(ui);

	mvhline(LINES - 1, 0, ' ', COLS);
	attron(COLOR_PAIR(CLR_DISABLED));
	mvprintw(LINES - 1, 2, "%.*s", COLS / 2, ui->status);
	if (hint)
		mvprintw(LINES - 1, ALLOY_MAX(COLS - (int)strlen(hint) - 2, 2),
			 "%s", hint);
	attroff(COLOR_PAIR(CLR_DISABLED));
}

void tui_draw(struct alloy_ui *ui)
{
	tui_render(ui);
	refresh();
}
