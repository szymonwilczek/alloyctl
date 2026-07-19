// SPDX-License-Identifier: GPL-2.0-only
/*
 * Pure pointer-transform math: angle snapping, speed-dependent gain and
 * sub-count remainder carry.
 *
 * No device I/O, no floating point - so it is trivially unit-testable
 * and cheap enough to run on every motion event at 1000 Hz report rate.
 */
#include "accel.h"
#include "alloy.h"
#include "driver.h"

#define FP ALLOY_ACCEL_FP

#define SREF2 ALLOY_ACCEL_SREF2

/* tan(deg) * FP for deg 0..45, so angle snapping needs no trig at runtime */
static const int32_t TAN_FP[ALLOY_SNAP_MAX + 1] = {
	0,     1144,  2289,  3435,  4583,  5734,  6888,	 8047,	9210,  10380,
	11556, 12739, 13930, 15130, 16340, 17560, 18792, 20036, 21294, 22566,
	23853, 25157, 26478, 27818, 29179, 30560, 31964, 33392, 34846, 36327,
	37837, 39378, 40951, 42560, 44205, 45889, 47615, 49385, 51202, 53070,
	54991, 56970, 59009, 61113, 63287, 65536,
};

void alloy_accel_from_config(const struct alloy_config *cfg,
			     struct alloy_accel_params *p)
{
	p->accel = ALLOY_CLAMP(cfg->acceleration, ALLOY_ACCEL_MIN,
			       ALLOY_ACCEL_MAX);
	p->decel = ALLOY_CLAMP(cfg->deceleration, ALLOY_DECEL_MIN,
			       ALLOY_DECEL_MAX);
	p->snap = ALLOY_CLAMP(cfg->angle_snapping, ALLOY_SNAP_MIN,
			      ALLOY_SNAP_MAX);
}

/*
 * Acceleration and deceleration act on opposite ends of the speed range,
 * so they compose instead of cancelling:
 * deceleration lowers the gain while the hand moves slowly (precision)
 * and fades out as the ramp comes in, while acceleration raises it as
 * the ramp saturates (flicks).
 * Each contributes up to 0.75x, so both at 100 give the full S-curve from
 * 0.25x through 1.0x (at half the reference speed squared) to 1.75x.
 */
int32_t alloy_accel_gain_fp(const struct alloy_accel_params *p, int64_t s2)
{
	int64_t ramp = s2 * FP / SREF2; /* 0..FP, saturating */
	int32_t g;

	if (ramp > FP)
		ramp = FP;
	g = FP + (int32_t)((int64_t)p->accel * ramp / 100 * 3 / 4) -
	    (int32_t)((int64_t)p->decel * (FP - ramp) / 100 * 3 / 4);
	return ALLOY_CLAMP(g, ALLOY_ACCEL_GAIN_MIN, ALLOY_ACCEL_GAIN_MAX);
}

void alloy_accel_transform(const struct alloy_accel_params *p,
			   struct alloy_accel_state *st, int dx, int dy,
			   int *out_x, int *out_y)
{
	int64_t s2, ax, ay;
	int32_t g;
	int ox, oy;

	/* (a) angle snapping on the raw vector, before any gain */
	if (p->snap > 0 && (dx || dy)) {
		int adx = dx < 0 ? -dx : dx;
		int ady = dy < 0 ? -dy : dy;
		int32_t t = TAN_FP[p->snap];

		if ((int64_t)ady * FP < (int64_t)adx * t) {
			dy = 0; /* within the cone of the horizontal axis */
			st->rem_y = 0;
		} else if ((int64_t)adx * FP < (int64_t)ady * t) {
			dx = 0; /* within the cone of the vertical axis */
			st->rem_x = 0;
		}
	}

	/* (b) speed-dependent gain, carrying the fractional remainder */
	s2 = (int64_t)dx * dx + (int64_t)dy * dy;
	g = alloy_accel_gain_fp(p, s2);
	ax = (int64_t)st->rem_x + (int64_t)dx * g;
	ay = (int64_t)st->rem_y + (int64_t)dy * g;
	ox = (int)(ax / FP); /* truncates toward zero */
	oy = (int)(ay / FP);
	st->rem_x = (int32_t)(ax - (int64_t)ox * FP);
	st->rem_y = (int32_t)(ay - (int64_t)oy * FP);
	*out_x = ox;
	*out_y = oy;
}
