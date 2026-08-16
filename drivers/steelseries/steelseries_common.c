/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SteelSeries vendor protocol helpers.
 *
 * Implements SteelSeries command framing and lighting preview
 * on top of the generic HID transport.
 */
#include <string.h>
#include <unistd.h>

#include "hid.h"
#include "steelseries/steelseries_common.h"

int steelseries_cmd_read_want(struct alloy_hid_dev *dev, const uint8_t *payload,
			      size_t len, int want, uint8_t *resp,
			      size_t resp_len, int attempts)
{
	return alloy_hid_cmd_read_want(dev, payload, len, want, resp, resp_len,
				       attempts);
}

int steelseries_cmd_read(struct alloy_hid_dev *dev, const uint8_t *payload,
			 size_t len, uint8_t *resp, size_t resp_len)
{
	return alloy_hid_cmd_read(dev, payload, len, resp, resp_len);
}

int steelseries_cmd(struct alloy_hid_dev *dev, const uint8_t *payload,
		    size_t len)
{
	return alloy_hid_cmd(dev, payload, len);
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

struct alloy_rgb
steelseries_preview_color(const struct alloy_driver *drv,
			  const struct alloy_config_common *cfg, uint8_t zone,
			  long ms)
{
	struct alloy_rgb c;
	uint8_t fx;
	long tms;
	int freq;
	const char *name = NULL;

	if (zone >= ALLOY_MAX_LED_ZONES || (drv && zone >= drv->num_zones))
		return (struct alloy_rgb){ 0, 0, 0 };

	c = cfg->zone_color[zone];
	fx = cfg->zone_fx[zone];
	tms = ms * cfg->zone_fx_speed[zone] / ALLOY_FX_RATE_DEF;
	freq = ALLOY_CLAMP(cfg->zone_fx_freq[zone], ALLOY_FX_RATE_MIN,
			   ALLOY_FX_RATE_MAX);

	if (drv && fx < drv->num_fx && drv->fx_names)
		name = drv->fx_names[fx];

	if (!name || fx == 0 || !strcmp(name, "STEADY"))
		return c;

	if (strstr(name, "SLOW"))
		tms /= 2;
	if (strstr(name, "FAST"))
		tms *= 2;

	if (strstr(name, "DISCO")) {
		long bucket = tms / ALLOY_MAX(1000 / freq, 50);

		return hue_to_rgb((int)((bucket * 137) % 360));
	}

	if (strstr(name, "RAINBOW")) {
		long shift = (drv->caps & ALLOY_CAP_FX_GLOBAL) ?
				     0 :
				     (long)zone * freq * 30;
		int hue = (int)((tms / 20 + shift) % 360);

		return hue_to_rgb(hue);
	}

	if (strstr(name, "BREATH")) {
		long period = 3000;
		long phase = (tms * freq / ALLOY_FX_RATE_DEF) % period;
		long level = phase < period / 2 ? phase * 510 / period :
						  510 - phase * 510 / period;

		return scale_rgb(c, (int)level, 255);
	}

	return c;
}
