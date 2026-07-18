// SPDX-License-Identifier: GPL-2.0-only
/*
 * Illumination view.
 *
 * Full-screen lighting editor reached through the ILLUMINATION button:
 * the left third is the EFFECTS pane editing one zone, the right two thirds
 * preview the mouse with every zone drawn in its current color.
 *
 * Zone selection lives in the preview pane:
 *	TAB cycles the zone tabs and ENTER commits the choice,
 *	dropping focus straight into the EFFECTS pane so the zone
 *	can be edited without extra keystrokes.
 *	ESC leaves the view.
 */
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "tui_internal.h"
#include "default_art.h"

/* Fixed items of the EFFECTS pane, top to bottom */
enum illum_item {
	ILL_EFFECT,
	ILL_FREQ,
	ILL_SPEED,
	ILL_R,
	ILL_G,
	ILL_B,
	ILL_PALETTE,
	ILL_HEX,
	ILL_COUNT,
};

/*
 * Device-wide rows of the DEVICE section follow the fixed items;
 * -1 for rows the hardware lacks the capability for.
 */
static int illum_idx_brightness(const struct tui *t)
{
	if (!(t->drv->caps & ALLOY_CAP_BRIGHTNESS))
		return -1;
	return ILL_COUNT;
}

static int illum_idx_reactive(const struct tui *t)
{
	if (!(t->drv->caps & ALLOY_CAP_FX_REACTIVE))
		return -1;
	return ILL_COUNT + ((t->drv->caps & ALLOY_CAP_BRIGHTNESS) ? 1 : 0);
}

static int illum_idx_startup(const struct tui *t)
{
	if (!(t->drv->caps & ALLOY_CAP_FX_STARTUP))
		return -1;
	return ILL_COUNT + ((t->drv->caps & ALLOY_CAP_BRIGHTNESS) ? 1 : 0) +
	       ((t->drv->caps & ALLOY_CAP_FX_REACTIVE) ? 1 : 0);
}

static int illum_item_count(const struct tui *t)
{
	return ILL_COUNT + ((t->drv->caps & ALLOY_CAP_BRIGHTNESS) ? 1 : 0) +
	       ((t->drv->caps & ALLOY_CAP_FX_REACTIVE) ? 1 : 0) +
	       ((t->drv->caps & ALLOY_CAP_FX_STARTUP) ? 1 : 0);
}

static long illum_now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

/* Fully saturated hue (0-359) to RGB, one 60 degree segment at a time */
static struct alloy_rgb hue_to_rgb(int hue)
{
	uint8_t rise = (uint8_t)((hue % 60) * 255 / 60);
	uint8_t fall = (uint8_t)(255 - rise);

	switch ((hue / 60) % 6) {
	case 0:
		return (struct alloy_rgb){ 255, rise, 0 };
	case 1:
		return (struct alloy_rgb){ fall, 255, 0 };
	case 2:
		return (struct alloy_rgb){ 0, 255, rise };
	case 3:
		return (struct alloy_rgb){ 0, fall, 255 };
	case 4:
		return (struct alloy_rgb){ rise, 0, 255 };
	default:
		return (struct alloy_rgb){ 255, 0, fall };
	}
}

static struct alloy_rgb scale_rgb(struct alloy_rgb c, int num, int den)
{
	c.r = (uint8_t)((int)c.r * num / den);
	c.g = (uint8_t)((int)c.g * num / den);
	c.b = (uint8_t)((int)c.b * num / den);
	return c;
}

/*
 * What color a zone shows at the given moment.
 *
 * Effect class is derived from the driver's display name -
 * the same convention tui_fx_ignores_color() uses - so any driver whose
 * names mention BREATH, RAINBOW or DISCO gets faithful animation without
 * core code knowing its wire format.
 * Per-zone speed knob scales time, the frequency knob packs more cycles
 * into a period, and SLOW/FAST name variants bake their own tempo on top.
 */
