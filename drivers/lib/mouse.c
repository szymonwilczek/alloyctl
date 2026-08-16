/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Shared front-end for pointing devices.
 *
 * Driver-side code: this is where "a mouse has CPI levels, remappable buttons,
 * a pointer transform, and - if it is wireless - a battery and power knobs" is
 * written down.
 *
 * Front-end learns all of it from the item lists below and from the canvas
 * painters that draw the response curve, the snapping trace and the battery gauge.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "hid.h"
#include "lib/accel.h"
#include "lib/light.h"
#include "lib/mouse.h"
#include "lib/widgets.h"
#include "state.h"

/* pane ids */
enum {
	MP_ACTIONS = 1,
	MP_MACRO,
	MP_DEVICE,
	MP_CENTER,
	MP_LEVELS,
	MP_POWER,
	MP_TUNING,
};

/* front-end scratch slot: is the host-side transform daemon running? */
#define MOUSE_VAR_ENGINE 1

/* consecutive idle battery polls tolerated before the gauge blanks to "--" */
#define MOUSE_BATTERY_MAX_MISSES 3
#define MOUSE_BATTERY_PERIOD_MS 8000

/*
 * Wireless link state.
 * One device is configured per process, so a single instance is enough;
 * enter() resets it so a fresh run never inherits a stale reading.
 */
static struct {
	int battery_pct;
	int charging;
	int misses;
	int bt_present;
	long next_poll_ms;
} link = { .battery_pct = -1 };

static int has_battery(const struct alloy_driver *drv)
{
	return (alloy_devinfo(drv)->caps & ALLOY_CAP_BATTERY) != 0;
}

static int pane_wireless(struct alloy_ui *ui, const struct alloy_ui_pane *pane)
{
	(void)pane;
	return has_battery(alloy_ui_driver(ui));
}

/*
 * Whether to offer the PAIR button:
 * Driver can bind a mouse to its receiver and nothing is currently linked
 * (no 2.4 GHz battery reading, not on Bluetooth).
 * This is a proxy - a paired-but-powered-off mouse looks the same as unpaired
 * one from the host - and stands in until a real "is a mouse bound?" query
 * is reverse-engineered from the receiver.
 */
static int needs_pairing(struct alloy_ui *ui)
{
	const struct alloy_driver *drv = alloy_ui_driver(ui);

	return (alloy_devinfo(drv)->caps & ALLOY_CAP_PAIRING) &&
	       alloy_mouse_info(drv)->pair && link.battery_pct < 0 &&
	       !link.bt_present;
}

static const char *action_label(const struct alloy_action *act, char *buf,
				size_t len)
{
	switch (act->type) {
	case ALLOY_ACT_MOUSE:
		snprintf(buf, len, "Button %u", act->value);
		break;
	case ALLOY_ACT_DPI_CYCLE:
		snprintf(buf, len, "CPI Toggle");
		break;
	case ALLOY_ACT_SCROLL_UP:
		snprintf(buf, len, "Scroll Up");
		break;
	case ALLOY_ACT_SCROLL_DOWN:
		snprintf(buf, len, "Scroll Down");
		break;
	case ALLOY_ACT_KEYBOARD:
		snprintf(buf, len, "Key 0x%02X", act->value);
		break;
	case ALLOY_ACT_MEDIA:
		snprintf(buf, len, "Media 0x%02X", act->value);
		break;
	case ALLOY_ACT_DISABLED:
	default:
		snprintf(buf, len, "Disabled");
		break;
	}
	return buf;
}

static void button_draw(struct alloy_ui *ui, const struct alloy_ui_item *it,
			struct alloy_ui_canvas *c)
{
	const struct alloy_driver *drv = alloy_ui_driver(ui);
	char label[32];

	alloy_ui_text(c, 0, 0,
		      (it->flags & ALLOY_UI_F_FOCUSED) ? ALLOY_UI_ST_SELECTED :
							 ALLOY_UI_ST_NORMAL,
		      "%.*s", alloy_ui_canvas_w(c) - 1,
		      alloy_mouse_info(drv)->buttons[it->idx].name);
	alloy_ui_text(c, 1, 2, ALLOY_UI_ST_ACCENT, "-> %s",
		      action_label(&(*alloy_mouse_cfg(alloy_ui_config(ui)))
					    .buttons[it->idx],
				   label, sizeof(label)));
}

