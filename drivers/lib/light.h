/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Shared lighting screen.
 *
 * Driver-side code: it knows what an LED zone, an effect and a brightness level
 * are, and turns them into the generic controls the front-end can draw.
 * Drivers that have lighting reuse it; drivers that do not never link it.
 *
 * Driver whose effects need more than the conventions below (its own knobs
 * per effect, its own spatial animation) fills in struct alloy_light_ops
 * and points struct alloy_devinfo.light at it.
 */
#ifndef ALLOY_LIB_LIGHT_H
#define ALLOY_LIB_LIGHT_H

#include "lib/devcfg.h"

/* Screen ids the shared front-ends use */
#define ALLOY_SCREEN_MAIN 0u
#define ALLOY_SCREEN_LIGHT 1u

/* Pane ids */
#define ALLOY_PANE_LIGHT_EFFECTS 0x100u
#define ALLOY_PANE_LIGHT_PREVIEW 0x101u

/* Front-end scratch slot holding the zone the lighting screen edits */
#define ALLOY_VAR_ZONE 0

enum alloy_ctrl_type {
	ALLOY_CTRL_SLIDER,
	ALLOY_CTRL_CHOICE,
	ALLOY_CTRL_TOGGLE,
};

/* One knob an effect exposes */
struct alloy_effect_ctrl {
	const char *name;
	enum alloy_ctrl_type type;
	int min_val;
	int max_val;
	const char *const *choices;
	uint8_t num_choices;

	int (*get)(const struct alloy_config *cfg, uint8_t zone);
	void (*set)(struct alloy_config *cfg, uint8_t zone, int val);
};

struct alloy_light_ops {
	/* knobs of effect @fx; returns how many and sets @out */
	size_t (*fx_ctrls)(const struct alloy_driver *drv, uint8_t fx,
			   const struct alloy_effect_ctrl **out);
	/* nonzero when effect @fx honours the configured color */
	int (*fx_has_color)(const struct alloy_driver *drv, uint8_t fx);
	/* color of one art cell, for spatial effects; nonzero when filled */
	int (*cell_color)(const struct alloy_driver *drv,
			  const struct alloy_config *cfg, int row, int col,
			  long ms, struct alloy_rgb *out);
};

extern const struct alloy_ui_pane alloy_light_panes[2];

/* does this device have enough lighting to be worth a screen of its own? */
int alloy_light_available(const struct alloy_driver *drv);

size_t alloy_light_items(struct alloy_ui *ui, const struct alloy_ui_pane *pane,
			 struct alloy_ui_item *out, size_t max);

/* desc->art_cell implementation: paint group N is LED zone N */
int alloy_light_art_cell(struct alloy_ui *ui, int group, int row, int col,
			 long ms, struct alloy_rgb *out);

/* align a loaded configuration with what the hardware can actually show */
void alloy_light_normalize(const struct alloy_driver *drv,
			   struct alloy_config *cfg);

/* every lighting edit funnels through here: dirty tracking plus the live push */
void alloy_light_changed(struct alloy_ui *ui);

/* the ILLUMINATION gateway button; writes nothing when there is no lighting */
size_t alloy_light_gateway(struct alloy_ui *ui, struct alloy_ui_item *out);

#endif /* ALLOY_LIB_LIGHT_H */