static struct alloy_rgb zone_preview_color(const struct tui *t, int zone,
					   long ms)
{
	const struct alloy_config *cfg = &t->cfg;
	uint8_t fx = cfg->zone_fx[zone];
	struct alloy_rgb c = cfg->zone_color[zone];
	long tms = ms * cfg->zone_fx_speed[zone] / ALLOY_FX_RATE_DEF;
	int freq = ALLOY_CLAMP(cfg->zone_fx_freq[zone], ALLOY_FX_RATE_MIN,
			       ALLOY_FX_RATE_MAX);
	int breath = 0;
	int rainbow = 0;
	int disco = 0;

	if (fx && fx < t->drv->num_fx) {
		const char *name = t->drv->fx_names[fx];

		breath = strstr(name, "BREATH") != NULL;
		rainbow = strstr(name, "RAINBOW") != NULL;
		disco = strstr(name, "DISCO") != NULL;
		if (strstr(name, "SLOW"))
			tms /= 2;
		if (strstr(name, "FAST"))
			tms *= 2;
	}

	if (disco) {
		/* hop distant hues on a beat */
		long bucket = tms / ALLOY_MAX(1000 / freq, 50);

		c = hue_to_rgb((int)((bucket * 137) % 360));
	} else if (rainbow) {
		/* hue wheel; zones are phase shifted so the cycle
		 * visibly travels across the mouse */
		int hue = (int)((tms / 20 + (long)zone * freq * 30) % 360);

		c = hue_to_rgb(hue);
	}

	if (breath) {
		/* triangle wave between dark and full */
		long period = 3000;
		long phase = (tms * freq / ALLOY_FX_RATE_DEF) % period;
		long level = phase < period / 2 ? phase * 510 / period :
						  510 - phase * 510 / period;

		c = scale_rgb(c, (int)level, 255);
	}

	if (t->drv->caps & ALLOY_CAP_BRIGHTNESS)
		c = scale_rgb(c, ALLOY_MIN(cfg->brightness, 100), 100);
	return c;
}

/* Refresh the per-zone color pairs from the animation clock */
static void illum_zone_pairs(const struct tui *t, long ms)
{
	uint8_t i;

	if (COLORS < 8)
		return;

	for (i = 0; i < t->drv->num_zones && i < ALLOY_MAX_LED_ZONES; i++) {
		struct alloy_rgb c = zone_preview_color(t, i, ms);

		init_pair((short)(CLR_ZONE_BASE + i), tui_rgb_to_color(&c), -1);
	}
}

void tui_illum_enter(struct tui *t)
{
	t->view = VIEW_ILLUM;
	t->illum_zone = 0; /* smallest zone is the default */
	t->illum_tab = 0;
	t->illum_cursor = 0;
	t->illum_focus = ILLUM_FOCUS_PREVIEW;
}

static void draw_zone_tabs(struct tui *t, int y, int x, int w)
{
	int focused = t->illum_focus == ILLUM_FOCUS_PREVIEW;
	int i;

	(void)w;
	for (i = 0; i < t->drv->num_zones; i++) {
		int active = t->illum_zone == i;
		int hot = focused && t->illum_tab == i;

		if (hot)
			attron(COLOR_PAIR(CLR_SELECTED));
		else if (active)
			attron(COLOR_PAIR(CLR_ACCENT) | A_BOLD);
		else
			attron(COLOR_PAIR(CLR_BUTTON));
		mvprintw(y, x, " Z%d:%s ", i + 1, t->drv->zones[i].name);
		attroff(COLOR_PAIR(CLR_SELECTED));
		attroff(COLOR_PAIR(CLR_ACCENT) | A_BOLD);
		attroff(COLOR_PAIR(CLR_BUTTON));

		x += (int)strlen(t->drv->zones[i].name) + 6;
	}
}

/*
 * Art carries no zone coordinates, so the preview tints it in horizontal bands:
 * line N of the art belongs to zone N * num_zones / art_lines,
 * matching the top-to-bottom zone order every supported mouse uses.
 */
