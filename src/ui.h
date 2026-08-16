/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Front-end ABI.
 *
 * Front-end is a widget engine. It lays panes out on a screen, draws and drives
 * a handful of generic controls, runs modal dialogs and paints ASCII art whose
 * colors somebody else chooses. It has no idea what any of it means.
 *
 * A driver points struct alloy_driver.ui at a struct alloy_ui_desc naming its
 * screens and panes, and fills the item list of each pane on demand.
 * Items carry the callbacks that read and write the driver's configuration,
 * so every semantic decision - what a control does, what it is called,
 * when it is greyed out, what pushing it means - stays in driver code.
 *
 * Descriptions can inherit:
 * set @parent and the front-end falls back to it for any hook this description
 * leaves NULL and for any pane its items() declines, which is how a driver
 * reuses a shared front-end and adds one pane of its own.
 *
 * Front-end registers itself once through alloy_ui_host_register(),
 * so this header never depends on curses and driver code links without it.
 */
#ifndef ALLOY_UI_H
#define ALLOY_UI_H

#include "alloy.h"

struct alloy_ui; /* opaque front-end instance */
struct alloy_ui_canvas; /* opaque drawing surface */
struct alloy_config;
struct alloy_device;
struct alloy_driver;
struct alloy_ui_item;
struct alloy_ui_pane;
struct alloy_ui_modal;
struct alloy_ui_color_req;

/* Static sizing of the per-frame scratch the front-end hands to item builders */
#define ALLOY_UI_MAX_ITEMS 64
#define ALLOY_UI_MAX_PANES 8
#define ALLOY_UI_MAX_VARS 8

/* Number of art paint groups the "$N" markup can name */
#define ALLOY_UI_ART_GROUPS 8

/*
 * Keys handed to driver-owned handlers.
 * Positive values are printable characters,
 * negatives are the named keys
 * and zero is an idle animation tick.
 */
#define ALLOY_UI_KEY_NONE 0
#define ALLOY_UI_KEY_ENTER (-1)
#define ALLOY_UI_KEY_ESC (-2)
#define ALLOY_UI_KEY_UP (-3)
#define ALLOY_UI_KEY_DOWN (-4)
#define ALLOY_UI_KEY_LEFT (-5)
#define ALLOY_UI_KEY_RIGHT (-6)
#define ALLOY_UI_KEY_BACKSPACE (-7)
#define ALLOY_UI_KEY_TAB (-8)

/* Semantic paint styles; the front-end owns the actual palette */
enum alloy_ui_style {
	ALLOY_UI_ST_NORMAL = 0,
	ALLOY_UI_ST_DIM, /* de-emphasized / disabled */
	ALLOY_UI_ST_FRAME,
	ALLOY_UI_ST_TITLE,
	ALLOY_UI_ST_ACCENT, /* value text */
	ALLOY_UI_ST_SELECTED, /* cursor highlight */
	ALLOY_UI_ST_BUTTON,
	ALLOY_UI_ST_HOT, /* active / engaged */
	ALLOY_UI_ST_GOOD,
	ALLOY_UI_ST_WARN,
	ALLOY_UI_ST_BAD,
	ALLOY_UI_ST_INFO,
};

/* Line-drawing and marker glyphs available on the canvas */
enum alloy_ui_glyph {
	ALLOY_UI_G_HLINE = 0,
	ALLOY_UI_G_VLINE,
	ALLOY_UI_G_ULCORNER,
	ALLOY_UI_G_URCORNER,
	ALLOY_UI_G_LLCORNER,
	ALLOY_UI_G_LRCORNER,
	ALLOY_UI_G_LTEE,
	ALLOY_UI_G_RTEE,
	ALLOY_UI_G_DIAMOND,
	ALLOY_UI_G_BULLET,
	ALLOY_UI_G_CKBOARD,
	ALLOY_UI_G_BLOCK,
	ALLOY_UI_G_SHADE,
	ALLOY_UI_G_LARROW,
	ALLOY_UI_G_RARROW,
	ALLOY_UI_G_UARROW,
	ALLOY_UI_G_DARROW,
};

