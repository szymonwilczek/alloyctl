// SPDX-License-Identifier: GPL-2.0-only
/*
 * Generic input dispatch.
 *
 * Navigation is over the same widget model the renderer walks:
 * panes hold items, items hold slots, and a keypress either moves the cursor
 * or is handed to the item's own callbacks.
 * Nothing here knows what any control means; keys the front-end does not claim
 * go to the driver's desc->key hook.
 */
#include <string.h>

#include "tui_internal.h"

int tui_translate_key(int ch)
{
	switch (ch) {
	case ERR:
		return ALLOY_UI_KEY_NONE;
	case '\n':
	case KEY_ENTER:
		return ALLOY_UI_KEY_ENTER;
	case 27:
		return ALLOY_UI_KEY_ESC;
	case KEY_UP:
		return ALLOY_UI_KEY_UP;
	case KEY_DOWN:
		return ALLOY_UI_KEY_DOWN;
	case KEY_LEFT:
		return ALLOY_UI_KEY_LEFT;
	case KEY_RIGHT:
		return ALLOY_UI_KEY_RIGHT;
	case KEY_BACKSPACE:
	case 127:
	case 8:
		return ALLOY_UI_KEY_BACKSPACE;
	case '\t':
		return ALLOY_UI_KEY_TAB;
	default:
		return (ch > 0 && ch < 256) ? ch : ALLOY_UI_KEY_NONE;
	}
}

static int item_step(const struct alloy_ui_item *it, int big)
{
	int step = it->step ? it->step : 1;

	if (big)
		return it->big_step ? it->big_step : step;
	return step;
}

static void item_commit(struct alloy_ui *ui, const struct alloy_ui_item *it,
			int val)
{
	if (it->set)
		it->set(ui, it, val);
	if (it->changed)
		it->changed(ui, it);
}

/* the color block's own cursor stops: R, G, B, palette, hex */
static void color_adjust(struct alloy_ui *ui, const struct alloy_ui_item *it,
			 int sub, int dir, int big)
{
	struct alloy_rgb *rgb = it->color ? it->color(ui, it) : NULL;
	uint8_t *chan;
	int val;

	if (sub == 3) {
		ui->swatch = (ui->swatch + TUI_PALETTE_SIZE + dir) %
			     TUI_PALETTE_SIZE;
		return;
	}
	if (!rgb || sub > 2 || (it->flags & ALLOY_UI_F_DISABLED))
		return;

	chan = sub == 0 ? &rgb->r : (sub == 1 ? &rgb->g : &rgb->b);
	val = *chan + dir * (big ? 16 : 1);
	*chan = (uint8_t)ALLOY_CLAMP(val, 0, 255);
	if (it->changed)
		it->changed(ui, it);
}

static void color_activate(struct alloy_ui *ui, const struct alloy_ui_item *it,
			   int sub)
{
	struct alloy_rgb *rgb = it->color ? it->color(ui, it) : NULL;

	if (!rgb || (it->flags & ALLOY_UI_F_DISABLED))
		return;

	if (sub == 3) {
		*rgb = tui_palette[ui->swatch];
	} else if (sub == 4) {
		if (tui_prompt_hex(ui, rgb))
			return;
	} else {
		return;
	}
	if (it->changed)
		it->changed(ui, it);
}

void tui_item_adjust(struct alloy_ui *ui, const struct alloy_ui_item *it,
		     int sub, int dir, int big)
{
	int val;

	if (it->kind == ALLOY_UI_COLOR) {
		color_adjust(ui, it, sub, dir, big);
		return;
	}
	if (it->flags & ALLOY_UI_F_DISABLED)
		return;

	val = it->get ? it->get(ui, it) : 0;

	switch (it->kind) {
	case ALLOY_UI_TOGGLE:
		item_commit(ui, it, dir > 0);
		break;
	case ALLOY_UI_CHOICE:
		if (!it->num_choices)
			break;
		val = (val + dir + (int)it->num_choices) % (int)it->num_choices;
		item_commit(ui, it, val);
		break;
	case ALLOY_UI_STEPPER:
	case ALLOY_UI_SLIDER:
	case ALLOY_UI_GAUGE:
	case ALLOY_UI_CUSTOM:
		if (!it->set)
			break;
		val += dir * item_step(it, big);
		if (it->max_val > it->min_val)
			val = ALLOY_CLAMP(val, it->min_val, it->max_val);
		item_commit(ui, it, val);
		break;
	case ALLOY_UI_BUTTON:
		break;
	default:
		break;
	}
}

void tui_item_activate(struct alloy_ui *ui, const struct alloy_ui_item *it,
		       int sub)
{
	int val;

	if (it->kind == ALLOY_UI_COLOR) {
		color_activate(ui, it, sub);
		return;
	}
	if (it->activate) {
		it->activate(ui, it);
		return;
	}
	if (it->flags & ALLOY_UI_F_DISABLED)
		return;

	val = it->get ? it->get(ui, it) : 0;
	switch (it->kind) {
	case ALLOY_UI_TOGGLE:
		item_commit(ui, it, !val);
		break;
	case ALLOY_UI_CHOICE:
		if (it->num_choices)
			item_commit(ui, it, (val + 1) % (int)it->num_choices);
		break;
	default:
		break;
	}
}

