/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Control groups shared by the drivers that use struct alloy_devcfg:
 * the polling-rate group and the brightness stepper.
 * See widgets.h.
 */
#include <stdio.h>

#include "lib/widgets.h"

/*
 * Square wave of the polling rate, drawn with real edges:
 * top-row plateaus for the high level, bottom-row plateaus for the low level,
 * vertical lines joining them.
 * Faster rate shortens the period, so the pulses pack tighter and more full
 * cycles fit across the width the closer we get to the fastest rate the driver offers.
 */
static void poll_draw(struct alloy_ui *ui, const struct alloy_ui_item *it,
		      struct alloy_ui_canvas *c)
{
	const struct alloy_devinfo *info = alloy_devinfo(alloy_ui_driver(ui));
	uint16_t hz = alloy_devcfg(alloy_ui_config(ui))->polling_hz;
	uint16_t max_hz = info->num_polling_rates ? info->polling_rates[0] : hz;
	const int h = 3;
	int w = alloy_ui_canvas_w(c) - 2;
	int half = (hz && max_hz) ? ALLOY_MAX(2, 2 * (int)max_hz / (int)hz) : 2;
	int period = half * 2;
	int i;
	int row;

	(void)it;
	for (i = 0; i < w; i++) {
		int phase = i % period;
		int col = 1 + i;

		if (phase == 0 && i > 0) {
			/* rising edge: low plateau (left) up to high (right) */
			alloy_ui_glyph(c, 0, col, ALLOY_UI_G_ULCORNER,
				       ALLOY_UI_ST_ACCENT);
			for (row = 1; row < h - 1; row++)
				alloy_ui_glyph(c, row, col, ALLOY_UI_G_VLINE,
					       ALLOY_UI_ST_ACCENT);
			alloy_ui_glyph(c, h - 1, col, ALLOY_UI_G_LRCORNER,
				       ALLOY_UI_ST_ACCENT);
		} else if (phase == half) {
			/* falling edge: high plateau (left) down to low (right) */
			alloy_ui_glyph(c, 0, col, ALLOY_UI_G_URCORNER,
				       ALLOY_UI_ST_ACCENT);
			for (row = 1; row < h - 1; row++)
				alloy_ui_glyph(c, row, col, ALLOY_UI_G_VLINE,
					       ALLOY_UI_ST_ACCENT);
			alloy_ui_glyph(c, h - 1, col, ALLOY_UI_G_LLCORNER,
				       ALLOY_UI_ST_ACCENT);
		} else if (phase < half) {
			alloy_ui_glyph(c, 0, col, ALLOY_UI_G_HLINE,
				       ALLOY_UI_ST_ACCENT);
		} else {
			alloy_ui_glyph(c, h - 1, col, ALLOY_UI_G_HLINE,
				       ALLOY_UI_ST_ACCENT);
		}
	}
}

/*
 * The rate is edited as an index that counts upward with the frequency,
 * while the driver's table is stored descending;
 * the two are mirrored here so the generic stepper's "right means more" holds.
 */
static int poll_get(struct alloy_ui *ui, const struct alloy_ui_item *it)
{
	const struct alloy_devinfo *info = alloy_devinfo(alloy_ui_driver(ui));
	uint16_t hz = alloy_devcfg(alloy_ui_config(ui))->polling_hz;
	int i;

	(void)it;
	for (i = 0; i < info->num_polling_rates; i++) {
		if (info->polling_rates[i] == hz)
			return info->num_polling_rates - 1 - i;
	}
	return info->num_polling_rates ? info->num_polling_rates - 1 : 0;
}

static void poll_set(struct alloy_ui *ui, const struct alloy_ui_item *it,
		     int val)
{
	const struct alloy_devinfo *info = alloy_devinfo(alloy_ui_driver(ui));
	int idx;

	(void)it;
	if (!info->num_polling_rates)
		return;
	idx = ALLOY_CLAMP(val, 0, info->num_polling_rates - 1);
	alloy_devcfg(alloy_ui_config(ui))->polling_hz =
		info->polling_rates[info->num_polling_rates - 1 - idx];
}

static void poll_text(struct alloy_ui *ui, const struct alloy_ui_item *it,
		      char *buf, size_t len)
{
	(void)it;
	snprintf(buf, len, "%4u Hz",
		 alloy_devcfg(alloy_ui_config(ui))->polling_hz);
}

static void poll_changed(struct alloy_ui *ui, const struct alloy_ui_item *it)
{
	(void)it;
	alloy_ui_changed(ui, ALLOY_STEP_POLLING);
}

size_t alloy_widget_polling(struct alloy_ui *ui, struct alloy_ui_item *out,
			    size_t max)
{
	const struct alloy_devinfo *info = alloy_devinfo(alloy_ui_driver(ui));
	size_t n = 0;

	if (!info || !info->num_polling_rates || max < 3)
		return 0;

	out[n++] = (struct alloy_ui_item){
		.label = "POLLING RATE",
		.kind = ALLOY_UI_HEADING,
	};
	out[n++] = (struct alloy_ui_item){
		.kind = ALLOY_UI_CUSTOM,
		.rows = 4,
		.flags = ALLOY_UI_F_STATIC,
		.draw = poll_draw,
	};
	out[n++] = (struct alloy_ui_item){
		.label = "Rate",
		.kind = ALLOY_UI_SLIDER,
		.min_val = 0,
		.max_val = info->num_polling_rates - 1,
		.get = poll_get,
		.set = poll_set,
		.text = poll_text,
		.changed = poll_changed,
	};
	return n;
}

static int bright_get(struct alloy_ui *ui, const struct alloy_ui_item *it)
{
	(void)it;
	return alloy_devcfg(alloy_ui_config(ui))->brightness;
}

static void bright_set(struct alloy_ui *ui, const struct alloy_ui_item *it,
		       int val)
{
	(void)it;
	alloy_devcfg(alloy_ui_config(ui))->brightness =
		(uint8_t)ALLOY_CLAMP(val, 0, 100);
}

static void bright_changed(struct alloy_ui *ui, const struct alloy_ui_item *it)
{
	(void)it;
	alloy_ui_changed(ui, ALLOY_STEP_BRIGHTNESS);
}

size_t alloy_widget_brightness(struct alloy_ui *ui, struct alloy_ui_item *out,
			       size_t max)
{
	const struct alloy_devinfo *info = alloy_devinfo(alloy_ui_driver(ui));

	if (!info || !(info->caps & ALLOY_CAP_BRIGHTNESS) || !max)
		return 0;

	out[0] = (struct alloy_ui_item){
		.label = "BRIGHTNESS",
		.kind = ALLOY_UI_SLIDER,
		.min_val = 0,
		.max_val = 100,
		.step = 5,
		.big_step = 20,
		.unit = "%",
		.get = bright_get,
		.set = bright_set,
		.changed = bright_changed,
	};
	return 1;
}
