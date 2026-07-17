// SPDX-License-Identifier: GPL-2.0-only
/*
 * Modal dialogs.
 *
 * Modals grab the keyboard until dismissed:
 * the pane content stays visible underneath.
 */
#include <string.h>

#include "tui_internal.h"

void tui_modal_frame(int h, int w, int *py, int *px, const char *title)
{
	int y = (LINES - h) / 2;
	int x = (COLS - w) / 2;
	int i;

	attron(COLOR_PAIR(CLR_FRAME_FOCUS) | A_BOLD);
	mvaddch(y, x, ACS_ULCORNER);
	mvaddch(y, x + w - 1, ACS_URCORNER);
	mvaddch(y + h - 1, x, ACS_LLCORNER);
	mvaddch(y + h - 1, x + w - 1, ACS_LRCORNER);
	mvhline(y, x + 1, ACS_HLINE, w - 2);
	mvhline(y + h - 1, x + 1, ACS_HLINE, w - 2);
	mvvline(y + 1, x, ACS_VLINE, h - 2);
	mvvline(y + 1, x + w - 1, ACS_VLINE, h - 2);
	attroff(COLOR_PAIR(CLR_FRAME_FOCUS) | A_BOLD);

	for (i = 1; i < h - 1; i++)
		mvhline(y + i, x + 1, ' ', w - 2);

	attron(COLOR_PAIR(CLR_TITLE) | A_BOLD);
	mvprintw(y, x + 2, " %s ", title);
	attroff(COLOR_PAIR(CLR_TITLE) | A_BOLD);

	*py = y;
	*px = x;
}

void tui_modal_message(const char *title, const char *text)
{
	int w = (int)ALLOY_MAX(strlen(text), strlen(title)) + 6;
	int y;
	int x;

	tui_modal_frame(5, w, &y, &x, title);
	mvprintw(y + 2, x + 3, "%s", text);
	attron(COLOR_PAIR(CLR_DISABLED));
	mvprintw(y + 4, x + 2, " any key ");
	attroff(COLOR_PAIR(CLR_DISABLED));
	refresh();
	getch();
}

/* selectable actions offered by remap modal */
struct remap_choice {
	const char *label;
	struct alloy_action action;
	int capture_key; /* prompt for keyboard key afterwards */
};

static const struct remap_choice remap_choices[] = {
	{ "Button 1 (Left Click)", { ALLOY_ACT_MOUSE, 1 }, 0 },
	{ "Button 2 (Right Click)", { ALLOY_ACT_MOUSE, 2 }, 0 },
	{ "Button 3 (Middle Click)", { ALLOY_ACT_MOUSE, 3 }, 0 },
	{ "Button 4 (Back)", { ALLOY_ACT_MOUSE, 4 }, 0 },
	{ "Button 5 (Forward)", { ALLOY_ACT_MOUSE, 5 }, 0 },
	{ "Button 6", { ALLOY_ACT_MOUSE, 6 }, 0 },
	{ "CPI Toggle", { ALLOY_ACT_DPI_CYCLE, 0 }, 0 },
	{ "Scroll Up", { ALLOY_ACT_SCROLL_UP, 0 }, 0 },
	{ "Scroll Down", { ALLOY_ACT_SCROLL_DOWN, 0 }, 0 },
	{ "Keyboard Key...", { ALLOY_ACT_KEYBOARD, 0 }, 1 },
	{ "Disabled", { ALLOY_ACT_DISABLED, 0 }, 0 },
};

/*
 * Minimal ASCII to USB HID keyboard usage translation for the "Keyboard Key..." capture.
 * Covers letters, digits and few essentials.
 * Anything else is rejected.
 */
static int ascii_to_hid(int ch)
{
	if (ch >= 'a' && ch <= 'z')
		return 0x04 + (ch - 'a');
	if (ch >= 'A' && ch <= 'Z')
		return 0x04 + (ch - 'A');
	if (ch >= '1' && ch <= '9')
		return 0x1E + (ch - '1');
	switch (ch) {
	case '0':
		return 0x27;
	case '\n':
	case KEY_ENTER:
		return 0x28;
	case '\t':
		return 0x2B;
	case ' ':
		return 0x2C;
	case '-':
		return 0x2D;
	case '=':
		return 0x2E;
	default:
		return -1;
	}
}

static int capture_keyboard_key(struct tui *t, struct alloy_action *out)
{
	int y;
	int x;
	int ch;
	int usage;

	tui_modal_frame(5, 36, &y, &x, "PRESS A KEY");
	mvprintw(y + 2, x + 3, "press the key to bind (esc: back)");
	refresh();

	ch = getch();
	if (ch == 27)
		return -1;
	usage = ascii_to_hid(ch);
	if (usage < 0) {
		tui_status(t, "unsupported key for binding");
		return -1;
	}
	out->type = ALLOY_ACT_KEYBOARD;
	out->value = (uint16_t)usage;
	return 0;
}

void tui_modal_remap(struct tui *t, int button)
{
	const int count = (int)ALLOY_ARRAY_SIZE(remap_choices);
	struct alloy_action action;
	int sel = 0;
	int done = 0;
	int y;
	int x;
	int i;
	int ch;

	while (!done) {
		tui_draw(t);
		tui_modal_frame(count + 4, 34, &y, &x,
				t->drv->buttons[button].name);

		for (i = 0; i < count; i++) {
			if (i == sel)
				attron(COLOR_PAIR(CLR_SELECTED));
			mvprintw(y + 2 + i, x + 3, "%-28s",
				 remap_choices[i].label);
			if (i == sel)
				attroff(COLOR_PAIR(CLR_SELECTED));
		}
		attron(COLOR_PAIR(CLR_DISABLED));
		mvprintw(y + count + 3, x + 2, " enter: bind  esc: cancel ");
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
			action = remap_choices[sel].action;
			if (remap_choices[sel].capture_key &&
			    capture_keyboard_key(t, &action))
				break;
			t->cfg.buttons[button] = action;
			t->dirty = 1;
			if (t->live_preview)
				tui_apply(t, t->drv->ops->apply_buttons,
					  "buttons");
			done = 1;
			break;
		default:
			break;
		}
	}
}
