// SPDX-License-Identifier: GPL-2.0-only
/*
 * Illumination view.
 *
 * Full-screen lighting editor reached through the ILLUMINATION button:
 * the left third is the EFFECTS pane editing one zone, the right two thirds
 * preview the device with every zone drawn in its current color.

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
	ILL_MULTICOLOR,
	ILL_DIRECTION,
	ILL_CUSTOM_ZONE,
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
	if (!(t->drv->caps & ALLOY_CAP_FX_REACTIVE) ||
	    !alloy_driver_is_mouse(t->drv))
		return -1;
	return ILL_COUNT + ((t->drv->caps & ALLOY_CAP_BRIGHTNESS) ? 1 : 0);
}

static int illum_item_count(const struct tui *t)
{
	return ILL_COUNT + ((t->drv->caps & ALLOY_CAP_BRIGHTNESS) ? 1 : 0) +
	       (illum_idx_reactive(t) >= 0 ? 1 : 0);
}

static int illum_item_valid(const struct tui *t, int idx)
{
	uint8_t fx = t->cfg.common.zone_fx[t->illum_zone];

	if (idx == ILL_FREQ && !(t->drv->caps & ALLOY_CAP_FX_FREQ))
		return 0;
	if (idx == ILL_SPEED) {
		if (!(t->drv->caps & ALLOY_CAP_FX_SPEED))
			return 0;
		/* speed is invalid for Steady (0), Solid (5), and Custom (14) */
		if (fx == 0 || fx == 5 || fx == 14)
			return 0;
	}
	if (idx == ILL_MULTICOLOR) {
		if (!(t->drv->caps & ALLOY_CAP_MULTICOLOR))
			return 0;
		if (fx != 0 && fx != 1 && fx != 6)
			return 0;
	}
	if (idx == ILL_DIRECTION) {
		if (!(t->drv->caps & ALLOY_CAP_DIRECTION))
			return 0;
		if (fx != 0 && fx != 6)
			return 0;
	}
	if (idx == ILL_CUSTOM_ZONE) {
		if (fx != 14)
			return 0;
	}
	if ((idx == ILL_R || idx == ILL_G || idx == ILL_B ||
	     idx == ILL_PALETTE || idx == ILL_HEX) &&
	    !(t->drv->caps & ALLOY_CAP_COLOR))
		return 0;
	return 1;
}

long tui_now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static struct alloy_rgb scale_rgb(struct alloy_rgb c, int num, int den)
{
	if (den <= 0)
		return (struct alloy_rgb){ 0, 0, 0 };
	c.r = (uint8_t)((int)c.r * num / den);
	c.g = (uint8_t)((int)c.g * num / den);
	c.b = (uint8_t)((int)c.b * num / den);
	return c;
}

static struct alloy_rgb hue_to_rgb(int hue)
{
	int h = ((hue % 360) + 360) % 360;
	int sector = h / 60;
	int frac = (h % 60) * 255 / 60;
	int q = 255 - frac;

	switch (sector) {
	case 0:
		return (struct alloy_rgb){ 255, (uint8_t)frac, 0 };
	case 1:
		return (struct alloy_rgb){ (uint8_t)q, 255, 0 };
	case 2:
		return (struct alloy_rgb){ 0, 255, (uint8_t)frac };
	case 3:
		return (struct alloy_rgb){ 0, (uint8_t)q, 255 };
	case 4:
		return (struct alloy_rgb){ (uint8_t)frac, 0, 255 };
	default:
		return (struct alloy_rgb){ 255, 0, (uint8_t)q };
	}
}

