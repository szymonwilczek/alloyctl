/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * alloyctl - SteelSeries device configuration TUI for Linux
 *
 * Common definitions shared by every translation unit
 */
#ifndef ALLOY_H
#define ALLOY_H

#include <stddef.h>
#include <stdint.h>

#define ALLOY_VERSION "0.1.0"

#define ALLOY_ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#define ALLOY_MIN(a, b) ((a) < (b) ? (a) : (b))
#define ALLOY_MAX(a, b) ((a) > (b) ? (a) : (b))
#define ALLOY_CLAMP(v, lo, hi) ALLOY_MIN(ALLOY_MAX(v, lo), hi)

/* Hard limits for statically sized configuration storage */
#define ALLOY_MAX_DPI_PRESETS 5
#define ALLOY_MAX_LED_ZONES 8
#define ALLOY_MAX_BUTTONS 16
#define ALLOY_MAX_POLLING_RATES 8

struct alloy_rgb {
	uint8_t r;
	uint8_t g;
	uint8_t b;
};

#endif /* ALLOY_H */
