/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * alloyctl - Device configuration shell for Linux.
 *
 * Definitions shared by every translation unit.
 *
 * Nothing here describes any kind of device:
 * the core is a shell that binds drivers, runs a front-end and persists
 * whatever the driver tells it to persist.
 */
#ifndef ALLOY_H
#define ALLOY_H

#include <stddef.h>
#include <stdint.h>

#define ALLOY_ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#define ALLOY_MIN(a, b) ((a) < (b) ? (a) : (b))
#define ALLOY_MAX(a, b) ((a) > (b) ? (a) : (b))
#define ALLOY_CLAMP(v, lo, hi) ALLOY_MIN(ALLOY_MAX(v, lo), hi)

/*
 * A color, in the only sense the core has one:
 * something to paint a terminal cell with.
 * What it means for a device is the driver's business.
 */
struct alloy_rgb {
	uint8_t r;
	uint8_t g;
	uint8_t b;
};

#endif /* ALLOY_H */
