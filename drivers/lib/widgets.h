/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Control groups shared by the drivers that use struct alloy_devcfg.
 *
 * Driver-side code: a polling-rate group and a brightness stepper look the same
 * on a mouse and on a keyboard, so both shared front-ends splice these in
 * instead of each spelling them out.
 * A driver with neither never calls them.
 */
#ifndef ALLOY_LIB_WIDGETS_H
#define ALLOY_LIB_WIDGETS_H

#include "lib/devcfg.h"

/* heading, square-wave preview and the rate stepper; empty without rates */
size_t alloy_widget_polling(struct alloy_ui *ui, struct alloy_ui_item *out,
			    size_t max);

/* brightness stepper; empty without ALLOY_CAP_BRIGHTNESS */
size_t alloy_widget_brightness(struct alloy_ui *ui, struct alloy_ui_item *out,
			       size_t max);

#endif /* ALLOY_LIB_WIDGETS_H */