/* Generic widgets the front-end can draw and drive */
enum alloy_ui_kind {
	ALLOY_UI_HEADING = 0, /* section caption, not selectable */
	ALLOY_UI_SPACER, /* blank rows, not selectable */
	ALLOY_UI_TEXT, /* label plus text(), not selectable */
	ALLOY_UI_TOGGLE, /* on/off */
	ALLOY_UI_STEPPER, /* numeric, "< value unit >" */
	ALLOY_UI_SLIDER, /* numeric, value row plus a bar */
	ALLOY_UI_GAUGE, /* numeric, value row plus a step ladder */
	ALLOY_UI_CHOICE, /* one of choices[] */
	ALLOY_UI_COLOR, /* R/G/B, palette and hex block */
	ALLOY_UI_BUTTON, /* activate only */
	ALLOY_UI_CUSTOM, /* driver paints it through draw() */
};

/* item flags */
#define ALLOY_UI_F_DISABLED (1u << 0) /* dimmed, adjustments ignored */
#define ALLOY_UI_F_HOT (1u << 1) /* value drawn as engaged */
#define ALLOY_UI_F_STATIC (1u << 2) /* never takes the cursor */
#define ALLOY_UI_F_NOBAR (1u << 3) /* slider/gauge without its bar */
/* set by the front-end on the copy handed to draw(): this row holds the cursor */
#define ALLOY_UI_F_FOCUSED (1u << 4)

/*
 * One control.
 *
 * Item lists are rebuilt every frame by the driver's items() callback,
 * so item is a short-lived description, not storage:
 * all state lives in the driver's configuration and is reached through
 * the callbacks.
 * @id and @idx are opaque to the front-end and come back unchanged in every
 * callback, so one callback can serve a whole family of rows.
 */
struct alloy_ui_item {
	const char *label;
	enum alloy_ui_kind kind;
	uint32_t id; /* driver-defined tag */
	int idx; /* driver-defined instance index */
	uint32_t flags;

	int min_val;
	int max_val;
	int step; /* h/l increment (default 1) */
	int big_step; /* H/L increment (default step) */
	const char *unit; /* e.g. "%", " Hz", " deg" */

	const char *const *choices;
	uint16_t num_choices;

	uint8_t rows; /* height override; required for CUSTOM/SPACER */

	int (*get)(struct alloy_ui *ui, const struct alloy_ui_item *it);
	void (*set)(struct alloy_ui *ui, const struct alloy_ui_item *it,
		    int val);
	/* editable color for ALLOY_UI_COLOR */
	struct alloy_rgb *(*color)(struct alloy_ui *ui,
				   const struct alloy_ui_item *it);
	/* value text for TEXT, or a value override for any valued widget */
	void (*text)(struct alloy_ui *ui, const struct alloy_ui_item *it,
		     char *buf, size_t len);
	/* enter on the row */
	void (*activate)(struct alloy_ui *ui, const struct alloy_ui_item *it);
	/* after the front-end changed the value or the color */
	void (*changed)(struct alloy_ui *ui, const struct alloy_ui_item *it);
	/* ALLOY_UI_CUSTOM painter; the canvas is clipped to the item's rows */
	void (*draw)(struct alloy_ui *ui, const struct alloy_ui_item *it,
		     struct alloy_ui_canvas *c);
};

/* pane flags */
#define ALLOY_UI_PANE_ART (1u << 0) /* paint the device art in this pane */
#define ALLOY_UI_PANE_TABS (1u << 1) /* items become a horizontal tab strip */

/*
 * One framed box.
 *
 * Panes are placed in columns, left to right:
 * @col groups them and @width_pct sizes the column (the single column left
 * at 0 soaks up the remaining width).
 * Inside a column panes stack top to bottom with @height_rows or @height_pct,
 * and the one left at 0 takes what is left.
 */
struct alloy_ui_pane {
	const char *title;
	uint32_t id; /* driver-defined tag, passed to items() */
	uint8_t col;
	uint8_t width_pct;
	uint8_t height_pct;
	uint8_t height_rows; /* exact height, overriding height_pct */
	uint8_t min_width; /* clamps for width_pct */
	uint8_t max_width;
	uint32_t flags;
	const char *hint; /* dim help line at the pane's bottom */

	/* optional: hide the pane; it then takes no space and never gets focus */
	int (*visible)(struct alloy_ui *ui, const struct alloy_ui_pane *pane);
	/* optional: title computed at runtime, overriding @title */
	const char *(*dyn_title)(struct alloy_ui *ui,
				 const struct alloy_ui_pane *pane);
};