static struct alloy_rgb zone_preview_color(const struct tui *t, int zone,
					   long ms)
{
	const struct alloy_config_common *cfg = &t->cfg.common;
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
		/*
		 * Hue wheel:
		 * Zone-mask hardware cycles each zone with offset so the rainbow
		 * visibly travels across the mouse;
		 * global effect steps every zone through the same hue in lockstep
		 */
		long shift = (t->drv->caps & ALLOY_CAP_FX_GLOBAL) ?
				     0 :
				     (long)zone * freq * 30;
		int hue = (int)((tms / 20 + shift) % 360);

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
void tui_zone_fx_pairs(const struct tui *t, long ms)
{
	uint8_t i;

	if (COLORS < 8)
		return;

	for (i = 0; i < t->drv->num_zones && i < ALLOY_MAX_LED_ZONES; i++) {
		struct alloy_rgb c = zone_preview_color(t, i, ms);

		init_pair((short)(CLR_ZONE_BASE + i), tui_rgb_to_color(&c), -1);
	}
}

/*
 * On ALLOY_CAP_FX_GLOBAL hardware the effect selector drives the whole device,
 * not the edited zone.
 * Mirror the edited zone's effect and preview rates onto every zone, so the config
 * matches what the firmware will do and the preview animates the mouse as one
 * - per-zone colors stay independent.
 */
static void illum_sync_global_fx(struct tui *t)
{
	int zone = t->illum_zone;
	uint8_t i;

	if (!(t->drv->caps & ALLOY_CAP_FX_GLOBAL))
		return;
	for (i = 0; i < t->drv->num_zones && i < ALLOY_MAX_LED_ZONES; i++) {
		t->cfg.common.zone_fx[i] = t->cfg.common.zone_fx[zone];
		t->cfg.common.zone_fx_freq[i] =
			t->cfg.common.zone_fx_freq[zone];
		t->cfg.common.zone_fx_speed[i] =
			t->cfg.common.zone_fx_speed[zone];
		t->cfg.common.zone_fx_multicolor[i] =
			t->cfg.common.zone_fx_multicolor[zone];
		t->cfg.common.zone_fx_direction[i] =
			t->cfg.common.zone_fx_direction[zone];
		t->cfg.common.zone_fx_custom[i] =
			t->cfg.common.zone_fx_custom[zone];
	}
}

/*
 * Align loaded config with the global-effect rule above:
 * older baselines (and hand-edited state files) may carry divergent per-zone
 * effects that the hardware can never show.
 * First zone running effect wins - the same best-effort rule the driver uses
 * when it builds the packet.
 */
void tui_fx_global_normalize(struct tui *t, struct alloy_config *cfg)
{
	uint8_t src = 0;
	uint8_t i;

	if (!(t->drv->caps & ALLOY_CAP_FX_GLOBAL))
		return;
	for (i = 0; i < t->drv->num_zones && i < ALLOY_MAX_LED_ZONES; i++) {
		if (cfg->common.zone_fx[i]) {
			src = i;
			break;
		}
	}
	for (i = 0; i < t->drv->num_zones && i < ALLOY_MAX_LED_ZONES; i++) {
		cfg->common.zone_fx[i] = cfg->common.zone_fx[src];
		cfg->common.zone_fx_freq[i] = cfg->common.zone_fx_freq[src];
		cfg->common.zone_fx_speed[i] = cfg->common.zone_fx_speed[src];
		cfg->common.zone_fx_multicolor[i] =
			cfg->common.zone_fx_multicolor[src];
		cfg->common.zone_fx_direction[i] =
			cfg->common.zone_fx_direction[src];
		cfg->common.zone_fx_custom[i] = cfg->common.zone_fx_custom[src];
	}
}

void tui_illum_enter(struct tui *t)
{
	t->view = VIEW_ILLUM;
	t->illum_zone = 0; /* smallest zone is the default */
	t->illum_tab = 0;
	t->illum_cursor = 0;
	while (!illum_item_valid(t, t->illum_cursor) &&
	       t->illum_cursor < illum_item_count(t) - 1)
		t->illum_cursor++;
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
 * Fallback for art without zone markup: tint it in horizontal bands,
 * line N of the art belongs to zone N * num_zones / art_lines,
 * matching the top-to-bottom zone order every supported mouse uses.
 * Marked-up art paints exactly its marked characters instead.
 */
static void draw_banded_art(struct tui *t, const char *art, int y, int x,
			    int max_y, int art_lines)
{
	const char *p;
	int zones = ALLOY_MAX(t->drv->num_zones, 1);
	int line = 0;

	move(y, x);
	for (p = art; *p && y < max_y; p++) {
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

static void draw_mouse_preview(struct tui *t, int py, int px, int ph, int pw)
{
	const char *art = t->drv->ascii_art ? t->drv->ascii_art :
					      alloy_default_mouse_art;
	int art_lines;
	int art_width;
	int y;
	int x;

	tui_art_measure(art, &art_lines, &art_width);
	y = py + ALLOY_MAX(1, (ph - art_lines) / 2);
	x = px + ALLOY_MAX(1, (pw - art_width) / 2);

	if (tui_art_has_markup(art))
		tui_art_draw(t, art, y, x, py + ph, t->illum_zone);
	else
		draw_banded_art(t, art, y, x, py + ph, art_lines);
}

static void draw_rate_row(struct tui *t, int y, int x, const char *name,
			  uint8_t val, int selected, int supported)
{
	int rate_disabled = !supported || !t->cfg.common.zone_fx[t->illum_zone];
	int i;

	if (selected && supported)
		attron(COLOR_PAIR(CLR_SELECTED));
	else if (rate_disabled)
		attron(COLOR_PAIR(CLR_DISABLED));
	mvprintw(y, x, "%-10s", name);
	if (selected && supported)
		attroff(COLOR_PAIR(CLR_SELECTED));
	else if (rate_disabled)
		attroff(COLOR_PAIR(CLR_DISABLED));

	if (!supported) {
		attron(COLOR_PAIR(CLR_DISABLED));
		mvprintw(y, x + 11, "N/A");
		attroff(COLOR_PAIR(CLR_DISABLED));
		return;
	}

	if (rate_disabled) {
		attron(COLOR_PAIR(CLR_DISABLED));
		mvprintw(y, x + 11, "Steady");
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
	struct alloy_rgb *rgb = &t->cfg.common.zone_color[t->illum_zone];
	int has_color = (t->drv->caps & ALLOY_CAP_COLOR) != 0;
	int sel = t->illum_cursor;
	int greyed = !has_color ||
		     tui_fx_ignores_color(t->drv,
					  t->cfg.common.zone_fx[t->illum_zone]);
	char hex[8];
	size_t i;

	if (COLORS >= 8) {
		init_pair(CLR_PICKER_PREVIEW, COLOR_BLACK,
			  tui_rgb_to_color(rgb));
		for (i = 0; i < TUI_PALETTE_SIZE; i++)
			init_pair((short)(CLR_PICKER_SWATCH + i),
				  tui_rgb_to_color(&tui_palette[i]), -1);
	}

	attron(COLOR_PAIR(has_color ? CLR_TITLE : CLR_DISABLED) | A_BOLD);
	if (has_color)
		mvprintw(y, x + 2, "COLORS");
	else
		mvprintw(y, x + 2, "COLORS (Fixed Tint)");
	attroff(COLOR_PAIR(has_color ? CLR_TITLE : CLR_DISABLED) | A_BOLD);

	draw_color_channel(y + 2, x + 2, w - 4, "R", rgb->r,
			   focused && has_color && sel == ILL_R, greyed);
	draw_color_channel(y + 3, x + 2, w - 4, "G", rgb->g,
			   focused && has_color && sel == ILL_G, greyed);
	draw_color_channel(y + 4, x + 2, w - 4, "B", rgb->b,
			   focused && has_color && sel == ILL_B, greyed);

	if (focused && has_color && sel == ILL_PALETTE)
		attron(COLOR_PAIR(CLR_SELECTED));
	else if (!has_color)
		attron(COLOR_PAIR(CLR_DISABLED));
	mvprintw(y + 6, x + 2, "PALETTE");
	if (focused && has_color && sel == ILL_PALETTE)
		attroff(COLOR_PAIR(CLR_SELECTED));
	else if (!has_color)
		attroff(COLOR_PAIR(CLR_DISABLED));
	for (i = 0; i < TUI_PALETTE_SIZE; i++) {
		int sx = x + 12 + (int)i * 2 - (int)(i / 8) * 16;
		int sy = y + 6 + (int)(i / 8);

		if (focused && has_color && sel == ILL_PALETTE &&
		    (int)i == t->illum_swatch)
			mvaddch(sy, sx - 1, '[' | A_BOLD);
		if (COLORS >= 8 && has_color)
			attron(COLOR_PAIR(CLR_PICKER_SWATCH + i) | A_BOLD);
		else
			attron(COLOR_PAIR(CLR_DISABLED));
		mvaddch(sy, sx, ACS_DIAMOND);
		if (COLORS >= 8 && has_color)
			attroff(COLOR_PAIR(CLR_PICKER_SWATCH + i) | A_BOLD);
		else
			attroff(COLOR_PAIR(CLR_DISABLED));
		if (focused && has_color && sel == ILL_PALETTE &&
		    (int)i == t->illum_swatch)
			mvaddch(sy, sx + 1, ']' | A_BOLD);
	}

	/* hex field: typed buffer while editing, live value otherwise */
	if (focused && has_color && sel == ILL_HEX)
		attron(COLOR_PAIR(CLR_SELECTED));
	else if (!has_color)
		attron(COLOR_PAIR(CLR_DISABLED));
	mvprintw(y + 9, x + 2, "HEX");
	if (focused && has_color && sel == ILL_HEX)
		attroff(COLOR_PAIR(CLR_SELECTED));
	else if (!has_color)
		attroff(COLOR_PAIR(CLR_DISABLED));

	if (!has_color) {
		attron(COLOR_PAIR(CLR_DISABLED));
		snprintf(hex, sizeof(hex), "%02X%02X%02X", rgb->r, rgb->g,
			 rgb->b);
		mvprintw(y + 9, x + 12, "#%s", hex);
		attroff(COLOR_PAIR(CLR_DISABLED));
	} else if (t->illum_hexbuf) {
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
	int sel = t->illum_cursor;
	int row;

	if (illum_idx_brightness(t) < 0 && illum_idx_reactive(t) < 0)
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
		mvprintw(y, x + 13, "< %3u%% >", t->cfg.common.brightness);
	}

	row = illum_idx_reactive(t);
	if (row >= 0 && alloy_driver_is_mouse(t->drv)) {
		y++;
		if (focused && sel == row)
			attron(COLOR_PAIR(CLR_SELECTED));
		mvprintw(y, x + 2, "%-10s", "REACTIVE");
		if (focused && sel == row)
			attroff(COLOR_PAIR(CLR_SELECTED));
		if (t->cfg.mouse.reactive_enabled)
			mvprintw(y, x + 13, "[#%02X%02X%02X]",
				 t->cfg.mouse.reactive_color.r,
				 t->cfg.mouse.reactive_color.g,
				 t->cfg.mouse.reactive_color.b);
		else
			mvprintw(y, x + 13, "[OFF]");
	}
}

static void draw_effects_pane(struct tui *t, int y, int x, int h, int w)
{
	int focused = t->illum_focus == ILLUM_FOCUS_EFFECTS;
	int sel = t->illum_cursor;
	const char *fx_name = "STEADY";
	int cur_y;

	tui_draw_pane_box(y, x, h, w, "EFFECTS", focused);

	attron(COLOR_PAIR(CLR_ACCENT) | A_BOLD);
	mvprintw(y + 2, x + 2, "ZONE Z%d: %s", t->illum_zone + 1,
		 t->drv->zones[t->illum_zone].name);
	attroff(COLOR_PAIR(CLR_ACCENT) | A_BOLD);

	if (t->drv->num_fx)
		fx_name =
			t->drv->fx_names[t->cfg.common.zone_fx[t->illum_zone] %
					 t->drv->num_fx];

	cur_y = y + 4;

	if (focused && sel == ILL_EFFECT)
		attron(COLOR_PAIR(CLR_SELECTED));
	mvprintw(cur_y, x + 2, "%-10s", "EFFECT");
	if (focused && sel == ILL_EFFECT)
		attroff(COLOR_PAIR(CLR_SELECTED));
	attron(COLOR_PAIR(CLR_ACCENT) | A_BOLD);
	mvprintw(cur_y, x + 13, "< %s >", fx_name);
	attroff(COLOR_PAIR(CLR_ACCENT) | A_BOLD);
	cur_y++;

	if (t->drv->caps & ALLOY_CAP_FX_GLOBAL) {
		attron(COLOR_PAIR(CLR_DISABLED));
		mvprintw(cur_y, x + 2, "drives the whole device");
		attroff(COLOR_PAIR(CLR_DISABLED));
		cur_y++;
	}
	cur_y++;

	if (t->drv->caps & ALLOY_CAP_FX_FREQ) {
		draw_rate_row(t, cur_y, x + 2, "FREQUENCY",
			      t->cfg.common.zone_fx_freq[t->illum_zone],
			      focused && sel == ILL_FREQ, 1);
		cur_y++;
	}

	if (illum_item_valid(t, ILL_SPEED)) {
		draw_rate_row(t, cur_y, x + 2, "SPEED",
			      t->cfg.common.zone_fx_speed[t->illum_zone],
			      focused && sel == ILL_SPEED, 1);
		cur_y++;
	}

	if (illum_item_valid(t, ILL_MULTICOLOR)) {
		if (focused && sel == ILL_MULTICOLOR)
			attron(COLOR_PAIR(CLR_SELECTED));
		mvprintw(cur_y, x + 2, "%-10s", "MULTICOLOR");
		if (focused && sel == ILL_MULTICOLOR)
			attroff(COLOR_PAIR(CLR_SELECTED));
		attron(COLOR_PAIR(CLR_ACCENT) | A_BOLD);
		mvprintw(cur_y, x + 13, "< %s >",
			 t->cfg.common.zone_fx_multicolor[t->illum_zone] ?
				 "ON" :
				 "OFF");
		attroff(COLOR_PAIR(CLR_ACCENT) | A_BOLD);
		cur_y++;
	}

	if (illum_item_valid(t, ILL_DIRECTION)) {
		static const char *const wave_dir_labels[] = { "Right", "Left",
							       "Down", "Up" };
		static const char *const spiral_dir_labels[] = {
			"Clockwise", "Counterclockwise"
		};
		uint8_t d = t->cfg.common.zone_fx_direction[t->illum_zone];
		const char *d_name =
			(t->cfg.common.zone_fx[t->illum_zone] == 0) ?
				wave_dir_labels[d & 3] :
				spiral_dir_labels[d & 1];

		if (focused && sel == ILL_DIRECTION)
			attron(COLOR_PAIR(CLR_SELECTED));
		mvprintw(cur_y, x + 2, "%-10s",
			 t->cfg.common.zone_fx[t->illum_zone] == 0 ?
				 "ANGLE" :
				 "DIRECTION");
		if (focused && sel == ILL_DIRECTION)
			attroff(COLOR_PAIR(CLR_SELECTED));
		attron(COLOR_PAIR(CLR_ACCENT) | A_BOLD);
		mvprintw(cur_y, x + 13, "< %s >", d_name);
		attroff(COLOR_PAIR(CLR_ACCENT) | A_BOLD);
		cur_y++;
	}

	if (illum_item_valid(t, ILL_CUSTOM_ZONE)) {
		static const char *const cust_labels[] = { "Cust1", "Cust2",
							   "Cust3", "Cust4",
							   "Cust5" };
		uint8_t cz = t->cfg.common.zone_fx_custom[t->illum_zone];
		const char *cz_name = (cz < ALLOY_ARRAY_SIZE(cust_labels)) ?
					      cust_labels[cz] :
					      "Cust1";

		if (focused && sel == ILL_CUSTOM_ZONE)
			attron(COLOR_PAIR(CLR_SELECTED));
		mvprintw(cur_y, x + 2, "%-10s", "ZONE");
		if (focused && sel == ILL_CUSTOM_ZONE)
			attroff(COLOR_PAIR(CLR_SELECTED));
		attron(COLOR_PAIR(CLR_ACCENT) | A_BOLD);
		mvprintw(cur_y, x + 13, "< %s >", cz_name);
		attroff(COLOR_PAIR(CLR_ACCENT) | A_BOLD);
		cur_y++;
	}

	cur_y++;
	draw_colors_section(t, cur_y, x, w, focused);
	cur_y += 11;
	draw_device_section(t, cur_y, x, focused);

	attron(COLOR_PAIR(CLR_DISABLED));
	mvprintw(y + h - 2, x + 2, "j/k: Item  h/l: Adjust");
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
	sel = t->cfg.common.zone_fx[t->illum_zone] % count;

	for (;;) {
		tui_illum_draw(t);
		tui_modal_frame(count + 4, 30, &y, &x,
				(t->drv->caps & ALLOY_CAP_FX_GLOBAL) ?
					"WHOLE DEVICE" :
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
			t->cfg.common.zone_fx[t->illum_zone] = (uint8_t)sel;
			illum_sync_global_fx(t);
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
		val = t->cfg.common.brightness + dir * (big ? 20 : 5);
		t->cfg.common.brightness = (uint8_t)ALLOY_CLAMP(val, 0, 100);
		t->dirty = memcmp(&t->cfg, &t->baseline, sizeof(t->cfg)) != 0;
		if (t->live_preview)
			tui_apply(t, t->drv->ops->apply_brightness,
				  "brightness");
		return;
	}
	if (t->illum_cursor == illum_idx_reactive(t) &&
	    alloy_driver_is_mouse(t->drv)) {
		t->cfg.mouse.reactive_enabled = !t->cfg.mouse.reactive_enabled;
		tui_lighting_changed(t);
		return;
	}

	switch (t->illum_cursor) {
	case ILL_EFFECT:
		if (t->drv->num_fx < 2)
			return;
		t->cfg.common.zone_fx[zone] =
			(uint8_t)((t->cfg.common.zone_fx[zone] +
				   t->drv->num_fx + dir) %
				  t->drv->num_fx);
		illum_sync_global_fx(t);
		tui_lighting_changed(t);
		return;
	case ILL_FREQ:
	case ILL_SPEED:
		if (!t->cfg.common.zone_fx[zone])
			return; /* steady has no rate */
		rate = t->illum_cursor == ILL_FREQ ?
			       &t->cfg.common.zone_fx_freq[zone] :
			       &t->cfg.common.zone_fx_speed[zone];
		val = *rate + dir;
		*rate = (uint8_t)ALLOY_CLAMP(val, ALLOY_FX_RATE_MIN,
					     ALLOY_FX_RATE_MAX);
		illum_sync_global_fx(t);
		tui_lighting_changed(t);
		return;
	case ILL_MULTICOLOR:
		t->cfg.common.zone_fx_multicolor[zone] =
			!t->cfg.common.zone_fx_multicolor[zone];
		illum_sync_global_fx(t);
		tui_lighting_changed(t);
		return;
	case ILL_DIRECTION: {
		int max_d = (t->cfg.common.zone_fx[zone] == 0) ? 4 : 2;
		int d = (int)t->cfg.common.zone_fx_direction[zone] + dir;
		if (d < 0)
			d = max_d - 1;
		if (d >= max_d)
			d = 0;
		t->cfg.common.zone_fx_direction[zone] = (uint8_t)d;
		illum_sync_global_fx(t);
		tui_lighting_changed(t);
		return;
	}
	case ILL_CUSTOM_ZONE: {
		int cz = (int)t->cfg.common.zone_fx_custom[zone] + dir;
		if (cz < 0)
			cz = 4;
		if (cz > 4)
			cz = 0;
		t->cfg.common.zone_fx_custom[zone] = (uint8_t)cz;
		illum_sync_global_fx(t);
		tui_lighting_changed(t);
		return;
	}
	case ILL_R:
		chan = &t->cfg.common.zone_color[zone].r;
		break;
	case ILL_G:
		chan = &t->cfg.common.zone_color[zone].g;
		break;
	case ILL_B:
		chan = &t->cfg.common.zone_color[zone].b;
		break;
	case ILL_PALETTE:
		t->illum_swatch = (t->illum_swatch + TUI_PALETTE_SIZE + dir) %
				  TUI_PALETTE_SIZE;
		return;
	default:
		return;
	}

	if (tui_fx_ignores_color(t->drv, t->cfg.common.zone_fx[zone]))
		return;
	val = *chan + dir * (big ? 16 : 1);
	*chan = (uint8_t)ALLOY_CLAMP(val, 0, 255);
	illum_sync_global_fx(t);
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

	if (tui_parse_hex_color(buf, len,
				&t->cfg.common.zone_color[t->illum_zone])) {
		tui_status(t, "invalid hex color");
		return;
	}
	illum_sync_global_fx(t);
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
	case ILL_MULTICOLOR:
		t->cfg.common.zone_fx_multicolor[t->illum_zone] =
			!t->cfg.common.zone_fx_multicolor[t->illum_zone];
		illum_sync_global_fx(t);
		tui_lighting_changed(t);
		return;
	case ILL_DIRECTION: {
		int max_d = (t->cfg.common.zone_fx[t->illum_zone] == 0) ? 4 : 2;
		t->cfg.common.zone_fx_direction[t->illum_zone] =
			(uint8_t)((t->cfg.common
					   .zone_fx_direction[t->illum_zone] +
				   1) %
				  max_d);
		illum_sync_global_fx(t);
		tui_lighting_changed(t);
		return;
	}
	case ILL_CUSTOM_ZONE:
		t->cfg.common.zone_fx_custom[t->illum_zone] =
			(uint8_t)((t->cfg.common.zone_fx_custom[t->illum_zone] +
				   1) %
				  5);
		illum_sync_global_fx(t);
		tui_lighting_changed(t);
		return;
	case ILL_PALETTE:
		t->cfg.common.zone_color[t->illum_zone] =
			tui_palette[t->illum_swatch];
		illum_sync_global_fx(t);
		tui_lighting_changed(t);
		return;
	case ILL_HEX:
		illum_hex_input(t);
		return;
	default:
		return;
	}
}

/*
 * Paint the illumination view into the virtual screen without refreshing,
 * the counterpart of tui_render for the main screen.
 * Color picker lays its background down this way so its single refresh
 * composites picker over view in one frame.
 */
void tui_illum_render(struct tui *t)
{
	int main_h = LINES - 2;
	int left_w = ALLOY_CLAMP(COLS * 28 / 100, 26, 34);
	int right_w = COLS - left_w;

	erase();
	tui_zone_fx_pairs(t, tui_now_ms());

	draw_effects_pane(t, 0, 0, main_h, left_w);
	tui_draw_pane_box(0, left_w, main_h, right_w, t->drv->name,
			  t->illum_focus == ILLUM_FOCUS_PREVIEW);
	draw_zone_tabs(t, 2, left_w + 3, right_w - 4);
	draw_mouse_preview(t, 3, left_w + 1, main_h - 4, right_w - 2);

	mvhline(LINES - 2, 0, ACS_HLINE, COLS);
	attron(COLOR_PAIR(CLR_DISABLED));
	mvprintw(LINES - 1, 2, "%s", t->status);
	mvprintw(LINES - 1, COLS - 52,
		 "Tab: Zone  Enter: Edit zone  s: Save  Esc: Back");
	attroff(COLOR_PAIR(CLR_DISABLED));
}

void tui_illum_draw(struct tui *t)
{
	tui_illum_render(t);
	refresh();
}

void tui_illum_handle_key(struct tui *t, int ch)
{
	switch (ch) {
	case 27:
	case 'q':
		t->view = VIEW_MAIN;
		return;
	case 's':
		tui_save(t);
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
	case 'k': {
		int count = illum_item_count(t);
		int tries = count;
		do {
			t->illum_cursor = (t->illum_cursor + count - 1) % count;
		} while (!illum_item_valid(t, t->illum_cursor) && --tries > 0);
		break;
	}
	case KEY_DOWN:
	case 'j': {
		int count = illum_item_count(t);
		int tries = count;
		do {
			t->illum_cursor = (t->illum_cursor + 1) % count;
		} while (!illum_item_valid(t, t->illum_cursor) && --tries > 0);
		break;
	}
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
