// SPDX-License-Identifier: GPL-2.0-only
/*
 * Unit tests for the pure pointer-transform math (src/accel_transform.c):
 * gain shape, clamping, remainder conservation, neutral passthrough and angle
 * snapping.
 *
 * No device I/O involved.
 */
#include "accel.h"
#include "driver.h"
#include "test.h"

static long labs_l(long v)
{
	return v < 0 ? -v : v;
}

ALLOY_TEST(test_accel_from_config_clamps)
{
	struct alloy_config cfg = { 0 };
	struct alloy_accel_params p;

	cfg.acceleration = 127; /* int8 max, above ALLOY_ACCEL_MAX */
	cfg.deceleration = 0;
	cfg.angle_snapping = 200; /* uint8, above ALLOY_SNAP_MAX */
	alloy_accel_from_config(&cfg, &p);
	ASSERT_EQ(p.accel, ALLOY_ACCEL_MAX);
	ASSERT_EQ(p.decel, 0);
	ASSERT_EQ(p.snap, ALLOY_SNAP_MAX);
}

ALLOY_TEST(test_gain_neutral_is_unity)
{
	struct alloy_accel_params p = { .accel = 0, .decel = 0, .snap = 0 };
	int64_t s2;

	/* both knobs at zero -> exactly unity gain at every speed */
	for (s2 = 0; s2 <= 4000; s2 += 250)
		ASSERT_EQ(alloy_accel_gain_fp(&p, s2), ALLOY_ACCEL_FP);

	/*
	 * equal accel and decel cross unity exactly at half the squared reference
	 * speed, where the fading decel term balances the rising accel term
	 */
	p.accel = 60;
	p.decel = 60;
	ASSERT_EQ(alloy_accel_gain_fp(&p, ALLOY_ACCEL_SREF2 / 2),
		  ALLOY_ACCEL_FP);
}

ALLOY_TEST(test_gain_monotonic)
{
	struct alloy_accel_params up = { .accel = 50, .decel = 0, .snap = 0 };
	struct alloy_accel_params down = { .accel = 0, .decel = 50, .snap = 0 };
	int32_t prev_up = alloy_accel_gain_fp(&up, 0);
	int32_t prev_down = alloy_accel_gain_fp(&down, 0);
	int64_t s2;

	for (s2 = 0; s2 <= 4000; s2 += 100) {
		int32_t gu = alloy_accel_gain_fp(&up, s2);
		int32_t gd = alloy_accel_gain_fp(&down, s2);

		/* accel: above unity, rising toward the flick gain */
		ASSERT_TRUE(gu >= prev_up);
		ASSERT_TRUE(gu >= ALLOY_ACCEL_FP);
		/* decel: below unity while slow, recovering with speed */
		ASSERT_TRUE(gd >= prev_down);
		ASSERT_TRUE(gd <= ALLOY_ACCEL_FP);
		prev_up = gu;
		prev_down = gd;
	}
}

ALLOY_TEST(test_gain_s_curve)
{
	/* both at 100: precision floor when slow, full flick gain when fast */
	struct alloy_accel_params p = { .accel = 100, .decel = 100, .snap = 0 };

	ASSERT_EQ(alloy_accel_gain_fp(&p, 0), ALLOY_ACCEL_GAIN_MIN);
	ASSERT_EQ(alloy_accel_gain_fp(&p, ALLOY_ACCEL_SREF2 / 2),
		  ALLOY_ACCEL_FP);
	ASSERT_EQ(alloy_accel_gain_fp(&p, ALLOY_ACCEL_SREF2),
		  ALLOY_ACCEL_FP * 7 / 4);
}

ALLOY_TEST(test_gain_clamped)
{
	struct alloy_accel_params fast = { .accel = 100,
					   .decel = 0,
					   .snap = 0 };
	struct alloy_accel_params slow = { .accel = 0,
					   .decel = 100,
					   .snap = 0 };
	int32_t g;

	/* huge speed never lifts gain past the ceiling */
	g = alloy_accel_gain_fp(&fast, 1000000000LL);
	ASSERT_TRUE(g <= ALLOY_ACCEL_GAIN_MAX);
	ASSERT_TRUE(g > ALLOY_ACCEL_FP);

	/* full deceleration sits exactly on the floor at standstill */
	ASSERT_EQ(alloy_accel_gain_fp(&slow, 0), ALLOY_ACCEL_GAIN_MIN);

	/* and fades back to unity once the hand is past the reference speed */
	g = alloy_accel_gain_fp(&slow, 1000000000LL);
	ASSERT_EQ(g, ALLOY_ACCEL_FP);
}

