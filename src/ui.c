/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Front-end service dispatch.
 *
 * Driver-side UI code calls alloy_ui_*(); those calls land here and are forwarded
 * to whatever front-end registered itself.
 * Keeping the indirection in its own translation unit is what lets driver code
 * (and the unit tests) link without curses, and it is the seam that keeps
 * the front-end from being able to reach into driver internals:
 * traffic only ever flows through this table.
 *
 * With no front-end registered every service degrades to a no-op,
 * which is the headless case the tests build.
 */
#include <stdarg.h>
#include <stdio.h>
#include <time.h>

#include "ui.h"

static const struct alloy_ui_host *ui_host;

void alloy_ui_host_register(const struct alloy_ui_host *host)
{
	ui_host = host;
}

struct alloy_config *alloy_ui_config(struct alloy_ui *ui)
{
	return (ui_host && ui_host->config) ? ui_host->config(ui) : NULL;
}

const struct alloy_driver *alloy_ui_driver(struct alloy_ui *ui)
{
	return (ui_host && ui_host->driver) ? ui_host->driver(ui) : NULL;
}

struct alloy_device *alloy_ui_device(struct alloy_ui *ui)
{
	return (ui_host && ui_host->device) ? ui_host->device(ui) : NULL;
}

int alloy_ui_live_preview(struct alloy_ui *ui)
{
	return (ui_host && ui_host->live_preview) ? ui_host->live_preview(ui) :
						    0;
}

long alloy_ui_now_ms(void)
{
	struct timespec ts;

	if (ui_host && ui_host->now_ms)
		return ui_host->now_ms();

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

void alloy_ui_status(struct alloy_ui *ui, const char *fmt, ...)
{
	char buf[256];
	va_list ap;

	if (!ui_host || !ui_host->status)
		return;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	ui_host->status(ui, buf);
}

void alloy_ui_mark_dirty(struct alloy_ui *ui)
{
	if (ui_host && ui_host->mark_dirty)
		ui_host->mark_dirty(ui);
}

void alloy_ui_changed(struct alloy_ui *ui, const char *step)
{
	if (ui_host && ui_host->changed)
		ui_host->changed(ui, step);
}

int alloy_ui_push(struct alloy_ui *ui, const char *step)
{
	return (ui_host && ui_host->push) ? ui_host->push(ui, step) : -1;
}

int alloy_ui_var(struct alloy_ui *ui, int slot)
{
	return (ui_host && ui_host->var) ? ui_host->var(ui, slot) : 0;
}

void alloy_ui_set_var(struct alloy_ui *ui, int slot, int val)
{
	if (ui_host && ui_host->set_var)
		ui_host->set_var(ui, slot, val);
}

void alloy_ui_goto_screen(struct alloy_ui *ui, uint32_t screen_id)
{
	if (ui_host && ui_host->goto_screen)
		ui_host->goto_screen(ui, screen_id);
}

uint32_t alloy_ui_screen(struct alloy_ui *ui)
{
	return (ui_host && ui_host->screen) ? ui_host->screen(ui) : 0;
}

void alloy_ui_message(struct alloy_ui *ui, const char *title, const char *text)
{
	if (ui_host && ui_host->message)
		ui_host->message(ui, title, text);
}

int alloy_ui_menu(struct alloy_ui *ui, const char *title,
		  const char *const *items, int count, int cur)
{
	if (ui_host && ui_host->menu)
		return ui_host->menu(ui, title, items, count, cur);
	return -1;
}

int alloy_ui_pick_color(struct alloy_ui *ui,
			const struct alloy_ui_color_req *req)
{
	if (ui_host && ui_host->pick_color)
		return ui_host->pick_color(ui, req);
	return -1;
}

int alloy_ui_prompt_hex(struct alloy_ui *ui, struct alloy_rgb *rgb)
{
	if (ui_host && ui_host->prompt_hex)
		return ui_host->prompt_hex(ui, rgb);
	return -1;
}

int alloy_ui_capture_key(struct alloy_ui *ui, const char *title,
			 const char *prompt)
{
	if (ui_host && ui_host->capture_key)
		return ui_host->capture_key(ui, title, prompt);
	return -1;
}

int alloy_ui_run_modal(struct alloy_ui *ui, const struct alloy_ui_modal *m)
{
	if (ui_host && ui_host->run_modal)
		return ui_host->run_modal(ui, m);
	return -1;
}

int alloy_ui_canvas_h(struct alloy_ui_canvas *c)
{
	return (ui_host && ui_host->canvas_h) ? ui_host->canvas_h(c) : 0;
}

int alloy_ui_canvas_w(struct alloy_ui_canvas *c)
{
	return (ui_host && ui_host->canvas_w) ? ui_host->canvas_w(c) : 0;
}

void alloy_ui_text(struct alloy_ui_canvas *c, int y, int x,
		   enum alloy_ui_style style, const char *fmt, ...)
{
	char buf[256];
	va_list ap;

	if (!ui_host || !ui_host->canvas_text)
		return;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	ui_host->canvas_text(c, y, x, style, buf);
}

void alloy_ui_glyph(struct alloy_ui_canvas *c, int y, int x,
		    enum alloy_ui_glyph g, enum alloy_ui_style style)
{
	if (ui_host && ui_host->canvas_glyph)
		ui_host->canvas_glyph(c, y, x, g, style);
}

void alloy_ui_hline(struct alloy_ui_canvas *c, int y, int x, int len,
		    enum alloy_ui_glyph g, enum alloy_ui_style style)
{
	int i;

	for (i = 0; i < len; i++)
		alloy_ui_glyph(c, y, x + i, g, style);
}

void alloy_ui_vline(struct alloy_ui_canvas *c, int y, int x, int len,
		    enum alloy_ui_glyph g, enum alloy_ui_style style)
{
	int i;

	for (i = 0; i < len; i++)
		alloy_ui_glyph(c, y + i, x, g, style);
}

void alloy_ui_cell(struct alloy_ui_canvas *c, int y, int x, char ch,
		   const struct alloy_rgb *color)
{
	if (ui_host && ui_host->canvas_cell)
		ui_host->canvas_cell(c, y, x, ch, color);
}