/* screen flags */
#define ALLOY_UI_SCREEN_NOFOOTER (1u << 0)

struct alloy_ui_screen {
	const char *name;
	uint32_t id;
	const struct alloy_ui_pane *panes;
	uint8_t num_panes;
	uint32_t flags;
	const char *hint; /* status-bar key legend */
};

/*
 * Driver-owned modal.
 * Front-end draws the frame and runs the key loop;
 * driver paints the body and decides when it is done.
 */
struct alloy_ui_modal {
	const char *title;
	int h;
	int w;
	void (*draw)(struct alloy_ui *ui, struct alloy_ui_canvas *c,
		     void *data);
	/* nonzero closes the modal; the value is returned by the runner */
	int (*key)(struct alloy_ui *ui, int key, void *data);
	void *data;
};

/*
 * Color picker request.
 * Picker edits @rgb in place and calls @changed after every keystroke,
 * so the driver can live-push the color while it is being dialed in.
 * @modes, when given, adds a mode row above the channels.
 */
struct alloy_ui_color_req {
	const char *title;
	struct alloy_rgb *rgb;
	const char *const *modes;
	int num_modes;
	uint8_t *mode;
	/* nonzero for mode values that grey the channel rows out */
	int (*mode_greys)(struct alloy_ui *ui, uint8_t mode);
	void (*changed)(struct alloy_ui *ui, void *data);
	void *data;
};

/*
 * Everything the front-end asks of the driver.
 * Only items() and the screen table are mandatory;
 * anything left NULL falls through to @parent, and then to nothing.
 */
struct alloy_ui_desc {
	const struct alloy_ui_desc *parent;

	const struct alloy_ui_screen *screens;
	uint8_t num_screens;

	/*
	 * Fill @out with the pane's controls; returns how many were written.
	 * Returning 0 for a pane this description does not recognize lets
	 * the parent description answer instead.
	 */
	size_t (*items)(struct alloy_ui *ui, const struct alloy_ui_pane *pane,
			struct alloy_ui_item *out, size_t max);

	/* once, before the first frame */
	void (*enter)(struct alloy_ui *ui);
	/* on every screen switch, with the screen being entered */
	void (*screen_changed)(struct alloy_ui *ui, uint32_t screen_id);

	/*
	 * Color of one art cell.
	 * @group is the "$N" marker index (0-based) or -1 for unmarked art;
	 * @row and @col are the cell's position in the art.
	 * Returns nonzero when @out was filled, zero to leave the cell plain.
	 */
	int (*art_cell)(struct alloy_ui *ui, int group, int row, int col,
			long ms, struct alloy_rgb *out);

	/* periodic work, on every frame */
	void (*tick)(struct alloy_ui *ui, long ms);
	/*
	 * Gate for the one-shot startup handshake:
	 * the front-end waits with the firmware read and the initial push until
	 * this returns nonzero.
	 * NULL means "always reachable".
	 */
	int (*ready)(struct alloy_ui *ui);

	/* unclaimed keypress; return nonzero when handled */
	int (*key)(struct alloy_ui *ui, int key);
	/* a device-initiated change was parsed into the working configuration */
	void (*event)(struct alloy_ui *ui, struct alloy_config *baseline);
	/* after a successful save / after a revert */
	void (*saved)(struct alloy_ui *ui);
	void (*reverted)(struct alloy_ui *ui);
};

struct alloy_config *alloy_ui_config(struct alloy_ui *ui);
const struct alloy_driver *alloy_ui_driver(struct alloy_ui *ui);
struct alloy_device *alloy_ui_device(struct alloy_ui *ui);
int alloy_ui_live_preview(struct alloy_ui *ui);
long alloy_ui_now_ms(void);

void alloy_ui_status(struct alloy_ui *ui, const char *fmt, ...)
	__attribute__((format(printf, 2, 3)));
void alloy_ui_mark_dirty(struct alloy_ui *ui);

/*
 * Record an edit:
 * marks the working configuration dirty and, while live preview is on,
 * pushes the driver's apply step named @step.
 * Pass NULL for host-only state that never reaches the device.
 */
void alloy_ui_changed(struct alloy_ui *ui, const char *step);
/* Push a step right now, regardless of live preview and without marking dirty */
int alloy_ui_push(struct alloy_ui *ui, const char *step);