ALLOY_TEST(test_transform_neutral_passthrough)
{
	struct alloy_accel_params p = { .accel = 0, .decel = 0, .snap = 0 };
	struct alloy_accel_state st = { 0 };
	const int in[][2] = { { 1, 0 },	  { 0, -1 }, { 5, 3 },
			      { -7, 12 }, { 0, 0 },  { -30, -30 } };
	size_t i;

	for (i = 0; i < sizeof(in) / sizeof(in[0]); i++) {
		int ox, oy;

		alloy_accel_transform(&p, &st, in[i][0], in[i][1], &ox, &oy);
		ASSERT_EQ(ox, in[i][0]);
		ASSERT_EQ(oy, in[i][1]);
	}
	/* nothing accumulated when the gain is exactly unity */
	ASSERT_EQ(st.rem_x, 0);
	ASSERT_EQ(st.rem_y, 0);
}

ALLOY_TEST(test_transform_remainder_conserves)
{
	struct alloy_accel_params p = { .accel = 100, .decel = 0, .snap = 0 };
	struct alloy_accel_state st = { 0 };
	const int dx = 3;
	int32_t g = alloy_accel_gain_fp(&p, (int64_t)dx * dx);
	long sum = 0;
	long expect;
	int i;

	for (i = 0; i < 1000; i++) {
		int ox, oy;

		alloy_accel_transform(&p, &st, dx, 0, &ox, &oy);
		sum += ox;
		ASSERT_EQ(oy, 0);
		/* remainder stays proper fraction */
		ASSERT_TRUE(labs_l(st.rem_x) < ALLOY_ACCEL_FP);
	}
	/* emitted counts equal the scaled input to within the carried fraction */
	expect = (long)((int64_t)1000 * dx * g / ALLOY_ACCEL_FP);
	ASSERT_TRUE(labs_l(sum - expect) <= 1);
}

ALLOY_TEST(test_transform_slow_drag_not_lost)
{
	/*
	 * decelerated slow drag runs at the reduced low-speed gain,
	 * but the remainder carry still delivers the scaled motion exactly - damped,
	 * never stalled or drifting
	 */
	struct alloy_accel_params p = { .accel = 0, .decel = 80, .snap = 0 };
	struct alloy_accel_state st = { 0 };
	int32_t g = alloy_accel_gain_fp(&p, 1);
	long expect = (long)((int64_t)200 * g / ALLOY_ACCEL_FP);
	long sum = 0;
	int i;

	ASSERT_TRUE(g < ALLOY_ACCEL_FP); /* deceleration bites when slow */
	for (i = 0; i < 200; i++) {
		int ox, oy;

		alloy_accel_transform(&p, &st, 1, 0, &ox, &oy);
		sum += ox;
	}
	ASSERT_TRUE(sum > 0);
	ASSERT_TRUE(labs_l(sum - expect) <= 1);
}

ALLOY_TEST(test_angle_snap_threshold)
{
	struct alloy_accel_params p = { .accel = 0, .decel = 0 };
	struct alloy_accel_state st = { 0 };
	int ox, oy;

	/* ~2.86 degrees off horizontal (dx=100, dy=5) */
	p.snap = 5; /* cone wide enough -> snaps to horizontal */
	st.rem_x = st.rem_y = 0;
	alloy_accel_transform(&p, &st, 100, 5, &ox, &oy);
	ASSERT_EQ(ox, 100);
	ASSERT_EQ(oy, 0);

	p.snap = 2; /* cone too narrow -> vector passes through */
	st.rem_x = st.rem_y = 0;
	alloy_accel_transform(&p, &st, 100, 5, &ox, &oy);
	ASSERT_EQ(ox, 100);
	ASSERT_EQ(oy, 5);

	/* exact diagonal with the widest cone stays diagonal */
	p.snap = ALLOY_SNAP_MAX;
	st.rem_x = st.rem_y = 0;
	alloy_accel_transform(&p, &st, 40, 40, &ox, &oy);
	ASSERT_EQ(ox, 40);
	ASSERT_EQ(oy, 40);

	/* near-vertical flick snaps to the vertical axis */
	p.snap = 10;
	st.rem_x = st.rem_y = 0;
	alloy_accel_transform(&p, &st, 4, 100, &ox, &oy);
	ASSERT_EQ(ox, 0);
	ASSERT_EQ(oy, 100);
}