/* resolve the focused pane's cursor to one item; returns 0 when there is none */
static int focused_item(struct alloy_ui *ui, struct alloy_ui_item *out,
			int *sub)
{
	const struct alloy_ui_screen *sc = tui_screen(ui);
	struct alloy_ui_item items[ALLOY_UI_MAX_ITEMS];
	struct tui_slot slots[ALLOY_UI_MAX_ITEMS];
	size_t count;
	int nslots;
	int cur;

	if (ui->focus < 0 || ui->focus >= sc->num_panes)
		return 0;

	count = tui_pane_items(ui, &sc->panes[ui->focus], items,
			       ALLOY_ARRAY_SIZE(items));
	nslots = tui_pane_slots(ui, items, count, slots,
				(int)ALLOY_ARRAY_SIZE(slots));
	if (!nslots)
		return 0;

	cur = ALLOY_CLAMP(ui->cursor[ui->focus], 0, nslots - 1);
	*out = items[slots[cur].item];
	*sub = slots[cur].sub;
	return 1;
}

static void footer_activate(struct alloy_ui *ui)
{
	switch (ui->cursor[TUI_FOOTER_PANE]) {
	case FOOTER_LIVE_PREVIEW:
		ui->live_preview = !ui->live_preview;
		if (ui->live_preview) {
			tui_apply_all(ui);
			tui_status(ui, "live preview on");
		} else {
			tui_status(ui, "live preview off - "
				       "changes stay on screen only");
		}
		break;
	case FOOTER_REVERT:
		tui_revert(ui);
		tui_status(ui, "reverted to session baseline");
		break;
	case FOOTER_SAVE:
		tui_save(ui);
		break;
	default:
		break;
	}
}

/* step focus by dir, skipping panes that hold nothing selectable */
static void focus_step(struct alloy_ui *ui, int dir)
{
	const struct alloy_ui_screen *sc = tui_screen(ui);
	int last = (sc->flags & ALLOY_UI_SCREEN_NOFOOTER) ? sc->num_panes - 1 :
							    sc->num_panes;
	int span = last + 1;
	int tries;
	int idx;

	if (span <= 0)
		return;

	idx = ui->focus == TUI_FOOTER_PANE ? sc->num_panes : ui->focus;
	for (tries = 0; tries < span; tries++) {
		idx = (idx + dir + span) % span;
		ui->focus = idx == sc->num_panes ? TUI_FOOTER_PANE : idx;
		if (tui_pane_slot_count(ui, ui->focus) > 0)
			return;
	}
	ui->focus = 0;
}

void tui_handle_key(struct alloy_ui *ui, int ch)
{
	struct alloy_ui_item it;
	int sub = 0;
	int have;
	int count;
	int dir;
	int big;

	switch (ch) {
	case 'q':
		if (ui->screen != 0) {
			alloy_ui_goto_screen(ui, ui->desc->screens[0].id);
			return;
		}
		if (ui->dirty)
			tui_modal_confirm_quit(ui);
		else
			ui->quit = 1;
		return;
	case 27: /* esc leaves a sub-screen, never the program */
		if (ui->screen != 0)
			alloy_ui_goto_screen(ui, ui->desc->screens[0].id);
		return;
	case 's':
		tui_save(ui);
		return;
	case '\t':
		focus_step(ui, 1);
		return;
	case KEY_BTAB:
		focus_step(ui, -1);
		return;
	case KEY_RESIZE:
		return;
	default:
		break;
	}

	count = tui_pane_slot_count(ui, ui->focus);
	have = focused_item(ui, &it, &sub);

	switch (ch) {
	case KEY_UP:
	case 'k':
		if (count)
			ui->cursor[ui->focus] =
				(ui->cursor[ui->focus] + count - 1) % count;
		return;
	case KEY_DOWN:
	case 'j':
		if (count)
			ui->cursor[ui->focus] =
				(ui->cursor[ui->focus] + 1) % count;
		return;
	case KEY_LEFT:
	case 'h':
	case KEY_RIGHT:
	case 'l':
	case 'H':
	case 'L':
		dir = (ch == KEY_LEFT || ch == 'h' || ch == 'H') ? -1 : 1;
		big = (ch == 'H' || ch == 'L');
		if (have)
			tui_item_adjust(ui, &it, sub, dir, big);
		return;
	case '\n':
	case KEY_ENTER:
		if (ui->focus == TUI_FOOTER_PANE)
			footer_activate(ui);
		else if (have)
			tui_item_activate(ui, &it, sub);
		return;
	case 'x':
		/* jump straight into the hex field of a focused color block */
		if (have && it.kind == ALLOY_UI_COLOR) {
			ui->cursor[ui->focus] += 4 - sub;
			tui_item_activate(ui, &it, 4);
			return;
		}
		break;
	default:
		break;
	}

	{
		int (*key)(struct alloy_ui *, int) = TUI_HOOK(ui, key);

		if (key)
			key(ui, tui_translate_key(ch));
	}
}
