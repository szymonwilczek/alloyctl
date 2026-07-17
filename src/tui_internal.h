/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Internals shared between the TUI core,
 * the pane renderers and the modal dialogs.
 *
 * Not part of any public interface.
 */
#ifndef ALLOY_TUI_INTERNAL_H
#define ALLOY_TUI_INTERNAL_H

#include <curses.h>

#include "driver.h"
#include "state.h"
#include "tui.h"

enum tui_pane {
	PANE_ACTIONS,
	PANE_CENTER,
	PANE_SENSITIVITY,
	PANE_TUNING,
	PANE_FOOTER,
	PANE_COUNT,
};

/* Items in the footer pane, left to right */
enum tui_footer_item {
	FOOTER_LIVE_PREVIEW,
	FOOTER_REVERT,
	FOOTER_SAVE,
	FOOTER_COUNT,
};

/* ncurses color pairs */
enum tui_color {
	CLR_FRAME = 1,
	CLR_FRAME_FOCUS,
	CLR_TITLE,
	CLR_SELECTED,
	CLR_ACCENT,
	CLR_DISABLED,
	CLR_BUTTON,
	CLR_BUTTON_HOT,
	CLR_ZONE_BASE, /* CLR_ZONE_BASE + zone index, dynamic RGB */
};

struct tui {
	struct alloy_device *dev;
	const struct alloy_driver *drv;

	struct alloy_config cfg; /* working configuration */
	struct alloy_config baseline; /* REVERT target */

	int live_preview;
	int dirty; /* differs from what SAVE last wrote */

	enum tui_pane focus;
	int cursor[PANE_COUNT]; /* per-pane selected item */

	char status[128];
	char firmware[48];

	int quit;
};

/* tui.c */
void tui_status(struct tui *t, const char *fmt, ...)
	__attribute__((format(printf, 2, 3)));
void tui_apply(struct tui *t,
	       int (*op)(struct alloy_device *, const struct alloy_config *),
	       const char *what);
void tui_apply_all(struct tui *t);
int tui_pane_item_count(const struct tui *t, enum tui_pane pane);

/* tui_panes.c */
void tui_draw(struct tui *t);
void tui_zone_color_pairs(const struct tui *t);

/* tui_modal.c */
void tui_modal_message(const char *title, const char *text);
void tui_modal_remap(struct tui *t, int button);

/* tui_input.c */
void tui_handle_key(struct tui *t, int ch);

#endif /* ALLOY_TUI_INTERNAL_H */
