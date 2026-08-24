/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Shared lighting screen.
 *
 * Left third is the EFFECTS pane editing one zone, right two thirds preview
 * the device with every zone in its current color.
 * Zone selection lives in the preview pane as a tab strip.
 *
 * The effect list, which knobs each effect exposes, whether an effect ignores
 * the configured color and how a running effect looks are all driver questions,
 * answered through struct alloy_light_ops.
 * Drivers that answer none of them get the conventional fallbacks below,
 * which is what keeps the simple drivers short.
 */
#include <stdio.h>
#include <string.h>

#include "lib/light.h"
#include "lib/widgets.h"

/* item ids inside the EFFECTS pane */
enum {
	LI_EFFECT = 1,
	LI_CTRL,
	LI_COLOR,
	LI_ZONE_TAB,
};

static const struct alloy_light_ops *light_ops(const struct alloy_driver *drv)
{
	const struct alloy_devinfo *info = alloy_devinfo(drv);

	return info ? info->light : NULL;
}

int alloy_light_available(const struct alloy_driver *drv)
{
	const struct alloy_devinfo *info = alloy_devinfo(drv);

	if (!info || info->num_zones == 0)
		return 0;
	if (info->num_zones > 1 || info->num_fx > 1 ||
	    (info->caps & ALLOY_CAP_COLOR) ||
	    (info->caps & ALLOY_CAP_FX_REACTIVE))
		return 1;
	return 0;
}

static uint8_t light_zone(struct alloy_ui *ui)
{
	const struct alloy_devinfo *info = alloy_devinfo(alloy_ui_driver(ui));
	int zone = alloy_ui_var(ui, ALLOY_VAR_ZONE);

	if (!info || zone < 0 || zone >= info->num_zones)
		return 0;
	return (uint8_t)zone;
}

static int param_get(const struct alloy_config *cfg, uint8_t zone, int slot)
{
	return alloy_devcfg_c(cfg)->zone_fx_param[zone][slot];
}

static void param_set(struct alloy_config *cfg, uint8_t zone, int slot, int val)
{
	alloy_devcfg(cfg)->zone_fx_param[zone][slot] = (uint8_t)val;
}

static int get_freq(const struct alloy_config *cfg, uint8_t zone)
{
	return param_get(cfg, zone, ALLOY_FX_P_FREQ);
}

static void set_freq(struct alloy_config *cfg, uint8_t zone, int val)
{
	param_set(cfg, zone, ALLOY_FX_P_FREQ, val);
}

static int get_speed(const struct alloy_config *cfg, uint8_t zone)
{
	return param_get(cfg, zone, ALLOY_FX_P_SPEED);
}

static void set_speed(struct alloy_config *cfg, uint8_t zone, int val)
{
	param_set(cfg, zone, ALLOY_FX_P_SPEED, val);
}

/*
 * Knobs of the effect the edited zone currently runs.
 * Driver that maps its own controls per effect wins;
 * otherwise the two conventional rate knobs are offered for
 * whichever the device claims.
 */
static size_t light_ctrls(struct alloy_ui *ui,
			  const struct alloy_effect_ctrl **out)
{
	const struct alloy_driver *drv = alloy_ui_driver(ui);
	const struct alloy_devinfo *info = alloy_devinfo(drv);
	const struct alloy_light_ops *ops = light_ops(drv);
	static struct alloy_effect_ctrl fallback[2];
	size_t n = 0;

	*out = NULL;
	if (ops && ops->fx_ctrls) {
		uint8_t fx = alloy_devcfg(alloy_ui_config(ui))
				     ->zone_fx[light_zone(ui)];

		return ops->fx_ctrls(drv, fx, out);
	}
	if (!info)
		return 0;

	if (info->caps & ALLOY_CAP_FX_FREQ) {
		fallback[n++] = (struct alloy_effect_ctrl){
			.name = "FREQUENCY",
			.type = ALLOY_CTRL_SLIDER,
			.min_val = ALLOY_FX_RATE_MIN,
			.max_val = ALLOY_FX_RATE_MAX,
			.get = get_freq,
			.set = set_freq,
		};
	}
	if (info->caps & ALLOY_CAP_FX_SPEED) {
		fallback[n++] = (struct alloy_effect_ctrl){
			.name = "SPEED",
			.type = ALLOY_CTRL_SLIDER,
			.min_val = ALLOY_FX_RATE_MIN,
			.max_val = ALLOY_FX_RATE_MAX,
			.get = get_speed,
			.set = set_speed,
		};
	}
	*out = fallback;
	return n;
}