/* selectable actions offered by the remap dialog */
static const struct {
	const char *label;
	struct alloy_action action;
	int capture_key; /* prompt for a keyboard key afterwards */
} remap_choices[] = {
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
 * Minimal ASCII to USB HID keyboard usage translation for the "Keyboard Key..."
 * capture.
 * Covers letters, digits and a few essentials; anything else is rejected.
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
	case ALLOY_UI_KEY_ENTER:
		return 0x28;
	case ALLOY_UI_KEY_TAB:
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

static void button_activate(struct alloy_ui *ui, const struct alloy_ui_item *it)
{
	const struct alloy_driver *drv = alloy_ui_driver(ui);
	const char *labels[ALLOY_ARRAY_SIZE(remap_choices)];
	struct alloy_action action;
	size_t i;
	int sel;

	for (i = 0; i < ALLOY_ARRAY_SIZE(remap_choices); i++)
		labels[i] = remap_choices[i].label;

	sel = alloy_ui_menu(ui, alloy_mouse_info(drv)->buttons[it->idx].name,
			    labels, (int)ALLOY_ARRAY_SIZE(remap_choices), 0);
	if (sel < 0)
		return;

	action = remap_choices[sel].action;
	if (remap_choices[sel].capture_key) {
		int key = alloy_ui_capture_key(
			ui, "PRESS A KEY", "press the key to bind (esc: back)");
		int usage = ascii_to_hid(key);

		if (key == ALLOY_UI_KEY_ESC)
			return;
		if (usage < 0) {
			alloy_ui_status(ui, "unsupported key for binding");
			return;
		}
		action.type = ALLOY_ACT_KEYBOARD;
		action.value = (uint16_t)usage;
	}

	(*alloy_mouse_cfg(alloy_ui_config(ui))).buttons[it->idx] = action;
	alloy_ui_changed(ui, ALLOY_STEP_BUTTONS);
}

static void macro_activate(struct alloy_ui *ui, const struct alloy_ui_item *it)
{
	(void)it;
	alloy_ui_message(ui, "MACRO EDITOR", "TBA");
}

/*
 * 2.4 GHz and Bluetooth link logos (lit in their own color when connected,
 * dimmed otherwise) and a drawn battery whose fill is banded
 * green -> white -> yellow -> red as it drains.
 */
static void device_draw(struct alloy_ui *ui, const struct alloy_ui_item *it,
			struct alloy_ui_canvas *c)
{
	int rf_on = link.battery_pct >= 0; /* receiver has a linked mouse */
	const int cap = 10;
	int fill;
	int band;
	int i;

	(void)it;
	alloy_ui_text(c, 0, 1, rf_on ? ALLOY_UI_ST_HOT : ALLOY_UI_ST_DIM,
		      "((o)) 2.4G");
	alloy_ui_text(c, 0, 14,
		      link.bt_present ? ALLOY_UI_ST_INFO : ALLOY_UI_ST_DIM,
		      ">B< BT");

	if (!rf_on) {
		/*
		 * No mouse on the 2.4 GHz link.
		 * If the receiver can bind one, point at the PAIR shortcut;
		 * otherwise just note that the battery is only readable over
		 * the 2.4 GHz link.
		 */
		if (needs_pairing(ui)) {
			alloy_ui_text(c, 2, 1, ALLOY_UI_ST_BUTTON,
				      " [p] PAIR ");
			alloy_ui_text(c, 2, 12, ALLOY_UI_ST_DIM,
				      "no mouse paired");
			return;
		}
		alloy_ui_text(c, 2, 1, ALLOY_UI_ST_DIM, "%s",
			      link.bt_present ? "battery  -- (on Bluetooth)" :
						"battery  -- (no 2.4G link)");
		return;
	}

	if (link.battery_pct > 75)
		band = ALLOY_UI_ST_GOOD;
	else if (link.battery_pct > 50)
		band = ALLOY_UI_ST_NORMAL;
	else if (link.battery_pct > 25)
		band = ALLOY_UI_ST_WARN;
	else
		band = ALLOY_UI_ST_BAD;

	fill = (link.battery_pct * cap + 50) / 100;

	alloy_ui_text(c, 2, 1, ALLOY_UI_ST_FRAME, "[");
	for (i = 0; i < cap; i++)
		alloy_ui_glyph(c, 2, 2 + i,
			       i < fill ? ALLOY_UI_G_BLOCK : ALLOY_UI_G_SHADE,
			       i < fill ? (enum alloy_ui_style)band :
					  ALLOY_UI_ST_DIM);
	alloy_ui_text(c, 2, 2 + cap, ALLOY_UI_ST_FRAME, "]");
	alloy_ui_text(c, 2, 4 + cap, (enum alloy_ui_style)band, "%d%%%s",
		      link.battery_pct, link.charging ? " CHG" : "");
}

/*
 * Dongle pairing wizard, modeled on the GG "connect a new device" flow.
 *
 *   1. PROBE     live-check that the 2.4 GHz receiver is plugged in;
 *                it has to be (pairing runs through it), and it is re-probed
 *                every frame so pulling the dongle mid-flow shows up at once.
 *   2. INSTRUCT  walk through the mouse-side gesture and, on enter, kick off
 *                pairing via ops->pair.
 *
 * Success there means the write went out, not that a mouse bound - the link
 * coming up afterwards is the real confirmation.
 * A driver whose bind opcode is still unmapped reports ALLOY_PAIR_UNIMPLEMENTED,
 * which is surfaced honestly.
 */
struct pair_wizard {
	int stage; /* 0: probe, 1: instruct */
	int dongle;
};

static void pair_draw(struct alloy_ui *ui, struct alloy_ui_canvas *c,
		      void *data)
{
	const struct alloy_driver *drv = alloy_ui_driver(ui);
	struct pair_wizard *w = data;

	w->dongle = alloy_driver_present(drv);

	if (!w->stage) {
		alloy_ui_text(c, 1, 2, ALLOY_UI_ST_NORMAL,
			      "Step 1 of 2 - wireless receiver");
		if (w->dongle) {
			alloy_ui_text(c, 3, 2, ALLOY_UI_ST_HOT,
				      "receiver detected");
			alloy_ui_text(c, 5, 2, ALLOY_UI_ST_NORMAL,
				      "Keep the 2.4 GHz dongle plugged in.");
		} else {
			alloy_ui_text(c, 3, 2, ALLOY_UI_ST_BAD,
				      "receiver NOT found");
			alloy_ui_text(
				c, 5, 2, ALLOY_UI_ST_NORMAL,
				"Plug the 2.4 GHz dongle into a USB port.");
		}
		alloy_ui_text(
			c, 8, 1, ALLOY_UI_ST_DIM, "%s",
			w->dongle ? " enter: next   esc: cancel " :
				    " waiting for receiver...   esc: cancel ");
		return;
	}

	alloy_ui_text(c, 1, 2, ALLOY_UI_ST_NORMAL,
		      "Step 2 of 2 - put the mouse in pairing mode");
	alloy_ui_text(c, 3, 2, ALLOY_UI_ST_NORMAL,
		      "1. Slide the mouse power switch to OFF.");
	alloy_ui_text(c, 4, 2, ALLOY_UI_ST_NORMAL,
		      "2. Press and hold the CPI button.");
	alloy_ui_text(c, 5, 2, ALLOY_UI_ST_NORMAL,
		      "3. Holding it, slide the switch to 2.4 GHz");
	alloy_ui_text(c, 6, 2, ALLOY_UI_ST_NORMAL,
		      "   (the LEDs blink white).");
	alloy_ui_text(c, 8, 1, ALLOY_UI_ST_DIM,
		      " enter: begin pairing   esc: back ");
}

static int pair_key(struct alloy_ui *ui, int key, void *data)
{
	const struct alloy_driver *drv = alloy_ui_driver(ui);
	struct pair_wizard *w = data;
	int ret;

	switch (key) {
	case ALLOY_UI_KEY_ESC:
		if (w->stage) {
			w->stage = 0;
			return 0;
		}
		return 1;
	case ALLOY_UI_KEY_ENTER:
		if (!w->stage) {
			if (w->dongle)
				w->stage = 1;
			return 0;
		}
		ret = alloy_mouse_info(drv)->pair ?
			      alloy_mouse_info(drv)->pair(alloy_ui_device(ui)) :
			      -1;
		if (ret == ALLOY_PAIR_UNIMPLEMENTED)
			alloy_ui_status(
				ui,
				"pairing: receiver bind command not captured yet");
		else if (ret == 0)
			alloy_ui_status(
				ui,
				"pairing started - waiting for the mouse to link");
		else
			alloy_ui_status(
				ui,
				"pairing: could not start (receiver error)");
		return 1;
	default:
		return 0;
	}
}

static int dpi_limit(const struct alloy_driver *drv)
{
	return ALLOY_MIN(alloy_mouse_info(drv)->dpi.max_presets,
			 ALLOY_MAX_DPI_PRESETS);
}

static int dpi_get(struct alloy_ui *ui, const struct alloy_ui_item *it)
{
	return (*alloy_mouse_cfg(alloy_ui_config(ui))).dpi[it->idx][0];
}

static void dpi_set(struct alloy_ui *ui, const struct alloy_ui_item *it,
		    int val)
{
	const struct alloy_driver *drv = alloy_ui_driver(ui);
	struct alloy_mouse_config *m = &(*alloy_mouse_cfg(alloy_ui_config(ui)));
	int dpi = ALLOY_CLAMP(val, alloy_mouse_info(drv)->dpi.min,
			      alloy_mouse_info(drv)->dpi.max);

	dpi = dpi / alloy_mouse_info(drv)->dpi.step *
	      alloy_mouse_info(drv)->dpi.step;
	m->dpi[it->idx][0] = (uint16_t)dpi;
	m->dpi[it->idx][1] = (uint16_t)dpi;
}

static void dpi_text(struct alloy_ui *ui, const struct alloy_ui_item *it,
		     char *buf, size_t len)
{
	const struct alloy_mouse_config *m =
		&(*alloy_mouse_cfg(alloy_ui_config(ui)));

	snprintf(buf, len, "%u%s", m->dpi[it->idx][0],
		 m->dpi_active == it->idx ? " *" : "");
}

static void dpi_changed(struct alloy_ui *ui, const struct alloy_ui_item *it)
{
	(void)it;
	alloy_ui_changed(ui, ALLOY_STEP_DPI);
}

static void dpi_activate(struct alloy_ui *ui, const struct alloy_ui_item *it)
{
	(*alloy_mouse_cfg(alloy_ui_config(ui))).dpi_active = (uint8_t)it->idx;
	dpi_changed(ui, it);
	alloy_ui_status(ui, "level %d active", it->idx + 1);
}

/*
 * Append a preset seeded with double the last one (clamped and snapped),
 * which reproduces the 800/1600/3200/... ladder the stock software builds.
 */
static void dpi_create(struct alloy_ui *ui, const struct alloy_ui_item *it)
{
	const struct alloy_driver *drv = alloy_ui_driver(ui);
	struct alloy_mouse_config *m = &(*alloy_mouse_cfg(alloy_ui_config(ui)));
	uint8_t n = m->dpi_count;
	int dpi;

	(void)it;
	if (n >= dpi_limit(drv)) {
		alloy_ui_status(ui, "this mouse holds at most %d levels",
				dpi_limit(drv));
		return;
	}

	dpi = m->dpi[n - 1][0] * 2;
	dpi = ALLOY_CLAMP(dpi, alloy_mouse_info(drv)->dpi.min,
			  alloy_mouse_info(drv)->dpi.max);
	dpi = dpi / alloy_mouse_info(drv)->dpi.step *
	      alloy_mouse_info(drv)->dpi.step;
	m->dpi[n][0] = (uint16_t)dpi;
	m->dpi[n][1] = (uint16_t)dpi;
	m->dpi_count = (uint8_t)(n + 1);

	alloy_ui_changed(ui, ALLOY_STEP_DPI);
	alloy_ui_status(ui, "level %u created", n + 1);
}

static int sleep_get(struct alloy_ui *ui, const struct alloy_ui_item *it)
{
	(void)it;
	return alloy_mouse_cfg(alloy_ui_config(ui))->sleep_min;
}

static void sleep_set(struct alloy_ui *ui, const struct alloy_ui_item *it,
		      int val)
{
	(void)it;
	alloy_mouse_cfg(alloy_ui_config(ui))->sleep_min = (uint8_t)val;
}

static void sleep_text(struct alloy_ui *ui, const struct alloy_ui_item *it,
		       char *buf, size_t len)
{
	uint8_t v = alloy_mouse_cfg(alloy_ui_config(ui))->sleep_min;

	(void)it;
	if (v)
		snprintf(buf, len, "%2u min", v);
	else
		snprintf(buf, len, " Off  ");
}

static void sleep_changed(struct alloy_ui *ui, const struct alloy_ui_item *it)
{
	(void)it;
	alloy_ui_changed(ui, ALLOY_STEP_SLEEP);
}

static int smart_get(struct alloy_ui *ui, const struct alloy_ui_item *it)
{
	(void)it;
	return alloy_mouse_cfg(alloy_ui_config(ui))->illum_smart;
}

static void smart_set(struct alloy_ui *ui, const struct alloy_ui_item *it,
		      int val)
{
	(void)it;
	alloy_mouse_cfg(alloy_ui_config(ui))->illum_smart =
		(uint8_t)(val ? 1 : 0);
}

static int dim_get(struct alloy_ui *ui, const struct alloy_ui_item *it)
{
	(void)it;
	return alloy_mouse_cfg(alloy_ui_config(ui))->illum_dim_s;
}

static void dim_set(struct alloy_ui *ui, const struct alloy_ui_item *it,
		    int val)
{
	(void)it;
	alloy_mouse_cfg(alloy_ui_config(ui))->illum_dim_s = (uint16_t)val;
}

static void dim_text(struct alloy_ui *ui, const struct alloy_ui_item *it,
		     char *buf, size_t len)
{
	uint16_t v = alloy_mouse_cfg(alloy_ui_config(ui))->illum_dim_s;

	(void)it;
	if (v)
		snprintf(buf, len, "%4u s", v);
	else
		snprintf(buf, len, " Off  ");
}

/*
 * Smart Illum and the dim timer ride the same illumination command as the brightness,
 * so they are pushed through apply_brightness rather than an op of their own.
 */
static void illum_changed(struct alloy_ui *ui, const struct alloy_ui_item *it)
{
	(void)it;
	alloy_ui_changed(ui, ALLOY_STEP_BRIGHTNESS);
}

static int accel_get(struct alloy_ui *ui, const struct alloy_ui_item *it)
{
	const struct alloy_mouse_config *m =
		&(*alloy_mouse_cfg(alloy_ui_config(ui)));

	switch (it->idx) {
	case 0:
		return m->acceleration;
	case 1:
		return m->deceleration;
	default:
		return m->angle_snapping;
	}
}

static void accel_set(struct alloy_ui *ui, const struct alloy_ui_item *it,
		      int val)
{
	struct alloy_mouse_config *m = &(*alloy_mouse_cfg(alloy_ui_config(ui)));

	switch (it->idx) {
	case 0:
		m->acceleration = (int8_t)val;
		break;
	case 1:
		m->deceleration = (int8_t)val;
		break;
	default:
		m->angle_snapping = (uint8_t)val;
		break;
	}
}

/*
 * Pointer-transform value changed.
 * The transform is a host-side daemon, not a device register, so "applying" it
 * means rewriting the config it watches and poking it to re-read.
 */
static void accel_changed(struct alloy_ui *ui, const struct alloy_ui_item *it)
{
	const struct alloy_driver *drv = alloy_ui_driver(ui);

	(void)it;
	alloy_ui_mark_dirty(ui);
	if (alloy_ui_live_preview(ui) && alloy_ui_var(ui, MOUSE_VAR_ENGINE)) {
		alloy_state_store(drv, alloy_ui_config(ui));
		alloy_accel_reload(drv->vendor_id, drv->product_id);
	}
}

static int engine_get(struct alloy_ui *ui, const struct alloy_ui_item *it)
{
	(void)it;
	return alloy_ui_var(ui, MOUSE_VAR_ENGINE);
}

/*
 * Turn the host-side transform engine on or off.
 * This is an immediate, committed action (like the LIVE PREVIEW toggle),
 * not part of the dirty/SAVE flow:
 * it spawns or stops the daemon, persists the intent and installs or removes
 * the autostart entry so the choice survives a reboot.
 */
static void engine_set(struct alloy_ui *ui, const struct alloy_ui_item *it,
		       int on)
{
	const struct alloy_driver *drv = alloy_ui_driver(ui);
	struct alloy_mouse_config *m = alloy_mouse_cfg(alloy_ui_config(ui));
	struct alloy_config *cfg = alloy_ui_config(ui);
	uint16_t vid = drv->vendor_id;
	uint16_t pid = drv->product_id;

	(void)it;
	if (on) {
		m->accel_enabled = 1;
		alloy_state_store(drv, cfg);
		if (alloy_accel_spawn(vid, pid) == 0) {
			alloy_ui_set_var(ui, MOUSE_VAR_ENGINE, 1);
			alloy_accel_autostart_set(vid, pid, 1);
			alloy_ui_status(ui, "accel engine on");
		} else {
			/*
			 * do not persist the intent or install autostart for
			 * engine that could not start
			 */
			alloy_ui_set_var(ui, MOUSE_VAR_ENGINE, 0);
			m->accel_enabled = 0;
			alloy_state_store(drv, cfg);
			alloy_ui_status(
				ui, "engine failed: no access to /dev/input "
				    "or /dev/uinput (install the udev rule "
				    "and replug, or re-login after "
				    "usermod -aG input)");
		}
	} else {
		alloy_accel_stop(vid, pid);
		alloy_accel_autostart_set(vid, pid, 0);
		alloy_ui_set_var(ui, MOUSE_VAR_ENGINE, 0);
		m->accel_enabled = 0;
		alloy_state_store(drv, cfg);
		alloy_ui_status(ui, "accel engine off - motion back to normal");
	}
	alloy_ui_mark_dirty(ui);
}

/*
 * Map a fixed-point gain onto a graph row.
 * The visible range is exactly what the transform can reach at the reference
 * speed: 0.25x (bottom row) through 1.00x (middle) to 1.75x (top row).
 */
static int gain_to_row(int32_t g, int graph_h)
{
	const int32_t top = ALLOY_ACCEL_FP * 7 / 4;
	const int32_t span = ALLOY_ACCEL_FP * 3 / 2;
	int row = (int)(((int64_t)(top - g) * (graph_h - 1) + span / 2) / span);

	return ALLOY_CLAMP(row, 0, graph_h - 1);
}

/*
 * The exact speed-to-gain response the daemon applies:
 * flat 1.0x when neutral, ramping up with acceleration or down with deceleration
 * until it saturates.
 * Hand speed sweeps 0..30 counts/event across the axis, so saturation lands two
 * thirds of the way along.
 * Row steps are joined with corner and vertical glyphs to keep the line continuous.
 */
static void accel_graph_draw(struct alloy_ui *ui,
			     const struct alloy_ui_item *it,
			     struct alloy_ui_canvas *c)
{
	struct alloy_accel_params ap;
	const int graph_h = 5;
	int w = alloy_ui_canvas_w(c);
	int gx = 7;
	int gw = w - gx - 3;
	int prev = -1;
	int i;

	(void)it;
	if (gw < 4)
		return;

	alloy_ui_text(c, 0, gx + ALLOY_MAX(gw - 11, 0), ALLOY_UI_ST_DIM,
		      "SENSITIVITY");
	alloy_ui_glyph(c, 0, gx - 1, ALLOY_UI_G_UARROW, ALLOY_UI_ST_NORMAL);

	alloy_ui_vline(c, 1, gx - 1, graph_h, ALLOY_UI_G_VLINE,
		       ALLOY_UI_ST_NORMAL);
	alloy_ui_text(c, 1, 0, ALLOY_UI_ST_DIM, "1.75x");
	alloy_ui_text(c, 1 + graph_h / 2, 0, ALLOY_UI_ST_DIM, "1.00x");
	alloy_ui_text(c, 1 + graph_h - 1, 0, ALLOY_UI_ST_DIM, "0.25x");
	alloy_ui_glyph(c, 1, gx - 1, ALLOY_UI_G_RTEE, ALLOY_UI_ST_NORMAL);
	alloy_ui_glyph(c, 1 + graph_h / 2, gx - 1, ALLOY_UI_G_RTEE,
		       ALLOY_UI_ST_NORMAL);
	alloy_ui_glyph(c, 1 + graph_h - 1, gx - 1, ALLOY_UI_G_RTEE,
		       ALLOY_UI_ST_NORMAL);
	alloy_ui_glyph(c, 1 + graph_h, gx - 1, ALLOY_UI_G_LLCORNER,
		       ALLOY_UI_ST_NORMAL);
	alloy_ui_hline(c, 1 + graph_h, gx, gw, ALLOY_UI_G_HLINE,
		       ALLOY_UI_ST_NORMAL);
	alloy_ui_glyph(c, 1 + graph_h, gx + gw, ALLOY_UI_G_RARROW,
		       ALLOY_UI_ST_NORMAL);
	alloy_ui_text(c, 2 + graph_h, 2, ALLOY_UI_ST_DIM,
		      "SPEED OF HAND MOVEMENT");

	alloy_accel_from_config(alloy_ui_config(ui), &ap);
	for (i = 0; i < gw; i++) {
		int s = i * 30 / ALLOY_MAX(gw - 1, 1);
		int row = gain_to_row(alloy_accel_gain_fp(&ap, (int64_t)s * s),
				      graph_h);
		int rr;

		if (prev < 0 || row == prev) {
			alloy_ui_glyph(c, 1 + row, gx + i, ALLOY_UI_G_HLINE,
				       ALLOY_UI_ST_ACCENT);
		} else if (row < prev) { /* gain rising: turn upward */
			alloy_ui_glyph(c, 1 + prev, gx + i, ALLOY_UI_G_LRCORNER,
				       ALLOY_UI_ST_ACCENT);
			for (rr = row + 1; rr < prev; rr++)
				alloy_ui_glyph(c, 1 + rr, gx + i,
					       ALLOY_UI_G_VLINE,
					       ALLOY_UI_ST_ACCENT);
			alloy_ui_glyph(c, 1 + row, gx + i, ALLOY_UI_G_ULCORNER,
				       ALLOY_UI_ST_ACCENT);
		} else { /* gain falling: turn downward */
			alloy_ui_glyph(c, 1 + prev, gx + i, ALLOY_UI_G_URCORNER,
				       ALLOY_UI_ST_ACCENT);
			for (rr = prev + 1; rr < row; rr++)
				alloy_ui_glyph(c, 1 + rr, gx + i,
					       ALLOY_UI_G_VLINE,
					       ALLOY_UI_ST_ACCENT);
			alloy_ui_glyph(c, 1 + row, gx + i, ALLOY_UI_G_LLCORNER,
				       ALLOY_UI_ST_ACCENT);
		}
		prev = row;
	}
}

/*
 * Dotted pointer trajectory:
 * Wobbly hand motion that straightens out as the snapping cone widens.
 * Diamonds mark the start and end of the stroke.
 */
static void snap_draw(struct alloy_ui *ui, const struct alloy_ui_item *it,
		      struct alloy_ui_canvas *c)
{
	/* one period of sin() scaled to +-100, 16 samples */
	static const int8_t sine[16] = { 0, 38,	 71,  92,  100,	 92,  71,  38,
					 0, -38, -71, -92, -100, -92, -71, -38 };
	uint8_t snap = (*alloy_mouse_cfg(alloy_ui_config(ui))).angle_snapping;
	int w = alloy_ui_canvas_w(c) - 2;
	int amp = 100 - (int)snap * 100 / ALLOY_SNAP_MAX;
	int i;

	(void)it;
	if (w < 4)
		return;
	alloy_ui_glyph(c, 1, 1, ALLOY_UI_G_DIAMOND, ALLOY_UI_ST_ACCENT);
	alloy_ui_glyph(c, 1, w, ALLOY_UI_G_DIAMOND, ALLOY_UI_ST_ACCENT);
	for (i = 2; i < w; i++) {
		int v = sine[(i * 32 / w) % 16] * amp / 100;
		int row = v > 50 ? 0 : (v < -50 ? 2 : 1);

		alloy_ui_glyph(c, row, i, ALLOY_UI_G_BULLET,
			       ALLOY_UI_ST_ACCENT);
	}
}

static size_t actions_items(struct alloy_ui *ui, struct alloy_ui_item *out,
			    size_t max)
{
	const struct alloy_driver *drv = alloy_ui_driver(ui);
	size_t n = 0;
	uint8_t i;

	for (i = 0; i < alloy_mouse_info(drv)->num_buttons && n < max; i++)
		out[n++] = (struct alloy_ui_item){
			.kind = ALLOY_UI_CUSTOM,
			.idx = i,
			.rows = 2,
			.draw = button_draw,
			.activate = button_activate,
		};
	return n;
}

static size_t levels_items(struct alloy_ui *ui, struct alloy_ui_item *out,
			   size_t max)
{
	const struct alloy_driver *drv = alloy_ui_driver(ui);
	const struct alloy_mouse_config *m =
		&(*alloy_mouse_cfg(alloy_ui_config(ui)));
	static char labels[ALLOY_MAX_DPI_PRESETS][16];
	size_t n = 0;
	uint8_t i;

	for (i = 0;
	     i < m->dpi_count && i < ALLOY_MAX_DPI_PRESETS && n + 1 < max;
	     i++) {
		snprintf(labels[i], sizeof(labels[i]), "LEVEL %u", i + 1);
		out[n++] = (struct alloy_ui_item){
			.label = labels[i],
			.kind = ALLOY_UI_SLIDER,
			.idx = i,
			.flags = m->dpi_active == i ? ALLOY_UI_F_HOT : 0u,
			.min_val = alloy_mouse_info(drv)->dpi.min,
			.max_val = alloy_mouse_info(drv)->dpi.max,
			.step = alloy_mouse_info(drv)->dpi.step,
			.big_step = alloy_mouse_info(drv)->dpi.step * 10,
			.get = dpi_get,
			.set = dpi_set,
			.text = dpi_text,
			.activate = dpi_activate,
			.changed = dpi_changed,
		};
		out[n++] = (struct alloy_ui_item){ .kind = ALLOY_UI_SPACER };
	}

	if (m->dpi_count < dpi_limit(drv) && n < max)
		out[n++] = (struct alloy_ui_item){
			.label = "CREATE",
			.kind = ALLOY_UI_BUTTON,
			.activate = dpi_create,
		};
	return n;
}

static size_t power_items(struct alloy_ui *ui, struct alloy_ui_item *out,
			  size_t max)
{
	size_t n = 0;

	(void)ui;
	if (n < max)
		out[n++] = (struct alloy_ui_item){
			.label = "Battery Saver",
			.kind = ALLOY_UI_STEPPER,
			.min_val = ALLOY_SLEEP_MIN,
			.max_val = ALLOY_SLEEP_MAX,
			.step = ALLOY_SLEEP_STEP,
			.big_step = ALLOY_SLEEP_STEP * 5,
			.get = sleep_get,
			.set = sleep_set,
			.text = sleep_text,
			.changed = sleep_changed,
		};
	if (n < max)
		out[n++] = (struct alloy_ui_item){
			.label = "Smart Illum",
			.kind = ALLOY_UI_TOGGLE,
			.get = smart_get,
			.set = smart_set,
			.changed = illum_changed,
		};
	if (n < max)
		out[n++] = (struct alloy_ui_item){
			.label = "Dim Timer",
			.kind = ALLOY_UI_STEPPER,
			.min_val = 0,
			.max_val = ALLOY_ILLUM_DIM_MAX,
			.step = ALLOY_ILLUM_DIM_STEP,
			.big_step = ALLOY_ILLUM_DIM_STEP * 4,
			.get = dim_get,
			.set = dim_set,
			.text = dim_text,
			.changed = illum_changed,
		};
	return n;
}

static size_t tuning_items(struct alloy_ui *ui, struct alloy_ui_item *out,
			   size_t max)
{
	size_t n = 0;

#define PUSH(...)                                                         \
	do {                                                              \
		if (n < max)                                              \
			out[n++] = (struct alloy_ui_item){ __VA_ARGS__ }; \
	} while (0)

	PUSH(.label = "ACCELERATION / DECELERATION", .kind = ALLOY_UI_HEADING);
	PUSH(.kind = ALLOY_UI_CUSTOM, .rows = 8, .draw = accel_graph_draw,
	     .flags = ALLOY_UI_F_STATIC);
	PUSH(.label = "Acceleration", .kind = ALLOY_UI_STEPPER, .idx = 0,
	     .min_val = ALLOY_ACCEL_MIN, .max_val = ALLOY_ACCEL_MAX,
	     .step = ALLOY_ACCEL_STEP, .big_step = ALLOY_ACCEL_STEP * 10,
	     .get = accel_get, .set = accel_set, .changed = accel_changed);
	PUSH(.label = "Deceleration", .kind = ALLOY_UI_STEPPER, .idx = 1,
	     .min_val = ALLOY_DECEL_MIN, .max_val = ALLOY_DECEL_MAX,
	     .step = ALLOY_DECEL_STEP, .big_step = ALLOY_DECEL_STEP * 10,
	     .get = accel_get, .set = accel_set, .changed = accel_changed);
	PUSH(.kind = ALLOY_UI_SPACER);

	PUSH(.label = "ANGLE SNAPPING", .kind = ALLOY_UI_HEADING);
	PUSH(.kind = ALLOY_UI_CUSTOM, .rows = 3, .draw = snap_draw,
	     .flags = ALLOY_UI_F_STATIC);
	PUSH(.label = "Snapping", .kind = ALLOY_UI_STEPPER, .idx = 2,
	     .min_val = ALLOY_SNAP_MIN, .max_val = ALLOY_SNAP_MAX,
	     .step = ALLOY_SNAP_STEP, .big_step = ALLOY_SNAP_STEP * 5,
	     .unit = " deg", .get = accel_get, .set = accel_set,
	     .changed = accel_changed);
	PUSH(.label = "Engine", .kind = ALLOY_UI_TOGGLE, .get = engine_get,
	     .set = engine_set);
	PUSH(.kind = ALLOY_UI_SPACER);

#undef PUSH
	n += alloy_widget_polling(ui, out + n, max - n);
	return n;
}

static size_t mouse_items(struct alloy_ui *ui, const struct alloy_ui_pane *pane,
			  struct alloy_ui_item *out, size_t max)
{
	switch (pane->id) {
	case MP_ACTIONS:
		return actions_items(ui, out, max);
	case MP_MACRO:
		out[0] = (struct alloy_ui_item){
			.label = "LAUNCH",
			.kind = ALLOY_UI_BUTTON,
			.activate = macro_activate,
		};
		return 1;
	case MP_DEVICE:
		out[0] = (struct alloy_ui_item){
			.kind = ALLOY_UI_CUSTOM,
			.rows = 3,
			.flags = ALLOY_UI_F_STATIC,
			.draw = device_draw,
		};
		return 1;
	case MP_CENTER:
		return alloy_light_gateway(ui, out);
	case MP_LEVELS:
		return levels_items(ui, out, max);
	case MP_POWER:
		return power_items(ui, out, max);
	case MP_TUNING:
		return tuning_items(ui, out, max);
	default:
		return alloy_light_items(ui, pane, out, max);
	}
}

static void mouse_enter(struct alloy_ui *ui)
{
	const struct alloy_driver *drv = alloy_ui_driver(ui);

	memset(&link, 0, sizeof(link));
	link.battery_pct = -1;
	alloy_ui_set_var(ui, MOUSE_VAR_ENGINE,
			 alloy_accel_is_running(drv->vendor_id,
						drv->product_id));
}

/*
 * Refresh the wireless battery gauge on a slow cadence - the query is a device
 * round-trip, so it runs every few seconds rather than every frame.
 * battery_pct is left negative when the receiver reports no reading (the mouse
 * is asleep or unlinked), so the gauge shows "--" instead of a bogus 0%.
 */
static void mouse_tick(struct alloy_ui *ui, long ms)
{
	const struct alloy_driver *drv = alloy_ui_driver(ui);

	if (!has_battery(drv) || ms < link.next_poll_ms)
		return;
	link.next_poll_ms = ms + MOUSE_BATTERY_PERIOD_MS;

	/* Bluetooth link: the mouse shows up on bus 0x05 while paired over BT */
	link.bt_present = alloy_mouse_info(drv)->bt_product_id &&
			  alloy_hid_present_bus(
				  0x05, alloy_mouse_info(drv)->bt_product_id);

	if (!alloy_mouse_info(drv)->battery)
		return;

	/*
	 * Single idle poll is not proof the mouse is gone - the 2.4 GHz link
	 * micro-sleeps between reports.
	 * Keep the last reading across a couple of misses so the gauge stays
	 * put instead of flickering to "--", and only declare "no link" once
	 * the misses persist.
	 */
	if (alloy_mouse_info(drv)->battery(alloy_ui_device(ui),
					   &link.battery_pct, &link.charging)) {
		if (++link.misses >= MOUSE_BATTERY_MAX_MISSES)
			link.battery_pct = -1;
	} else {
		link.misses = 0;
	}
}

/*
 * Is there a mouse we can actually talk to right now?
 * Wired mice are always reachable; wireless receiver only counts once a 2.4 GHz
 * battery reading or a Bluetooth link says a mouse is on the other end.
 */
static int mouse_ready(struct alloy_ui *ui)
{
	const struct alloy_driver *drv = alloy_ui_driver(ui);

	if (!has_battery(drv))
		return 1;
	/*
	 * Bluetooth driver binds its hidraw node only while the mouse is actually
	 * connected, so once it is open the link is up - there is no bare-receiver
	 * case to wait out.
	 */
	if (alloy_mouse_info(drv)->link_implicit)
		return 1;
	return link.battery_pct >= 0 || link.bt_present;
}

static int mouse_key(struct alloy_ui *ui, int key)
{
	struct pair_wizard w = { 0, 0 };
	struct alloy_ui_modal m = {
		.title = "PAIR A NEW DEVICE",
		.h = 11,
		.w = 52,
		.draw = pair_draw,
		.key = pair_key,
		.data = &w,
	};

	/* PAIR shortcut in the DEVICE box: only when a mouse can be bound */
	if (key != 'p' || !needs_pairing(ui))
		return 0;
	alloy_ui_run_modal(ui, &m);
	return 1;
}

/*
 * Hardware CPI switch is the user acting on the device itself, not a pending edit:
 * it lands in the baseline too, so REVERT and the quit guard never fight the physical button.
 */
static void mouse_event(struct alloy_ui *ui, struct alloy_config *baseline)
{
	const struct alloy_mouse_config *m =
		alloy_mouse_cfg_c(alloy_ui_config(ui));

	alloy_mouse_cfg(baseline)->dpi_active = m->dpi_active;
	alloy_ui_status(ui, "level %u active (mouse button)",
			m->dpi_active + 1);
}

static void mouse_saved(struct alloy_ui *ui)
{
	const struct alloy_driver *drv = alloy_ui_driver(ui);

	if (alloy_ui_var(ui, MOUSE_VAR_ENGINE))
		alloy_accel_reload(drv->vendor_id, drv->product_id);
}

static void mouse_reverted(struct alloy_ui *ui)
{
	const struct alloy_driver *drv = alloy_ui_driver(ui);

	if (!alloy_ui_var(ui, MOUSE_VAR_ENGINE))
		return;
	alloy_state_store(drv, alloy_ui_config(ui));
	alloy_accel_reload(drv->vendor_id, drv->product_id);
}

static const char *center_title(struct alloy_ui *ui,
				const struct alloy_ui_pane *pane)
{
	(void)pane;
	return alloy_ui_driver(ui)->name;
}

static const struct alloy_ui_pane mouse_main_panes[] = {
	{
		.title = "ACTIONS",
		.id = MP_ACTIONS,
		.col = 0,
		.width_pct = 24,
		.height_pct = 72,
	},
	{
		.title = "MACRO EDITOR",
		.id = MP_MACRO,
		.col = 0,
		.width_pct = 24,
	},
	{
		.title = "DEVICE",
		.id = MP_DEVICE,
		.col = 1,
		.height_rows = 5,
		.visible = pane_wireless,
	},
	{
		.dyn_title = center_title,
		.id = MP_CENTER,
		.col = 1,
		.flags = ALLOY_UI_PANE_ART,
	},
	{
		.title = "CPI LEVELS",
		.id = MP_LEVELS,
		.col = 2,
		.width_pct = 22,
		.hint = "h/l: Adjust  Enter: Active",
	},
	{
		.title = "POWER",
		.id = MP_POWER,
		.col = 2,
		.width_pct = 22,
		.height_rows = 9,
		.visible = pane_wireless,
		.hint = "h/l: Adjust  Enter: Toggle",
	},
	{
		.title = "TUNING",
		.id = MP_TUNING,
		.col = 3,
		.width_pct = 24,
		.hint = "h/l: Adjust  Enter: Toggle",
	},
};

static const struct alloy_ui_screen mouse_screens[] = {
	{
		.name = "MAIN",
		.id = ALLOY_SCREEN_MAIN,
		.panes = mouse_main_panes,
		.num_panes = ALLOY_ARRAY_SIZE(mouse_main_panes),
		.hint = "Tab: Pane  Enter: Select  s: Save  q: Quit",
	},
	{
		.name = "ILLUMINATION",
		.id = ALLOY_SCREEN_LIGHT,
		.panes = alloy_light_panes,
		.num_panes = ALLOY_ARRAY_SIZE(alloy_light_panes),
		.hint = "Tab: Pane  Enter: Edit zone  s: Save  Esc: Back",
	},
};

const struct alloy_ui_desc alloy_mouse_ui = {
	.screens = mouse_screens,
	.num_screens = ALLOY_ARRAY_SIZE(mouse_screens),
	.items = mouse_items,
	.enter = mouse_enter,
	.art_cell = alloy_light_art_cell,
	.tick = mouse_tick,
	.ready = mouse_ready,
	.key = mouse_key,
	.event = mouse_event,
	.saved = mouse_saved,
	.reverted = mouse_reverted,
};

void alloy_mouse_defaults(const struct alloy_driver *drv,
			  struct alloy_config *cfg)
{
	const struct alloy_devinfo *info = alloy_devinfo(drv);
	const struct alloy_mouse_info *mi = alloy_mouse_info(drv);
	struct alloy_mouse_config *m = alloy_mouse_cfg(cfg);
	uint8_t i;

	alloy_devcfg_defaults(drv, cfg);

	/* one preset out of the box */
	m->dpi_count = 1;
	m->dpi[0][0] = 800;
	m->dpi[0][1] = 800;
	m->dpi_active = 0;

	m->reactive_color = (struct alloy_rgb){ 0xFF, 0xFF, 0xFF };
	if (info && (info->caps & ALLOY_CAP_FX_RAINBOW))
		m->startup_fx = ALLOY_STARTUP_RAINBOW;
	if (info && (info->caps & ALLOY_CAP_BATTERY))
		m->sleep_min = ALLOY_SLEEP_MIN_DEFAULT;

	for (i = 0; mi && i < mi->num_buttons && i < ALLOY_MAX_BUTTONS; i++)
		m->buttons[i] = mi->buttons[i].def;
}

static const char *action_type_name(enum alloy_action_type type)
{
	switch (type) {
	case ALLOY_ACT_MOUSE:
		return "mouse";
	case ALLOY_ACT_DPI_CYCLE:
		return "dpi";
	case ALLOY_ACT_SCROLL_UP:
		return "scrollup";
	case ALLOY_ACT_SCROLL_DOWN:
		return "scrolldown";
	case ALLOY_ACT_KEYBOARD:
		return "key";
	case ALLOY_ACT_MEDIA:
		return "media";
	case ALLOY_ACT_DISABLED:
	default:
		return "disabled";
	}
}

static int parse_action(const char *val, struct alloy_action *act)
{
	char name[16];
	unsigned value = 0;
	const char *colon = strchr(val, ':');
	size_t n;

	if (colon) {
		n = ALLOY_MIN((size_t)(colon - val), sizeof(name) - 1);
		value = (unsigned)strtoul(colon + 1, NULL, 10);
	} else {
		n = ALLOY_MIN(strlen(val), sizeof(name) - 1);
	}
	memcpy(name, val, n);
	name[n] = '\0';

	if (!strcmp(name, "mouse"))
		act->type = ALLOY_ACT_MOUSE;
	else if (!strcmp(name, "dpi"))
		act->type = ALLOY_ACT_DPI_CYCLE;
	else if (!strcmp(name, "scrollup"))
		act->type = ALLOY_ACT_SCROLL_UP;
	else if (!strcmp(name, "scrolldown"))
		act->type = ALLOY_ACT_SCROLL_DOWN;
	else if (!strcmp(name, "key"))
		act->type = ALLOY_ACT_KEYBOARD;
	else if (!strcmp(name, "media"))
		act->type = ALLOY_ACT_MEDIA;
	else if (!strcmp(name, "disabled"))
		act->type = ALLOY_ACT_DISABLED;
	else
		return -1;

	act->value = (uint16_t)value;
	return 0;
}

void alloy_mouse_state_save(const struct alloy_driver *drv,
			    const struct alloy_config *cfg, void *ctx,
			    alloy_state_emit_fn emit)
{
	const struct alloy_devinfo *info = alloy_devinfo(drv);
	const struct alloy_mouse_info *mi = alloy_mouse_info(drv);
	const struct alloy_mouse_config *m = alloy_mouse_cfg_c(cfg);
	char key[48];
	char val[48];
	uint8_t i;

	alloy_devcfg_state_save(drv, cfg, ctx, emit);

	snprintf(val, sizeof(val), "%u", m->dpi_count);
	emit(ctx, "mouse.dpi_count", val);
	snprintf(val, sizeof(val), "%u", m->dpi_active);
	emit(ctx, "mouse.dpi_active", val);
	for (i = 0; i < m->dpi_count && i < ALLOY_MAX_DPI_PRESETS; i++) {
		snprintf(key, sizeof(key), "mouse.dpi%u", i);
		snprintf(val, sizeof(val), "%u:%u", m->dpi[i][0], m->dpi[i][1]);
		emit(ctx, key, val);
	}

	if (info && (info->caps & ALLOY_CAP_FX_REACTIVE)) {
		if (m->reactive_enabled)
			snprintf(val, sizeof(val), "%02x%02x%02x",
				 m->reactive_color.r, m->reactive_color.g,
				 m->reactive_color.b);
		else
			snprintf(val, sizeof(val), "off");
		emit(ctx, "mouse.reactive", val);
	}
	if (info && (info->caps & ALLOY_CAP_FX_STARTUP)) {
		snprintf(val, sizeof(val), "%u", m->startup_fx);
		emit(ctx, "mouse.startup_fx", val);
	}
	if (info && (info->caps & ALLOY_CAP_HIGH_EFFICIENCY)) {
		snprintf(val, sizeof(val), "%u", m->high_efficiency ? 1 : 0);
		emit(ctx, "mouse.high_efficiency", val);
	}
	if (info && (info->caps & ALLOY_CAP_BATTERY)) {
		snprintf(val, sizeof(val), "%u", m->illum_smart ? 1 : 0);
		emit(ctx, "mouse.illum_smart", val);
		snprintf(val, sizeof(val), "%u", m->illum_dim_s);
		emit(ctx, "mouse.illum_dim_s", val);
		snprintf(val, sizeof(val), "%u", m->sleep_min);
		emit(ctx, "mouse.sleep_min", val);
	}

	for (i = 0; mi && i < mi->num_buttons && i < ALLOY_MAX_BUTTONS; i++) {
		snprintf(key, sizeof(key), "mouse.button%u", i);
		snprintf(val, sizeof(val), "%s:%u",
			 action_type_name(m->buttons[i].type),
			 m->buttons[i].value);
		emit(ctx, key, val);
	}

	snprintf(val, sizeof(val), "%d", m->acceleration);
	emit(ctx, "mouse.acceleration", val);
	snprintf(val, sizeof(val), "%d", m->deceleration);
	emit(ctx, "mouse.deceleration", val);
	snprintf(val, sizeof(val), "%u", m->angle_snapping);
	emit(ctx, "mouse.angle_snapping", val);
	snprintf(val, sizeof(val), "%u", m->accel_enabled ? 1 : 0);
	emit(ctx, "mouse.accel_enabled", val);
}

int alloy_mouse_state_load(const struct alloy_driver *drv,
			   struct alloy_config *cfg, const char *key,
			   const char *val)
{
	struct alloy_mouse_config *m = alloy_mouse_cfg(cfg);
	unsigned idx;
	unsigned a;
	unsigned b;
	unsigned rgb;

	if (alloy_devcfg_state_load(drv, cfg, key, val))
		return 1;
	if (strncmp(key, "mouse.", 6))
		return 0;
	key += 6;

	if (!strcmp(key, "dpi_count")) {
		m->dpi_count = (uint8_t)ALLOY_CLAMP(atoi(val), 1,
						    ALLOY_MAX_DPI_PRESETS);
	} else if (!strcmp(key, "dpi_active")) {
		m->dpi_active = (uint8_t)ALLOY_CLAMP(atoi(val), 0,
						     ALLOY_MAX_DPI_PRESETS - 1);
	} else if (sscanf(key, "dpi%u", &idx) == 1 &&
		   idx < ALLOY_MAX_DPI_PRESETS) {
		if (sscanf(val, "%u:%u", &a, &b) == 2) {
			m->dpi[idx][0] = (uint16_t)a;
			m->dpi[idx][1] = (uint16_t)b;
		}
	} else if (!strcmp(key, "reactive")) {
		if (sscanf(val, "%x", &rgb) == 1) {
			m->reactive_enabled = 1;
			m->reactive_color.r = (rgb >> 16) & 0xFF;
			m->reactive_color.g = (rgb >> 8) & 0xFF;
			m->reactive_color.b = rgb & 0xFF;
		} else {
			m->reactive_enabled = 0;
		}
	} else if (!strcmp(key, "startup_fx")) {
		m->startup_fx = (uint8_t)ALLOY_CLAMP(
			atoi(val), 0, ALLOY_STARTUP_REACTIVE_RAINBOW);
	} else if (!strcmp(key, "high_efficiency")) {
		m->high_efficiency = atoi(val) ? 1 : 0;
	} else if (!strcmp(key, "illum_smart")) {
		m->illum_smart = atoi(val) ? 1 : 0;
	} else if (!strcmp(key, "illum_dim_s")) {
		m->illum_dim_s = (uint16_t)ALLOY_CLAMP(atoi(val), 0,
						       ALLOY_ILLUM_DIM_MAX);
	} else if (!strcmp(key, "sleep_min")) {
		m->sleep_min =
			(uint8_t)ALLOY_CLAMP(atoi(val), 0, ALLOY_SLEEP_MAX);
	} else if (sscanf(key, "button%u", &idx) == 1 &&
		   idx < ALLOY_MAX_BUTTONS) {
		parse_action(val, &m->buttons[idx]);
	} else if (!strcmp(key, "acceleration")) {
		m->acceleration = (int8_t)atoi(val);
	} else if (!strcmp(key, "deceleration")) {
		m->deceleration = (int8_t)atoi(val);
	} else if (!strcmp(key, "angle_snapping")) {
		m->angle_snapping = (uint8_t)atoi(val);
	} else if (!strcmp(key, "accel_enabled")) {
		m->accel_enabled = atoi(val) ? 1 : 0;
	} else {
		return 0;
	}
	return 1;
}

/* hand-edited file may point the active preset past the count */
void alloy_mouse_state_done(const struct alloy_driver *drv,
			    struct alloy_config *cfg)
{
	struct alloy_mouse_config *m = alloy_mouse_cfg(cfg);

	alloy_light_normalize(drv, cfg);
	if (!m->dpi_count)
		m->dpi_count = 1;
	if (m->dpi_active >= m->dpi_count)
		m->dpi_active = (uint8_t)(m->dpi_count - 1);
}

static int mouse_has_cap(const struct alloy_driver *drv, uint64_t cap)
{
	const struct alloy_devinfo *info = alloy_devinfo(drv);

	return info && (info->caps & cap);
}

static int avail_dpi(const struct alloy_driver *drv)
{
	return mouse_has_cap(drv, ALLOY_CAP_DPI);
}

static int avail_transform(const struct alloy_driver *drv)
{
	return mouse_has_cap(drv, ALLOY_CAP_ACCELERATION |
					  ALLOY_CAP_DECELERATION |
					  ALLOY_CAP_ANGLE_SNAPPING);
}

static int avail_higheff(const struct alloy_driver *drv)
{
	return mouse_has_cap(drv, ALLOY_CAP_HIGH_EFFICIENCY);
}

static int parse_int_arg(const char *arg, int lo, int hi, int *out)
{
	char *end;
	long val;

	if (!arg || !*arg)
		return 0;
	val = strtol(arg, &end, 10);
	if (*end != '\0' || val < lo || val > hi)
		return 0;
	*out = (int)val;
	return 1;
}

static int parse_bool_arg(const char *arg, uint8_t *out)
{
	if (!arg || !*arg)
		return -1;
	if (!strcasecmp(arg, "1") || !strcasecmp(arg, "on") ||
	    !strcasecmp(arg, "true") || !strcasecmp(arg, "yes")) {
		*out = 1;
		return 0;
	}
	if (!strcasecmp(arg, "0") || !strcasecmp(arg, "off") ||
	    !strcasecmp(arg, "false") || !strcasecmp(arg, "no")) {
		*out = 0;
		return 0;
	}
	return -1;
}

static int opt_parse_dpi(const struct alloy_driver *drv, const char *arg,
			 struct alloy_config *cfg, char *err, size_t err_len)
{
	struct alloy_mouse_config *m = alloy_mouse_cfg(cfg);
	int cpi;
	uint8_t i;

	(void)drv;
	if (!parse_int_arg(arg, 1, 65535, &cpi)) {
		snprintf(err, err_len, "invalid CPI '%s'", arg ? arg : "");
		return -1;
	}
	for (i = 0; i < ALLOY_MAX_DPI_PRESETS; i++) {
		m->dpi[i][0] = (uint16_t)cpi;
		m->dpi[i][1] = (uint16_t)cpi;
	}
	return 0;
}

static int opt_validate_dpi(const struct alloy_driver *drv,
			    const struct alloy_config *cfg, char *err,
			    size_t err_len)
{
	const struct alloy_mouse_info *mi = alloy_mouse_info(drv);
	uint16_t cpi = alloy_mouse_cfg_c(cfg)->dpi[0][0];

	if (mi && (cpi < mi->dpi.min || cpi > mi->dpi.max)) {
		snprintf(err, err_len, "CPI %u out of range [%u, %u] for '%s'",
			 cpi, mi->dpi.min, mi->dpi.max, drv->name);
		return -1;
	}
	return 0;
}

static int opt_parse_accel(const struct alloy_driver *drv, const char *arg,
			   struct alloy_config *cfg, char *err, size_t err_len)
{
	int val;

	(void)drv;
	if (!parse_int_arg(arg, ALLOY_ACCEL_MIN, ALLOY_ACCEL_MAX, &val)) {
		snprintf(err, err_len,
			 "invalid acceleration '%s'; expected %d-%d",
			 arg ? arg : "", ALLOY_ACCEL_MIN, ALLOY_ACCEL_MAX);
		return -1;
	}
	alloy_mouse_cfg(cfg)->acceleration = (int8_t)val;
	alloy_mouse_cfg(cfg)->accel_enabled = 1;
	return 0;
}

static int opt_parse_decel(const struct alloy_driver *drv, const char *arg,
			   struct alloy_config *cfg, char *err, size_t err_len)
{
	int val;

	(void)drv;
	if (!parse_int_arg(arg, ALLOY_DECEL_MIN, ALLOY_DECEL_MAX, &val)) {
		snprintf(err, err_len,
			 "invalid deceleration '%s'; expected %d-%d",
			 arg ? arg : "", ALLOY_DECEL_MIN, ALLOY_DECEL_MAX);
		return -1;
	}
	alloy_mouse_cfg(cfg)->deceleration = (int8_t)val;
	alloy_mouse_cfg(cfg)->accel_enabled = 1;
	return 0;
}

static int opt_parse_snap(const struct alloy_driver *drv, const char *arg,
			  struct alloy_config *cfg, char *err, size_t err_len)
{
	int val;

	(void)drv;
	if (!parse_int_arg(arg, ALLOY_SNAP_MIN, ALLOY_SNAP_MAX, &val)) {
		snprintf(err, err_len,
			 "invalid angle snapping '%s'; expected %d-%d",
			 arg ? arg : "", ALLOY_SNAP_MIN, ALLOY_SNAP_MAX);
		return -1;
	}
	alloy_mouse_cfg(cfg)->angle_snapping = (uint8_t)val;
	alloy_mouse_cfg(cfg)->accel_enabled = 1;
	return 0;
}

static int opt_parse_higheff(const struct alloy_driver *drv, const char *arg,
			     struct alloy_config *cfg, char *err,
			     size_t err_len)
{
	uint8_t on = 1;

	(void)drv;
	(void)err;
	(void)err_len;
	if (arg)
		parse_bool_arg(arg, &on);
	alloy_mouse_cfg(cfg)->high_efficiency = on;
	return 0;
}

const struct alloy_cli_option alloy_mouse_cli_options[] = {
	{
		.name = "--dpi",
		.alias = "--cpi",
		.arg_desc = "<cpi>",
		.help = "Set the sensor CPI on every level",
		.has_arg = 1,
		.available = avail_dpi,
		.parse = opt_parse_dpi,
		.validate = opt_validate_dpi,
		.apply_step = ALLOY_STEP_DPI,
	},
	{
		.name = "--accel",
		.arg_desc = "<0-100>",
		.help = "Set host pointer acceleration intensity",
		.has_arg = 1,
		.available = avail_transform,
		.parse = opt_parse_accel,
	},
	{
		.name = "--decel",
		.arg_desc = "<0-100>",
		.help = "Set host pointer deceleration intensity",
		.has_arg = 1,
		.available = avail_transform,
		.parse = opt_parse_decel,
	},
	{
		.name = "--snap",
		.arg_desc = "<0-45>",
		.help = "Set angle snapping threshold in degrees",
		.has_arg = 1,
		.available = avail_transform,
		.parse = opt_parse_snap,
	},
	{
		.name = "--high-efficiency",
		.arg_desc = "[on|off]",
		.help = "Toggle wireless high-efficiency mode",
		.has_arg = 2,
		.available = avail_higheff,
		.parse = opt_parse_higheff,
		.apply_step = ALLOY_STEP_HIGH_EFFICIENCY,
	},
};

_Static_assert(ALLOY_ARRAY_SIZE(alloy_mouse_cli_options) ==
		       ALLOY_MOUSE_CLI_COUNT,
	       "ALLOY_MOUSE_CLI_COUNT is out of step with the table");
