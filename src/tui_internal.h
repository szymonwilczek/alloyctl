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
	PANE_LEVELS,
	PANE_TUNING,
	PANE_FOOTER,
	PANE_COUNT,
};

/* Top-level screens; ILLUMINATION button switches between them */
enum tui_view {
	VIEW_MAIN,
	VIEW_ILLUM,
};

/* Panes of the illumination view: EFFECTS (1/3) and the preview (2/3) */
enum tui_illum_focus {
	ILLUM_FOCUS_EFFECTS,
	ILLUM_FOCUS_PREVIEW,
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
	CLR_INFO, /* static native tint for art guide chars ($i) */
	CLR_ZONE_BASE, /* CLR_ZONE_BASE + zone index, dynamic RGB */
	CLR_PICKER_PREVIEW = CLR_ZONE_BASE + ALLOY_MAX_LED_ZONES,
	CLR_PICKER_SWATCH, /* + swatch index */
};

struct tui {
	struct alloy_device *dev;
	const struct alloy_driver *drv;

	struct alloy_config cfg; /* working configuration */
	struct alloy_config baseline; /* REVERT target */

	int live_preview;
	int dirty; /* differs from what SAVE last wrote */
	int accel_running; /* host-side accel engine active for this device */

	enum tui_pane focus;
	int cursor[PANE_COUNT]; /* per-pane selected item */

	enum tui_view view;
	enum tui_illum_focus illum_focus;
	int illum_zone; /* zone the EFFECTS pane edits */
	int illum_tab; /* zone tab cursor in the preview pane */
	int illum_cursor; /* selected item in the EFFECTS pane */
	int illum_swatch; /* palette cursor in the COLORS section */
	const char *illum_hexbuf; /* non-NULL while typing a hex color */

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
int tui_save(struct tui *t);
void tui_revert(struct tui *t);
void tui_accel_changed(struct tui *t);
void tui_accel_set_enabled(struct tui *t, int on);
int tui_pane_item_count(const struct tui *t, enum tui_pane pane);
int tui_dpi_preset_limit(const struct tui *t);
int tui_fx_ignores_color(const struct alloy_driver *drv, uint8_t fx);
void tui_lighting_changed(struct tui *t);

/* tui_panes.c */
void tui_draw(struct tui *t);
void tui_zone_color_pairs(const struct tui *t);
void tui_draw_pane_box(int y, int x, int h, int w, const char *title,
		       int focused);

/* tui_modal.c */
void tui_modal_message(const char *title, const char *text);
void tui_modal_confirm_quit(struct tui *t);
void tui_modal_remap(struct tui *t, int button);
void tui_modal_frame(int h, int w, int *py, int *px, const char *title);

/* tui_colorpicker.c */
#define TUI_PALETTE_SIZE 16
extern const struct alloy_rgb tui_palette[TUI_PALETTE_SIZE];
short tui_rgb_to_color(const struct alloy_rgb *c);
int tui_hex_digit(int ch);
int tui_parse_hex_color(char *buf, size_t len, struct alloy_rgb *rgb);
void tui_modal_color_reactive(struct tui *t);

/* tui_input.c */
void tui_handle_key(struct tui *t, int ch);

/* tui_art.c */
int tui_art_has_markup(const char *art);
void tui_art_measure(const char *art, int *lines, int *width);
void tui_art_draw(const struct tui *t, const char *art, int y, int x, int max_y,
		  int hl_zone);

/* tui_illum.c */
#define TUI_ILLUM_FRAME_MS 100 /* preview animation tick */
long tui_now_ms(void);
void tui_zone_fx_pairs(const struct tui *t, long ms);
void tui_illum_draw(struct tui *t);
void tui_illum_handle_key(struct tui *t, int ch);
void tui_illum_enter(struct tui *t);

#endif /* ALLOY_TUI_INTERNAL_H */