/*
 * Effects that cycle their own hues ignore the configured zone color.
 * Driver may classify its own effects; fallback goes by the display-name
 * convention these drivers share.
 */
static int fx_has_color(const struct alloy_driver *drv, uint8_t fx)
{
	const struct alloy_devinfo *info = alloy_devinfo(drv);
	const struct alloy_light_ops *ops = light_ops(drv);
	const char *name;

	if (ops && ops->fx_has_color)
		return ops->fx_has_color(drv, fx);
	if (!info || !fx || fx >= info->num_fx || !info->fx_names)
		return 1;
	name = info->fx_names[fx];
	return strstr(name, "RAINBOW") == NULL && strstr(name, "DISCO") == NULL;
}

/*
 * On ALLOY_CAP_FX_GLOBAL hardware the effect selector drives the whole device,
 * not the edited zone.
 * Mirror the edited zone onto every zone so the config matches what the firmware
 * will do and the preview animates as one.
 */
static void sync_global_fx(struct alloy_ui *ui)
{
	const struct alloy_devinfo *info = alloy_devinfo(alloy_ui_driver(ui));
	struct alloy_devcfg *d = alloy_devcfg(alloy_ui_config(ui));
	uint8_t zone = light_zone(ui);
	uint8_t i;

	if (!info || !(info->caps & ALLOY_CAP_FX_GLOBAL))
		return;
	for (i = 0; i < info->num_zones && i < ALLOY_MAX_LED_ZONES; i++) {
		d->zone_fx[i] = d->zone_fx[zone];
		d->zone_color[i] = d->zone_color[zone];
		memcpy(d->zone_fx_param[i], d->zone_fx_param[zone],
		       sizeof(d->zone_fx_param[i]));
	}
}

/*
 * Align a loaded configuration with the rule above:
 * older baselines and hand-edited files may carry divergent per-zone effects
 * the hardware can never show.
 * First zone running an effect wins.
 */
void alloy_light_normalize(const struct alloy_driver *drv,
			   struct alloy_config *cfg)
{
	const struct alloy_devinfo *info = alloy_devinfo(drv);
	struct alloy_devcfg *d = alloy_devcfg(cfg);
	uint8_t src = 0;
	uint8_t i;

	if (!info || !(info->caps & ALLOY_CAP_FX_GLOBAL))
		return;
	for (i = 0; i < info->num_zones && i < ALLOY_MAX_LED_ZONES; i++) {
		if (d->zone_fx[i]) {
			src = i;
			break;
		}
	}
	for (i = 0; i < info->num_zones && i < ALLOY_MAX_LED_ZONES; i++) {
		d->zone_fx[i] = d->zone_fx[src];
		memcpy(d->zone_fx_param[i], d->zone_fx_param[src],
		       sizeof(d->zone_fx_param[i]));
	}
}