static void draw_mouse_preview(struct tui *t, int py, int px, int ph, int pw)
{
	const char *art = t->drv->ascii_art ? t->drv->ascii_art :
					      alloy_default_mouse_art;
	const char *p;
	int art_lines = 0;
	int art_width = 0;
	int cur = 0;
	int line = 0;
	int zones = ALLOY_MAX(t->drv->num_zones, 1);
	int y;
	int x;

	for (p = art; *p; p++) {
		if (*p == '\n') {
			art_lines++;
			art_width = ALLOY_MAX(art_width, cur);
			cur = 0;
		} else {
			cur++;
		}
	}

	y = py + ALLOY_MAX(1, (ph - art_lines) / 2);
	x = px + ALLOY_MAX(1, (pw - art_width) / 2);

	move(y, x);
	for (p = art; *p && y < py + ph; p++) {
		if (*p == '\n') {
			line++;
			y++;
			move(y, x);
		} else {
			int zone = line * zones / ALLOY_MAX(art_lines, 1);
			int pair = COLORS >= 8 ? CLR_ZONE_BASE + zone :
						 CLR_FRAME;

			if (zone == t->illum_zone)
				addch((chtype)*p | COLOR_PAIR(pair) | A_BOLD);
			else
				addch((chtype)*p | COLOR_PAIR(pair));
		}
	}
}

static void draw_rate_row(struct tui *t, int y, int x, const char *name,
			  uint8_t val, int selected)
{
	int rate_disabled = !t->cfg.zone_fx[t->illum_zone];
	int i;

	if (selected)
		attron(COLOR_PAIR(CLR_SELECTED));
	else if (rate_disabled)
		attron(COLOR_PAIR(CLR_DISABLED));
	mvprintw(y, x, "%-10s", name);
	if (selected)
		attroff(COLOR_PAIR(CLR_SELECTED));
	else if (rate_disabled)
		attroff(COLOR_PAIR(CLR_DISABLED));

	if (rate_disabled) {
		attron(COLOR_PAIR(CLR_DISABLED));
		mvprintw(y, x + 11, "steady");
		attroff(COLOR_PAIR(CLR_DISABLED));
		return;
	}

	mvprintw(y, x + 11, "< %2u >", val);
	move(y, x + 18);
	for (i = ALLOY_FX_RATE_MIN; i <= ALLOY_FX_RATE_MAX; i++)
		addch(i <= val ? (chtype)(ACS_CKBOARD | A_BOLD) :
				 (chtype)ACS_BULLET);
}

static void draw_color_channel(int y, int x, int w, const char *name,
			       uint8_t val, int selected, int disabled)
{
	int span = ALLOY_CLAMP(w - 16, 4, 20);
	int bar = (int)val * span / 255;
	int i;

	if (selected)
		attron(COLOR_PAIR(CLR_SELECTED));
	else if (disabled)
		attron(COLOR_PAIR(CLR_DISABLED));
	mvprintw(y, x, "%s < %3u >", name, val);
	if (selected)
		attroff(COLOR_PAIR(CLR_SELECTED));
	else if (disabled)
		attroff(COLOR_PAIR(CLR_DISABLED));

	move(y, x + 12);
	for (i = 0; i < span; i++)
		addch(i < bar ? (chtype)(ACS_CKBOARD | A_BOLD) :
				(chtype)ACS_BULLET);
}

/*
 * COLORS section is the old zone picker modal laid flat into the pane:
 * R/G/B steppers, shared palette and the hex field.
 * R/G/B grey out while the zone runs a color-cycling effect but
 * the palette and hex stay editable so color can be prepared in advance.
 */