/* Scratch integers the front-end stores per instance and never interprets */
int alloy_ui_var(struct alloy_ui *ui, int slot);
void alloy_ui_set_var(struct alloy_ui *ui, int slot, int val);

void alloy_ui_goto_screen(struct alloy_ui *ui, uint32_t screen_id);
uint32_t alloy_ui_screen(struct alloy_ui *ui);

/* Modals */
void alloy_ui_message(struct alloy_ui *ui, const char *title, const char *text);
int alloy_ui_menu(struct alloy_ui *ui, const char *title,
		  const char *const *items, int count, int cur);
int alloy_ui_pick_color(struct alloy_ui *ui,
			const struct alloy_ui_color_req *req);
int alloy_ui_prompt_hex(struct alloy_ui *ui, struct alloy_rgb *rgb);
int alloy_ui_capture_key(struct alloy_ui *ui, const char *title,
			 const char *prompt);
int alloy_ui_run_modal(struct alloy_ui *ui, const struct alloy_ui_modal *m);

/* Canvas primitives, for ALLOY_UI_CUSTOM items and driver-owned modals */
int alloy_ui_canvas_h(struct alloy_ui_canvas *c);
int alloy_ui_canvas_w(struct alloy_ui_canvas *c);
void alloy_ui_text(struct alloy_ui_canvas *c, int y, int x,
		   enum alloy_ui_style style, const char *fmt, ...)
	__attribute__((format(printf, 5, 6)));
void alloy_ui_glyph(struct alloy_ui_canvas *c, int y, int x,
		    enum alloy_ui_glyph g, enum alloy_ui_style style);
void alloy_ui_hline(struct alloy_ui_canvas *c, int y, int x, int len,
		    enum alloy_ui_glyph g, enum alloy_ui_style style);
void alloy_ui_vline(struct alloy_ui_canvas *c, int y, int x, int len,
		    enum alloy_ui_glyph g, enum alloy_ui_style style);
/* one character in a true RGB tint, mapped onto whatever the terminal has */
void alloy_ui_cell(struct alloy_ui_canvas *c, int y, int x, char ch,
		   const struct alloy_rgb *color);

struct alloy_ui_host {
	struct alloy_config *(*config)(struct alloy_ui *ui);
	const struct alloy_driver *(*driver)(struct alloy_ui *ui);
	struct alloy_device *(*device)(struct alloy_ui *ui);
	int (*live_preview)(struct alloy_ui *ui);
	long (*now_ms)(void);

	void (*status)(struct alloy_ui *ui, const char *msg);
	void (*mark_dirty)(struct alloy_ui *ui);
	void (*changed)(struct alloy_ui *ui, const char *step);
	int (*push)(struct alloy_ui *ui, const char *step);

	int (*var)(struct alloy_ui *ui, int slot);
	void (*set_var)(struct alloy_ui *ui, int slot, int val);
	void (*goto_screen)(struct alloy_ui *ui, uint32_t screen_id);
	uint32_t (*screen)(struct alloy_ui *ui);

	void (*message)(struct alloy_ui *ui, const char *title,
			const char *text);
	int (*menu)(struct alloy_ui *ui, const char *title,
		    const char *const *items, int count, int cur);
	int (*pick_color)(struct alloy_ui *ui,
			  const struct alloy_ui_color_req *req);
	int (*prompt_hex)(struct alloy_ui *ui, struct alloy_rgb *rgb);
	int (*capture_key)(struct alloy_ui *ui, const char *title,
			   const char *prompt);
	int (*run_modal)(struct alloy_ui *ui, const struct alloy_ui_modal *m);

	int (*canvas_h)(struct alloy_ui_canvas *c);
	int (*canvas_w)(struct alloy_ui_canvas *c);
	void (*canvas_text)(struct alloy_ui_canvas *c, int y, int x,
			    enum alloy_ui_style style, const char *s);
	void (*canvas_glyph)(struct alloy_ui_canvas *c, int y, int x,
			     enum alloy_ui_glyph g, enum alloy_ui_style style);
	void (*canvas_cell)(struct alloy_ui_canvas *c, int y, int x, char ch,
			    const struct alloy_rgb *color);
};

void alloy_ui_host_register(const struct alloy_ui_host *host);

#endif /* ALLOY_UI_H */