void alloy_light_changed(struct alloy_ui *ui)
{
	sync_global_fx(ui);
	alloy_ui_changed(ui, ALLOY_STEP_COLORS);
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

/*
 * What one zone looks like right now.
 * Effects are recognized by the display-name convention these drivers share;
 * Driver with a spatial effect of its own overrides per cell instead.
 */
static void zone_color(struct alloy_ui *ui, uint8_t zone, long ms,
		       struct alloy_rgb *out)
{
	const struct alloy_devinfo *info = alloy_devinfo(alloy_ui_driver(ui));
	const struct alloy_devcfg *d = alloy_devcfg_c(alloy_ui_config(ui));
	uint8_t fx = d->zone_fx[zone];
	struct alloy_rgb c = d->zone_color[zone];
	long tms = ms * ALLOY_MAX(d->zone_fx_param[zone][ALLOY_FX_P_SPEED], 1) /
		   ALLOY_FX_RATE_DEF;
	int freq = ALLOY_CLAMP(d->zone_fx_param[zone][ALLOY_FX_P_FREQ],
			       ALLOY_FX_RATE_MIN, ALLOY_FX_RATE_MAX);
	int breath = 0;
	int rainbow = 0;
	int disco = 0;

	if (info && fx && fx < info->num_fx && info->fx_names) {
		const char *name = info->fx_names[fx];

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
		 * Zone-mask hardware cycles each zone with an offset
		 * so the rainbow visibly travels across the device;
		 * global effect steps every zone through the same hue in lockstep
		 */
		long shift = (info && (info->caps & ALLOY_CAP_FX_GLOBAL)) ?
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

	if (info && (info->caps & ALLOY_CAP_BRIGHTNESS))
		c = scale_rgb(c, ALLOY_MIN(d->brightness, 100), 100);

	*out = c;
}

int alloy_light_art_cell(struct alloy_ui *ui, int group, int row, int col,
			 long ms, struct alloy_rgb *out)
{
	const struct alloy_driver *drv = alloy_ui_driver(ui);
	const struct alloy_devinfo *info = alloy_devinfo(drv);
	const struct alloy_light_ops *ops = light_ops(drv);
	int zone = group;

	if (!info || !info->num_zones)
		return 0;

	/* driver with a spatial effect paints the cell itself */
	if (ops && ops->cell_color &&
	    ops->cell_color(drv, alloy_ui_config(ui), row, col, ms, out))
		return 1;

	/*
	 * Unmarked art is tinted in horizontal bands:
	 * with one zone the whole portrait lights up,
	 * with several the bands follow the top-to-bottom order the zone
	 * list is written in.
	 */
	if (zone < 0)
		zone = 0;
	if (zone >= info->num_zones)
		return 0;

	zone_color(ui, (uint8_t)zone, ms, out);
	return 1;
}

static void light_edited(struct alloy_ui *ui, const struct alloy_ui_item *it)
{
	(void)it;
	alloy_light_changed(ui);
}

static int effect_get(struct alloy_ui *ui, const struct alloy_ui_item *it)
{
	(void)it;
	return alloy_devcfg(alloy_ui_config(ui))->zone_fx[light_zone(ui)];
}

static void effect_set(struct alloy_ui *ui, const struct alloy_ui_item *it,
		       int val)
{
	const struct alloy_devinfo *info = alloy_devinfo(alloy_ui_driver(ui));

	(void)it;
	if (!info || info->num_fx < 2)
		return;
	alloy_devcfg(alloy_ui_config(ui))->zone_fx[light_zone(ui)] =
		(uint8_t)(((val % info->num_fx) + info->num_fx) % info->num_fx);
}

static void effect_activate(struct alloy_ui *ui, const struct alloy_ui_item *it)
{
	const struct alloy_driver *drv = alloy_ui_driver(ui);
	const struct alloy_devinfo *info = alloy_devinfo(drv);
	int sel;

	if (!info || info->num_fx < 2) {
		alloy_ui_message(ui, "EFFECT", "only one mode on this device");
		return;
	}
	sel = alloy_ui_menu(ui,
			    (info->caps & ALLOY_CAP_FX_GLOBAL) ?
				    "WHOLE DEVICE" :
				    info->zones[light_zone(ui)].name,
			    info->fx_names, info->num_fx, effect_get(ui, it));
	if (sel < 0)
		return;
	effect_set(ui, it, sel);
	alloy_light_changed(ui);
}

static const struct alloy_effect_ctrl *ctrl_at(struct alloy_ui *ui, int idx)
{
	const struct alloy_effect_ctrl *ctrls = NULL;
	size_t n = light_ctrls(ui, &ctrls);

	if (idx < 0 || (size_t)idx >= n || !ctrls)
		return NULL;
	return &ctrls[idx];
}

static int ctrl_get(struct alloy_ui *ui, const struct alloy_ui_item *it)
{
	const struct alloy_effect_ctrl *c = ctrl_at(ui, it->idx);

	if (!c || !c->get)
		return 0;
	return c->get(alloy_ui_config(ui), light_zone(ui));
}

static void ctrl_set(struct alloy_ui *ui, const struct alloy_ui_item *it,
		     int val)
{
	const struct alloy_effect_ctrl *c = ctrl_at(ui, it->idx);

	if (!c || !c->set)
		return;
	c->set(alloy_ui_config(ui), light_zone(ui), val);
}

static struct alloy_rgb *color_of(struct alloy_ui *ui,
				  const struct alloy_ui_item *it)
{
	(void)it;
	return &alloy_devcfg(alloy_ui_config(ui))->zone_color[light_zone(ui)];
}

static int tab_get(struct alloy_ui *ui, const struct alloy_ui_item *it)
{
	return light_zone(ui) == it->idx;
}

static void tab_activate(struct alloy_ui *ui, const struct alloy_ui_item *it)
{
	alloy_ui_set_var(ui, ALLOY_VAR_ZONE, it->idx);
}

static void gateway_activate(struct alloy_ui *ui,
			     const struct alloy_ui_item *it)
{
	(void)it;
	alloy_ui_goto_screen(ui, ALLOY_SCREEN_LIGHT);
}

size_t alloy_light_gateway(struct alloy_ui *ui, struct alloy_ui_item *out)
{
	if (!alloy_light_available(alloy_ui_driver(ui)))
		return 0;

	out[0] = (struct alloy_ui_item){
		.label = "ILLUMINATION",
		.kind = ALLOY_UI_BUTTON,
		.activate = gateway_activate,
	};
	return 1;
}

static size_t effects_items(struct alloy_ui *ui, struct alloy_ui_item *out,
			    size_t max)
{
	const struct alloy_driver *drv = alloy_ui_driver(ui);
	const struct alloy_devinfo *info = alloy_devinfo(drv);
	const struct alloy_effect_ctrl *ctrls = NULL;
	static char zone_label[48];
	uint8_t zone = light_zone(ui);
	uint8_t fx;
	size_t nctrls;
	size_t n = 0;
	size_t i;

	if (!info)
		return 0;
	fx = alloy_devcfg(alloy_ui_config(ui))->zone_fx[zone];
	nctrls = light_ctrls(ui, &ctrls);

#define PUSH(...)                                                         \
	do {                                                              \
		if (n < max)                                              \
			out[n++] = (struct alloy_ui_item){ __VA_ARGS__ }; \
	} while (0)

	snprintf(zone_label, sizeof(zone_label), "ZONE Z%u: %s", zone + 1,
		 info->zones[zone].name);
	PUSH(.label = zone_label, .kind = ALLOY_UI_HEADING);
	PUSH(.kind = ALLOY_UI_SPACER);

	PUSH(.label = "EFFECT", .kind = ALLOY_UI_CHOICE, .id = LI_EFFECT,
	     .choices = info->fx_names, .num_choices = info->num_fx,
	     .get = effect_get, .set = effect_set, .activate = effect_activate,
	     .changed = light_edited);

	if (info->caps & ALLOY_CAP_FX_GLOBAL)
		PUSH(.label = "drives the whole device",
		     .kind = ALLOY_UI_HEADING);

	PUSH(.kind = ALLOY_UI_SPACER);

	for (i = 0; i < nctrls && n < max; i++) {
		const struct alloy_effect_ctrl *c = &ctrls[i];
		struct alloy_ui_item it = {
			.label = c->name,
			.id = LI_CTRL,
			.idx = (int)i,
			.get = ctrl_get,
			.set = ctrl_set,
			.changed = light_edited,
		};

		switch (c->type) {
		case ALLOY_CTRL_CHOICE:
			it.kind = ALLOY_UI_CHOICE;
			it.choices = c->choices;
			it.num_choices = c->num_choices;
			break;
		case ALLOY_CTRL_TOGGLE:
			it.kind = ALLOY_UI_TOGGLE;
			break;
		case ALLOY_CTRL_SLIDER:
		default:
			it.kind = ALLOY_UI_GAUGE;
			it.min_val = c->min_val;
			it.max_val = c->max_val;
			/* a steady zone has no rate to speak of */
			if (!fx)
				it.flags |= ALLOY_UI_F_DISABLED;
			break;
		}
		out[n++] = it;
	}

	if (info->caps & ALLOY_CAP_COLOR) {
		PUSH(.kind = ALLOY_UI_SPACER);
		PUSH(.label = "COLORS", .kind = ALLOY_UI_HEADING);
		PUSH(.label = "COLOR", .kind = ALLOY_UI_COLOR, .id = LI_COLOR,
		     .flags = fx_has_color(drv, fx) ? 0u : ALLOY_UI_F_DISABLED,
		     .color = color_of, .changed = light_edited);
	}

	if (info->caps & ALLOY_CAP_BRIGHTNESS) {
		PUSH(.kind = ALLOY_UI_SPACER);
		PUSH(.label = "DEVICE", .kind = ALLOY_UI_HEADING);
		n += alloy_widget_brightness(ui, out + n, max - n);
	}
#undef PUSH
	return n;
}

static size_t preview_items(struct alloy_ui *ui, struct alloy_ui_item *out,
			    size_t max)
{
	const struct alloy_devinfo *info = alloy_devinfo(alloy_ui_driver(ui));
	static char labels[ALLOY_MAX_LED_ZONES][24];
	size_t n = 0;
	uint8_t i;

	if (!info)
		return 0;
	for (i = 0; i < info->num_zones && i < ALLOY_MAX_LED_ZONES && n < max;
	     i++) {
		snprintf(labels[i], sizeof(labels[i]), "Z%u:%s", i + 1,
			 info->zones[i].name);
		out[n++] = (struct alloy_ui_item){
			.label = labels[i],
			.kind = ALLOY_UI_BUTTON,
			.id = LI_ZONE_TAB,
			.idx = i,
			.get = tab_get,
			.activate = tab_activate,
		};
	}
	return n;
}

size_t alloy_light_items(struct alloy_ui *ui, const struct alloy_ui_pane *pane,
			 struct alloy_ui_item *out, size_t max)
{
	switch (pane->id) {
	case ALLOY_PANE_LIGHT_EFFECTS:
		return effects_items(ui, out, max);
	case ALLOY_PANE_LIGHT_PREVIEW:
		return preview_items(ui, out, max);
	default:
		return 0;
	}
}

static const char *preview_title(struct alloy_ui *ui,
				 const struct alloy_ui_pane *pane)
{
	(void)pane;
	return alloy_ui_driver(ui)->name;
}

const struct alloy_ui_pane alloy_light_panes[2] = {
	{
		.title = "EFFECTS",
		.id = ALLOY_PANE_LIGHT_EFFECTS,
		.col = 0,
		.width_pct = 28,
		.min_width = 26,
		.max_width = 36,
		.hint = "j/k: Item  h/l: Adjust  x: Hex",
	},
	{
		.dyn_title = preview_title,
		.id = ALLOY_PANE_LIGHT_PREVIEW,
		.col = 1,
		.flags = ALLOY_UI_PANE_ART | ALLOY_UI_PANE_TABS,
	},
};