static void draw_colors_section(struct tui *t, int y, int x, int w, int focused)
{
	struct alloy_rgb *rgb = &t->cfg.zone_color[t->illum_zone];
	int sel = t->illum_cursor;
	int greyed =
		tui_fx_ignores_color(t->drv, t->cfg.zone_fx[t->illum_zone]);
	char hex[8];
	size_t i;

	if (COLORS >= 8) {
		init_pair(CLR_PICKER_PREVIEW, COLOR_BLACK,
			  tui_rgb_to_color(rgb));
		for (i = 0; i < TUI_PALETTE_SIZE; i++)
			init_pair((short)(CLR_PICKER_SWATCH + i),
				  tui_rgb_to_color(&tui_palette[i]), -1);
	}

	attron(COLOR_PAIR(CLR_TITLE) | A_BOLD);
	mvprintw(y, x + 2, "COLORS");
	attroff(COLOR_PAIR(CLR_TITLE) | A_BOLD);

	draw_color_channel(y + 2, x + 2, w - 4, "R", rgb->r,
			   focused && sel == ILL_R, greyed);
	draw_color_channel(y + 3, x + 2, w - 4, "G", rgb->g,
			   focused && sel == ILL_G, greyed);
	draw_color_channel(y + 4, x + 2, w - 4, "B", rgb->b,
			   focused && sel == ILL_B, greyed);

	if (focused && sel == ILL_PALETTE)
		attron(COLOR_PAIR(CLR_SELECTED));
	mvprintw(y + 6, x + 2, "PALETTE");
	if (focused && sel == ILL_PALETTE)
		attroff(COLOR_PAIR(CLR_SELECTED));
	for (i = 0; i < TUI_PALETTE_SIZE; i++) {
		int sx = x + 12 + (int)i * 2 - (int)(i / 8) * 16;
		int sy = y + 6 + (int)(i / 8);

		if (focused && sel == ILL_PALETTE && (int)i == t->illum_swatch)
			mvaddch(sy, sx - 1, '[' | A_BOLD);
		if (COLORS >= 8)
			attron(COLOR_PAIR(CLR_PICKER_SWATCH + i) | A_BOLD);
		mvaddch(sy, sx, ACS_DIAMOND);
		if (COLORS >= 8)
			attroff(COLOR_PAIR(CLR_PICKER_SWATCH + i) | A_BOLD);
		if (focused && sel == ILL_PALETTE && (int)i == t->illum_swatch)
			mvaddch(sy, sx + 1, ']' | A_BOLD);
	}

	/* hex field: typed buffer while editing, live value otherwise */
	if (focused && sel == ILL_HEX)
		attron(COLOR_PAIR(CLR_SELECTED));
	mvprintw(y + 9, x + 2, "HEX");
	if (focused && sel == ILL_HEX)
		attroff(COLOR_PAIR(CLR_SELECTED));
	if (t->illum_hexbuf) {
		attron(A_BOLD);
		mvprintw(y + 9, x + 12, "#%-6s_", t->illum_hexbuf);
		attroff(A_BOLD);
	} else {
		snprintf(hex, sizeof(hex), "%02X%02X%02X", rgb->r, rgb->g,
			 rgb->b);
		mvprintw(y + 9, x + 12, "#%s", hex);
	}
	if (COLORS >= 8) {
		attron(COLOR_PAIR(CLR_PICKER_PREVIEW));
		mvprintw(y + 9, x + w - 9, "      ");
		attroff(COLOR_PAIR(CLR_PICKER_PREVIEW));
	}
}

