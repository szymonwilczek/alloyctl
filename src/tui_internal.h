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
	/*
	 * Wireless power controls, carved off the bottom of the CPI LEVELS column.
	 * Present only for drivers with ALLOY_CAP_BATTERY, otherwise it holds
	 * no items and pane navigation skips over it.
	 */
	PANE_POWER,
	PANE_TUNING,
	PANE_FOOTER,
	PANE_COUNT,
};

/*
 * Items in the POWER pane, top to bottom.
 * SLEEP/SMART/DIM come with ALLOY_CAP_BATTERY;
 * HIGHEFF is last and present only with ALLOY_CAP_HIGH_EFFICIENCY,
 * so its absence never shifts the other indices (see tui_pane_item_count).
 */
enum tui_power_item {
	POWER_SLEEP, /* Battery Saver: inactivity sleep timer stepper */
	POWER_SMART, /* Smart Illum: blank LEDs while moving, toggle */
	POWER_DIM, /* Dim Timer: dim LEDs after N s idle, stepper */
	POWER_HIGHEFF, /* High-Efficiency Mode toggle */
	POWER_COUNT,
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
	/* wireless DEVICE section: battery bands (full -> empty) and link logos */
	CLR_BAT_HIGH, /* green: plenty */
	CLR_BAT_GOOD, /* white: comfortable */
	CLR_BAT_MID, /* yellow: getting low */
	CLR_BAT_LOW, /* red: nearly empty */
	CLR_LINK_BT, /* blue: Bluetooth connected */
	CLR_LINK_RF, /* white: 2.4 GHz connected */
	CLR_LINK_OFF, /* dim: link inactive */
	CLR_ZONE_BASE, /* CLR_ZONE_BASE + zone index, dynamic RGB */
	CLR_PICKER_PREVIEW = CLR_ZONE_BASE + ALLOY_MAX_LED_ZONES,
	CLR_PICKER_SWATCH, /* + swatch index */
};

/* consecutive idle battery polls tolerated before the gauge blanks to "--" */
#define TUI_BATTERY_MAX_MISSES 3

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

	/*
	 * Wireless battery gauge (ALLOY_CAP_BATTERY):
	 * battery_pct < 0 means no reading (mouse asleep or unlinked).
	 * Refreshed on slow cadence keyed off battery_next_ms.
	 */
	int battery_pct;
	int battery_charging;
	int battery_misses; /* consecutive failed polls; blanks the gauge past a threshold */
	int bt_present; /* mouse currently paired to the host over Bluetooth */
	long battery_next_ms;

	/*
	 * One-shot device handshake (firmware read + initial config push) done lazily
	 * once a mouse is actually reachable, so bare 2.4 GHz receiver does not stall
	 * startup on the per-command wake-retry budget.
	 */
	int device_synced;

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
void tui_poll_battery(struct tui *t);
int tui_device_needs_pairing(const struct tui *t);
int tui_pane_item_count(const struct tui *t, enum tui_pane pane);
int tui_dpi_preset_limit(const struct tui *t);
int tui_fx_ignores_color(const struct alloy_driver *drv, uint8_t fx);
void tui_lighting_changed(struct tui *t);

/* tui_panes.c */
void tui_draw(struct tui *t);
void tui_render(struct tui *t);
void tui_zone_color_pairs(const struct tui *t);
void tui_draw_pane_box(int y, int x, int h, int w, const char *title,
		       int focused);

/* tui_modal.c */
void tui_modal_message(const char *title, const char *text);
void tui_modal_confirm_quit(struct tui *t);
void tui_modal_remap(struct tui *t, int button);
void tui_modal_pair(struct tui *t);
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
void tui_illum_render(struct tui *t);
void tui_illum_handle_key(struct tui *t, int ch);
void tui_illum_enter(struct tui *t);
void tui_fx_global_normalize(struct tui *t, struct alloy_config *cfg);

#endif /* ALLOY_TUI_INTERNAL_H */
