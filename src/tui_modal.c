// SPDX-License-Identifier: GPL-2.0-only
/*
 * Modal dialogs.
 *
 * All of them are generic services offered to driver-side UI code:
 * a message box, a pick-one list, a key capture, and an escape hatch where
 * the driver paints the body itself on a canvas while the front-end owns
 * the frame and the key loop.
 *
 * The quit guard is the one modal the front-end raises on its own,
 * because unsaved-changes is front-end state.
 *
 * Modals grab the keyboard until dismissed; the screen underneath stays visible,
 * and the single refresh at the end of each frame composites the two.
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
	mvprintw(y, x + 2, " %s ", title ? title : "");
	attroff(COLOR_PAIR(CLR_TITLE) | A_BOLD);

	*py = y;
	*px = x;
}

void tui_modal_message(struct alloy_ui *ui, const char *title, const char *text)
{
	int w = (int)ALLOY_MAX(strlen(text), strlen(title)) + 6;
	int y;
	int x;

	tui_render(ui);
	tui_modal_frame(5, w, &y, &x, title);
	mvprintw(y + 2, x + 3, "%s", text);
	attron(COLOR_PAIR(CLR_DISABLED));
	mvprintw(y + 4, x + 2, " any key ");
	attroff(COLOR_PAIR(CLR_DISABLED));
	refresh();
	/* getch runs on the animation timeout: wait for a real key, not a tick */
	while (getch() == ERR)
		;
}

/*
 * Pick one of @items.
 * Returns the chosen index, or -1 when the user backed out.
 */
int tui_modal_menu(struct alloy_ui *ui, const char *title,
		   const char *const *items, int count, int cur)
{
	int sel = ALLOY_CLAMP(cur, 0, count - 1);
	int w = (int)strlen(title ? title : "") + 8;
	int y;
	int x;
	int i;
	int ch;

	if (count < 1)
		return -1;
	for (i = 0; i < count; i++)
		w = ALLOY_MAX(w, (int)strlen(items[i]) + 8);
	w = ALLOY_MIN(w, COLS - 4);

	for (;;) {
		tui_render(ui);
		tui_modal_frame(count + 4, w, &y, &x, title);

		for (i = 0; i < count; i++) {
			if (i == sel)
				attron(COLOR_PAIR(CLR_SELECTED));
			mvprintw(y + 2 + i, x + 3, "%-*s", w - 6, items[i]);
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
		case 'q':
			return -1;
		case '\n':
		case KEY_ENTER:
			return sel;
		default:
			break;
		}
	}
}

/*
 * Wait for one keypress and return it in the front-end's key encoding
 * (ALLOY_UI_KEY_*, or the character itself).
 * Escape returns ALLOY_UI_KEY_ESC.
 */
int tui_modal_capture_key(struct alloy_ui *ui, const char *title,
			  const char *prompt)
{
	int w = (int)strlen(prompt ? prompt : "") + 8;
	int y;
	int x;
	int ch;

	tui_render(ui);
	tui_modal_frame(5, w, &y, &x, title);
	mvprintw(y + 2, x + 3, "%s", prompt ? prompt : "");
	refresh();

	/* ignore animation-timeout ticks; wait for an actual keypress */
	while ((ch = getch()) == ERR)
		;
	return tui_translate_key(ch);
}

/*
 * Driver-owned modal: the front-end draws the frame and pumps keys,
 * the driver paints the body on a canvas clipped to the frame's interior
 * and decides when the dialog is done.
 */
int tui_modal_run(struct alloy_ui *ui, const struct alloy_ui_modal *m)
{
	int h = ALLOY_CLAMP(m->h, 5, LINES - 2);
	int w = ALLOY_CLAMP(m->w, 10, COLS - 2);
	int y;
	int x;
	int ch;
	int ret;

	for (;;) {
		tui_render(ui);
		tui_modal_frame(h, w, &y, &x, m->title);
		if (m->draw)
			m->draw(ui, tui_canvas_bind(y + 1, x + 2, h - 2, w - 4),
				m->data);
		refresh();

		ch = getch();
		if (!m->key)
			return 0;
		ret = m->key(ui, tui_translate_key(ch), m->data);
		if (ret)
			return ret;
	}
}

/*
 * Quit guard shown when unsaved changes exist:
 * save first, throw them away, or stay.
 * Failed save (no device ACK) keeps the program running so nothing is silently lost.
 */
void tui_modal_confirm_quit(struct alloy_ui *ui)
{
	static const char *const choices[] = {
		"Save and quit",
		"Quit without saving",
		"Cancel",
	};
	int sel = tui_modal_menu(ui, "UNSAVED CHANGES", choices,
				 (int)ALLOY_ARRAY_SIZE(choices), 0);

	if (sel == 0) {
		if (tui_save(ui) == 0)
			ui->quit = 1;
	} else if (sel == 1) {
		/* undo live-previewed changes on the device before leaving */
		tui_revert(ui);
		ui->quit = 1;
	}
}