/* Device-wide lighting that used to live in the main center pane */
static void draw_device_section(struct tui *t, int y, int x, int focused)
{
	static const char *const startup_names[] = { "OFF", "REACTIVE",
						     "RAINBOW", "REACT+RBOW" };
	int sel = t->illum_cursor;
	int row;

	if (illum_idx_brightness(t) < 0 && illum_idx_reactive(t) < 0 &&
	    illum_idx_startup(t) < 0)
		return;

	attron(COLOR_PAIR(CLR_TITLE) | A_BOLD);
	mvprintw(y, x + 2, "DEVICE");
	attroff(COLOR_PAIR(CLR_TITLE) | A_BOLD);
	y++;

	row = illum_idx_brightness(t);
	if (row >= 0) {
		y++;
		if (focused && sel == row)
			attron(COLOR_PAIR(CLR_SELECTED));
		mvprintw(y, x + 2, "%-10s", "BRIGHTNESS");
		if (focused && sel == row)
			attroff(COLOR_PAIR(CLR_SELECTED));
		mvprintw(y, x + 13, "< %3u%% >", t->cfg.brightness);
	}

	row = illum_idx_reactive(t);
	if (row >= 0) {
		y++;
		if (focused && sel == row)
			attron(COLOR_PAIR(CLR_SELECTED));
		mvprintw(y, x + 2, "%-10s", "REACTIVE");
		if (focused && sel == row)
			attroff(COLOR_PAIR(CLR_SELECTED));
		if (t->cfg.reactive_enabled)
			mvprintw(y, x + 13, "[#%02X%02X%02X]",
				 t->cfg.reactive_color.r,
				 t->cfg.reactive_color.g,
				 t->cfg.reactive_color.b);
		else
			mvprintw(y, x + 13, "[OFF]");
	}

	row = illum_idx_startup(t);
	if (row >= 0) {
		y++;
		if (focused && sel == row)
			attron(COLOR_PAIR(CLR_SELECTED));
		mvprintw(y, x + 2, "%-10s", "STARTUP");
		if (focused && sel == row)
			attroff(COLOR_PAIR(CLR_SELECTED));
		mvprintw(y, x + 13, "< %s >",
			 startup_names[t->cfg.startup_fx &
				       ALLOY_STARTUP_REACTIVE_RAINBOW]);
	}
}

static void draw_effects_pane(struct tui *t, int y, int x, int h, int w)
{
	int focused = t->illum_focus == ILLUM_FOCUS_EFFECTS;
	int sel = t->illum_cursor;
	const char *fx_name = "STEADY";

	tui_draw_pane_box(y, x, h, w, "EFFECTS", focused);

	attron(COLOR_PAIR(CLR_ACCENT) | A_BOLD);
	mvprintw(y + 2, x + 2, "ZONE Z%d: %s", t->illum_zone + 1,
		 t->drv->zones[t->illum_zone].name);
	attroff(COLOR_PAIR(CLR_ACCENT) | A_BOLD);

	if (t->drv->num_fx)
		fx_name = t->drv->fx_names[t->cfg.zone_fx[t->illum_zone] %
					   t->drv->num_fx];

	if (focused && sel == ILL_EFFECT)
		attron(COLOR_PAIR(CLR_SELECTED));
	mvprintw(y + 4, x + 2, "%-10s", "EFFECT");
	if (focused && sel == ILL_EFFECT)
		attroff(COLOR_PAIR(CLR_SELECTED));
	attron(COLOR_PAIR(CLR_ACCENT) | A_BOLD);
	mvprintw(y + 4, x + 13, "< %s >", fx_name);
	attroff(COLOR_PAIR(CLR_ACCENT) | A_BOLD);

	draw_rate_row(t, y + 6, x + 2, "FREQUENCY",
		      t->cfg.zone_fx_freq[t->illum_zone],
		      focused && sel == ILL_FREQ);
	draw_rate_row(t, y + 7, x + 2, "SPEED",
		      t->cfg.zone_fx_speed[t->illum_zone],
		      focused && sel == ILL_SPEED);

	draw_colors_section(t, y + 9, x, w, focused);
	draw_device_section(t, y + 20, x, focused);

	attron(COLOR_PAIR(CLR_DISABLED));
	mvprintw(y + h - 2, x + 2, "j/k: item  h/l: adjust");
	attroff(COLOR_PAIR(CLR_DISABLED));
}

