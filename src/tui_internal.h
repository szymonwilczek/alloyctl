/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Internals shared between the front-end's translation units.
 *
 * Front-end holds a working configuration it cannot read, a screen and pane
 * cursor, and a curses session.
 * Everything else it asks the driver's struct alloy_ui_desc for.
 *
 * Not part of any public interface.
 */
#ifndef ALLOY_TUI_INTERNAL_H
#define ALLOY_TUI_INTERNAL_H

#include <curses.h>

#include "driver.h"
#include "state.h"
#include "tui.h"
#include "ui.h"

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
	CLR_INFO,
	CLR_GOOD,
	CLR_WARN,
	CLR_BAD,
	CLR_RGB_FALLBACK, /* stand-in tint on terminals without the color cube */
	CLR_PICKER_PREVIEW,
	CLR_PICKER_SWATCH, /* + swatch index */
	CLR_RGB_CUBE_BASE =
		32, /* 216-color cube (32..247), fits safely in 256 pairs */
};

/* preview animation tick */
#define TUI_FRAME_MS 100

/* the footer is addressed as the pane just past the screen's own panes */
#define TUI_FOOTER_PANE ALLOY_UI_MAX_PANES

/* Items in the footer, left to right */
enum tui_footer_item {
	FOOTER_LIVE_PREVIEW,
	FOOTER_REVERT,
	FOOTER_SAVE,
	FOOTER_COUNT,
};

struct tui_rect {
	int y;
	int x;
	int h;
	int w;
};

/*
 * Navigable position inside a pane:
 * one item, plus the sub-row for compound widgets
 * (the color block is five stops in one item)
 */
struct tui_slot {
	int item;
	int sub;
};

struct alloy_ui {
	struct alloy_device *dev;
	const struct alloy_driver *drv;
	const struct alloy_ui_desc *desc;

	struct alloy_config *cfg; /* working configuration */
	struct alloy_config *baseline; /* REVERT target */

	int live_preview;
	int dirty; /* differs from what SAVE last wrote */

	int screen; /* index into the screen table */
	int focus; /* pane index, or TUI_FOOTER_PANE */
	int cursor[ALLOY_UI_MAX_PANES + 1];

	/* scratch integers owned by the driver's UI description */
	int vars[ALLOY_UI_MAX_VARS];

	char status[128];
	char firmware[48];

	/*
	 * One-shot device handshake (firmware read + initial push),
	 * done lazily once desc->ready() says the device is actually reachable
	 */
	int device_synced;
	int probed_hw;

	int quit;

	/* non-NULL while a hex color is being typed inside a color widget */
	const char *hexbuf;
	int swatch; /* palette cursor inside a color widget */
};

/*
 * Resolve one hook through the description's inheritance chain:
 * nearest non-NULL implementation, or NULL when nobody supplies one
 */
#define TUI_HOOK(ui, member)                                           \
	({                                                             \
		const struct alloy_ui_desc *d_ = (ui)->desc;           \
		/* unevaluated, purely to name the member's type */    \
		struct alloy_ui_desc *nc_ = (struct alloy_ui_desc *)0; \
		__typeof__(nc_->member) fn_ = NULL;                    \
                                                                       \
		for (; d_; d_ = d_->parent) {                          \
			if (d_->member) {                              \
				fn_ = d_->member;                      \
				break;                                 \
			}                                              \
		}                                                      \
		fn_;                                                   \
	})

/* tui.c */
void tui_status(struct alloy_ui *ui, const char *fmt, ...)
	__attribute__((format(printf, 2, 3)));
int tui_push(struct alloy_ui *ui, const char *step);
void tui_apply_all(struct alloy_ui *ui);
int tui_save(struct alloy_ui *ui);
void tui_revert(struct alloy_ui *ui);
void tui_mark_dirty(struct alloy_ui *ui);
long tui_now_ms(void);
const struct alloy_ui_screen *tui_screen(const struct alloy_ui *ui);

/* tui_render.c */
void tui_draw(struct alloy_ui *ui);
void tui_render(struct alloy_ui *ui);
void tui_layout(struct alloy_ui *ui, struct tui_rect *out);
size_t tui_pane_items(struct alloy_ui *ui, const struct alloy_ui_pane *pane,
		      struct alloy_ui_item *out, size_t max);
int tui_pane_slots(struct alloy_ui *ui, const struct alloy_ui_item *items,
		   size_t count, struct tui_slot *out, int max);
int tui_pane_slot_count(struct alloy_ui *ui, int pane);
int tui_item_rows(const struct alloy_ui_item *it);
void tui_draw_pane_box(int y, int x, int h, int w, const char *title,
		       int focused);
int tui_style_attr(enum alloy_ui_style style);

/* tui_canvas.c */
struct alloy_ui_canvas *tui_canvas_bind(int y, int x, int h, int w);
extern const struct alloy_ui_host tui_ui_host;

/* tui_input.c */
void tui_handle_key(struct alloy_ui *ui, int ch);
int tui_translate_key(int ch);
void tui_item_adjust(struct alloy_ui *ui, const struct alloy_ui_item *it,
		     int sub, int dir, int big);
void tui_item_activate(struct alloy_ui *ui, const struct alloy_ui_item *it,
		       int sub);

/* tui_modal.c */
void tui_modal_message(struct alloy_ui *ui, const char *title,
		       const char *text);
int tui_modal_menu(struct alloy_ui *ui, const char *title,
		   const char *const *items, int count, int cur);
int tui_modal_capture_key(struct alloy_ui *ui, const char *title,
			  const char *prompt);
int tui_modal_run(struct alloy_ui *ui, const struct alloy_ui_modal *m);
void tui_modal_confirm_quit(struct alloy_ui *ui);
void tui_modal_frame(int h, int w, int *py, int *px, const char *title);

/* tui_colorpicker.c */
#define TUI_PALETTE_SIZE 16
extern const struct alloy_rgb tui_palette[TUI_PALETTE_SIZE];
short tui_rgb_to_color(const struct alloy_rgb *c);
int tui_pick_color(struct alloy_ui *ui, const struct alloy_ui_color_req *req);
int tui_prompt_hex(struct alloy_ui *ui, struct alloy_rgb *rgb);
int tui_hex_digit(int ch);
int tui_parse_hex_color(char *buf, size_t len, struct alloy_rgb *rgb);

static inline int tui_rgb_attr(const struct alloy_rgb *c)
{
	if (COLORS >= 256) {
		short color_idx = tui_rgb_to_color(c);

		return COLOR_PAIR(CLR_RGB_CUBE_BASE + (color_idx - 16));
	}
	return COLOR_PAIR(CLR_RGB_FALLBACK);
}

/* tui_art.c */
int tui_art_has_markup(const char *art);
void tui_art_measure(const char *art, int *lines, int *width);
void tui_art_draw(struct alloy_ui *ui, const char *art, int y, int x, int max_y,
		  int max_x);

#endif /* ALLOY_TUI_INTERNAL_H */
