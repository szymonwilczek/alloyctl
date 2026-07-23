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
	/* getch runs on the animation timeout: wait for a real key, not a tick */
	while (getch() == ERR)
		;
}

/*
 * Quit guard shown when unsaved changes exist:
 * save first, throw them away, or stay.
 * Failed save (no device ACK) keeps the program running
 * so nothing is silently lost.
 */
void tui_modal_confirm_quit(struct tui *t)
{
	static const char *const choices[] = {
		"Save and quit",
		"Quit without saving",
		"Cancel",
	};
	const int count = (int)ALLOY_ARRAY_SIZE(choices);
	int sel = 0;
	int y;
	int x;
	int i;
	int ch;

	for (;;) {
		tui_render(t);
		tui_modal_frame(count + 4, 30, &y, &x, "UNSAVED CHANGES");

		for (i = 0; i < count; i++) {
			if (i == sel)
				attron(COLOR_PAIR(CLR_SELECTED));
			mvprintw(y + 2 + i, x + 3, "%-24s", choices[i]);
			if (i == sel)
				attroff(COLOR_PAIR(CLR_SELECTED));
		}
		attron(COLOR_PAIR(CLR_DISABLED));
		mvprintw(y + count + 3, x + 2, " enter: pick  esc: stay ");
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
			if (sel == 0) {
				if (tui_save(t) == 0)
					t->quit = 1;
			} else if (sel == 1) {
				/* undo live-previewed changes on the mouse before leaving,
				 * back to the startup state */
				tui_revert(t);
				t->quit = 1;
			}
			return;
		default:
			break;
		}
	}
}

/*
 * Dongle pairing wizard (ALLOY_CAP_PAIRING), modeled on the GG "connect a new device" flow.
 *
 * Two stages:
 *
 *	1. PROBE	Live-check that the 2.4 GHz receiver is plugged in.
 *			It has to be (pairing runs through it); re-probed every frame
 *			so pulling the dongle mid-flow shows up at once.
 *
 *   2. INSTRUCT	Walk through the mouse-side gesture (OFF, hold CPI, flick to
 *			2.4 GHz) and, on Enter, kick off pairing via ops->pair.
 *
 * On Enter, ops->pair puts the receiver into bind mode; success means the write
 * went out, not that a mouse bound - the link/battery coming up afterwards is the
 * real confirmation. A driver whose bind opcode is still unmapped may report
 * ALLOY_PAIR_UNIMPLEMENTED, which the wizard surfaces honestly.
 */
enum pair_stage {
	PAIR_STAGE_PROBE,
	PAIR_STAGE_INSTRUCT,
};

static void pair_begin(struct tui *t)
{
	int ret = t->drv->ops->pair ? t->drv->ops->pair(t->dev) : -1;

	if (ret == ALLOY_PAIR_UNIMPLEMENTED)
		tui_status(t,
			   "pairing: receiver bind command not captured yet");
	else if (ret == 0)
		tui_status(t,
			   "pairing started - waiting for the mouse to link");
	else
		tui_status(t, "pairing: could not start (receiver error)");
}

void tui_modal_pair(struct tui *t)
{
	const int w = 52;
	const int h = 11;
	enum pair_stage stage = PAIR_STAGE_PROBE;
	int y;
	int x;
	int ch;

	for (;;) {
		int dongle = alloy_hid_present(t->drv->vendor_id,
					       t->drv->product_id,
					       t->drv->interface);

		tui_render(t);
		tui_modal_frame(h, w, &y, &x, "PAIR A NEW DEVICE");

		if (stage == PAIR_STAGE_PROBE) {
			mvprintw(y + 2, x + 3,
				 "Step 1 of 2 - wireless receiver");
			if (dongle) {
				attron(COLOR_PAIR(CLR_LINK_RF) | A_BOLD);
				mvprintw(y + 4, x + 3, "receiver detected");
				attrset(A_NORMAL);
				mvprintw(y + 6, x + 3,
					 "Keep the 2.4 GHz dongle plugged in.");
			} else {
				attron(COLOR_PAIR(CLR_BAT_LOW) | A_BOLD);
				mvprintw(y + 4, x + 3, "receiver NOT found");
				attrset(A_NORMAL);
				mvprintw(
					y + 6, x + 3,
					"Plug the 2.4 GHz dongle into a USB port.");
			}
			attron(COLOR_PAIR(CLR_DISABLED));
			mvprintw(
				y + h - 2, x + 2,
				dongle ?
					" enter: next   esc: cancel " :
					" waiting for receiver...   esc: cancel ");
			attrset(A_NORMAL);
		} else {
			mvprintw(y + 2, x + 3,
				 "Step 2 of 2 - put the mouse in pairing mode");
			mvprintw(y + 4, x + 3,
				 "1. Slide the mouse power switch to OFF.");
			mvprintw(y + 5, x + 3,
				 "2. Press and hold the CPI button.");
			mvprintw(y + 6, x + 3,
				 "3. Holding it, slide the switch to 2.4 GHz");
			mvprintw(y + 7, x + 3, "   (the LEDs blink white).");
			attron(COLOR_PAIR(CLR_DISABLED));
			mvprintw(y + h - 2, x + 2,
				 " enter: begin pairing   esc: back ");
			attrset(A_NORMAL);
		}
		refresh();

		ch = getch();
		if (ch == ERR)
			continue; /* animation tick: re-probe and redraw */
		switch (ch) {
		case 27: /* esc */
			if (stage == PAIR_STAGE_INSTRUCT) {
				stage = PAIR_STAGE_PROBE;
				break;
			}
			return;
		case '\n':
		case KEY_ENTER:
			if (stage == PAIR_STAGE_PROBE) {
				if (dongle)
					stage = PAIR_STAGE_INSTRUCT;
				break;
			}
			pair_begin(t);
			return;
		default:
			break;
		}
	}
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

	/* ignore animation-timeout ticks; wait for an actual keypress */
	while ((ch = getch()) == ERR)
		;
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
		tui_render(t);
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