/* Modal listing the driver's effects for the edited zone */
static void illum_effect_modal(struct tui *t)
{
	const int count = t->drv->num_fx;
	int sel;
	int y;
	int x;
	int i;
	int ch;

	if (count < 2) {
		tui_modal_message("EFFECT", "only STEADY on this device");
		return;
	}
	sel = t->cfg.zone_fx[t->illum_zone] % count;

	for (;;) {
		tui_illum_draw(t);
		tui_modal_frame(count + 4, 30, &y, &x,
				t->drv->zones[t->illum_zone].name);

		for (i = 0; i < count; i++) {
			if (i == sel)
				attron(COLOR_PAIR(CLR_SELECTED));
			mvprintw(y + 2 + i, x + 3, "%-24s",
				 t->drv->fx_names[i]);
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
			return;
		case '\n':
		case KEY_ENTER:
			t->cfg.zone_fx[t->illum_zone] = (uint8_t)sel;
			tui_lighting_changed(t);
			return;
		default:
			break;
		}
	}
}

static void illum_adjust(struct tui *t, int dir, int big)
{
	int zone = t->illum_zone;
	uint8_t *chan = NULL;
	uint8_t *rate;
	int val;

	if (t->illum_cursor == illum_idx_brightness(t)) {
		val = t->cfg.brightness + dir * (big ? 20 : 5);
		t->cfg.brightness = (uint8_t)ALLOY_CLAMP(val, 0, 100);
		t->dirty = memcmp(&t->cfg, &t->baseline, sizeof(t->cfg)) != 0;
		if (t->live_preview)
			tui_apply(t, t->drv->ops->apply_brightness,
				  "brightness");
		return;
	}
	if (t->illum_cursor == illum_idx_reactive(t)) {
		t->cfg.reactive_enabled = !t->cfg.reactive_enabled;
		tui_lighting_changed(t);
		return;
	}
	if (t->illum_cursor == illum_idx_startup(t)) {
		t->cfg.startup_fx =
			(uint8_t)((t->cfg.startup_fx + 4 + dir) % 4);
		tui_lighting_changed(t);
		return;
	}

	switch (t->illum_cursor) {
	case ILL_EFFECT:
		if (t->drv->num_fx < 2)
			return;
		t->cfg.zone_fx[zone] = (uint8_t)((t->cfg.zone_fx[zone] +
						  t->drv->num_fx + dir) %
						 t->drv->num_fx);
		tui_lighting_changed(t);
		return;
	case ILL_FREQ:
	case ILL_SPEED:
		if (!t->cfg.zone_fx[zone])
			return; /* steady has no rate */
		rate = t->illum_cursor == ILL_FREQ ?
			       &t->cfg.zone_fx_freq[zone] :
			       &t->cfg.zone_fx_speed[zone];
		val = *rate + dir;
		*rate = (uint8_t)ALLOY_CLAMP(val, ALLOY_FX_RATE_MIN,
					     ALLOY_FX_RATE_MAX);
		tui_lighting_changed(t);
		return;
	case ILL_R:
		chan = &t->cfg.zone_color[zone].r;
		break;
	case ILL_G:
		chan = &t->cfg.zone_color[zone].g;
		break;
	case ILL_B:
		chan = &t->cfg.zone_color[zone].b;
		break;
	case ILL_PALETTE:
		t->illum_swatch = (t->illum_swatch + TUI_PALETTE_SIZE + dir) %
				  TUI_PALETTE_SIZE;
		return;
	default:
		return;
	}

	if (tui_fx_ignores_color(t->drv, t->cfg.zone_fx[zone]))
		return;
	val = *chan + dir * (big ? 16 : 1);
	*chan = (uint8_t)ALLOY_CLAMP(val, 0, 255);
	tui_lighting_changed(t);
}

/*
 * Hex entry loop:
 * type up to six digits, enter commits (three-digit shorthand expands CSS-style),
 * esc abandons.
 * Buffer is exposed through illum_hexbuf so the redraw shows the digits as they are typed.
 */
static void illum_hex_input(struct tui *t)
{
	char buf[7] = "";
	size_t len = 0;
	int ch;

	t->illum_hexbuf = buf;
	for (;;) {
		tui_illum_draw(t);
		ch = getch();
		if (ch == 27) {
			t->illum_hexbuf = NULL;
			return;
		}
		if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
			if (len)
				buf[--len] = '\0';
			continue;
		}
		if (ch == '\n' || ch == KEY_ENTER)
			break;
		if (len < 6 && tui_hex_digit(ch) >= 0) {
			buf[len++] = (char)ch;
			buf[len] = '\0';
		}
	}
	t->illum_hexbuf = NULL;

	if (tui_parse_hex_color(buf, len, &t->cfg.zone_color[t->illum_zone])) {
		tui_status(t, "invalid hex color");
		return;
	}
	tui_lighting_changed(t);
}

static void illum_activate(struct tui *t)
{
	if (t->illum_cursor == illum_idx_reactive(t)) {
		tui_modal_color_reactive(t);
		return;
	}

	switch (t->illum_cursor) {
	case ILL_EFFECT:
		illum_effect_modal(t);
		return;
	case ILL_PALETTE:
		t->cfg.zone_color[t->illum_zone] = tui_palette[t->illum_swatch];
		tui_lighting_changed(t);
		return;
	case ILL_HEX:
		illum_hex_input(t);
		return;
	default:
		return;
	}
}

void tui_illum_draw(struct tui *t)
{
	int main_h = LINES - 2;
	int left_w = COLS / 3;
	int right_w = COLS - left_w;

	erase();
	illum_zone_pairs(t, illum_now_ms());

	draw_effects_pane(t, 0, 0, main_h, left_w);
	tui_draw_pane_box(0, left_w, main_h, right_w, t->drv->name,
			  t->illum_focus == ILLUM_FOCUS_PREVIEW);
	draw_zone_tabs(t, 2, left_w + 3, right_w - 4);
	draw_mouse_preview(t, 3, left_w + 1, main_h - 4, right_w - 2);

	mvhline(LINES - 2, 0, ACS_HLINE, COLS);
	attron(COLOR_PAIR(CLR_DISABLED));
	mvprintw(LINES - 1, 2, "%s", t->status);
	mvprintw(LINES - 1, COLS - 44,
		 "tab: zone  enter: edit zone  esc: back");
	attroff(COLOR_PAIR(CLR_DISABLED));

	refresh();
}

void tui_illum_handle_key(struct tui *t, int ch)
{
	switch (ch) {
	case 27:
	case 'q':
		t->view = VIEW_MAIN;
		return;
	case '\t':
		if (t->illum_focus == ILLUM_FOCUS_PREVIEW)
			t->illum_tab = (t->illum_tab + 1) % t->drv->num_zones;
		else
			t->illum_focus = ILLUM_FOCUS_PREVIEW;
		return;
	case KEY_BTAB:
		if (t->illum_focus == ILLUM_FOCUS_PREVIEW)
			t->illum_tab = (t->illum_tab + t->drv->num_zones - 1) %
				       t->drv->num_zones;
		return;
	case '\n':
	case KEY_ENTER:
		if (t->illum_focus == ILLUM_FOCUS_PREVIEW) {
			t->illum_zone = t->illum_tab;
			t->illum_focus = ILLUM_FOCUS_EFFECTS;
		} else {
			illum_activate(t);
		}
		return;
	default:
		break;
	}

	if (t->illum_focus != ILLUM_FOCUS_EFFECTS)
		return;

	switch (ch) {
	case KEY_UP:
	case 'k':
		t->illum_cursor = (t->illum_cursor + illum_item_count(t) - 1) %
				  illum_item_count(t);
		break;
	case KEY_DOWN:
	case 'j':
		t->illum_cursor = (t->illum_cursor + 1) % illum_item_count(t);
		break;
	case KEY_LEFT:
	case 'h':
		illum_adjust(t, -1, 0);
		break;
	case KEY_RIGHT:
	case 'l':
		illum_adjust(t, 1, 0);
		break;
	case 'H':
		illum_adjust(t, -1, 1);
		break;
	case 'L':
		illum_adjust(t, 1, 1);
		break;
	case 'x':
		t->illum_cursor = ILL_HEX;
		illum_hex_input(t);
		break;
	default:
		break;
	}
}
