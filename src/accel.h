/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Host-side pointer transform: acceleration, deceleration and angle snapping.
 *
 * These are NOT firmware features of any supported mouse.
 * See Documentation/architecture/pointer-transform.rst
 *
 * Per-device daemon grabs the mouse's evdev node, runs the transform below on
 * the relative motion and re-emits through a uinput virtual pointer.
 *
 * Math lives in accel_transform.c as pure, integer-only, unit-testable functions;
 * the device I/O and lifecycle live in accel.c.
 */
#ifndef ALLOY_ACCEL_H
#define ALLOY_ACCEL_H

#include <stdint.h>

struct alloy_config;

/* Config parameter ranges (and TUI stepper increments) */
#define ALLOY_ACCEL_MIN 0
#define ALLOY_ACCEL_MAX 100
#define ALLOY_ACCEL_STEP 5
#define ALLOY_DECEL_MIN 0
#define ALLOY_DECEL_MAX 100
#define ALLOY_DECEL_STEP 5
#define ALLOY_SNAP_MIN 0
#define ALLOY_SNAP_MAX 45 /* degrees half-cone; 0 disables snapping */
#define ALLOY_SNAP_STEP 1

/* Fixed-point scale and the gain clamp the transform never crosses */
#define ALLOY_ACCEL_FP (1 << 16)
#define ALLOY_ACCEL_GAIN_MIN (ALLOY_ACCEL_FP / 4) /* 0.25x */
#define ALLOY_ACCEL_GAIN_MAX (ALLOY_ACCEL_FP * 4) /* 4.0x */

/*
 * Squared reference speed, in (counts/event)^2, at which the gain ramp
 * saturates.
 * ~20 counts/event is a brisk flick at 1000 Hz.
 * Shared with the TUI so the tuning graph plots the exact curve the daemon applies.
 */
#define ALLOY_ACCEL_SREF2 400

/* Transform parameters derived from the working config */
struct alloy_accel_params {
	int accel; /* 0..100: gain rises with speed */
	int decel; /* 0..100: gain falls with speed */
	int snap; /* 0 = off, else half-cone in degrees (1..45) */
};

/* Per-stream carry: signed sub-count remainder in fixed-point */
struct alloy_accel_state {
	int32_t rem_x;
	int32_t rem_y;
};

/* Derive (clamped) transform parameters from the user configuration */
void alloy_accel_from_config(const struct alloy_config *cfg,
			     struct alloy_accel_params *p);

/* Fixed-point gain (scale ALLOY_ACCEL_FP) for squared speed */
int32_t alloy_accel_gain_fp(const struct alloy_accel_params *p, int64_t s2);

/*
 * Transform one relative motion sample (dx,dy) into (out_x,out_y),
 * carrying the fractional remainder in st, so slow motion never drifts or is lost
 */
void alloy_accel_transform(const struct alloy_accel_params *p,
			   struct alloy_accel_state *st, int dx, int dy,
			   int *out_x, int *out_y);

/*
 * Run the transform daemon for the given device.
 * Returns on teardown.
 */
int alloy_accel_daemon_run(uint16_t vid, uint16_t pid);

/* Running daemon pid for the device, or -1 if none */
int alloy_accel_pid(uint16_t vid, uint16_t pid);

/* 1 if daemon is running for the device, else 0 */
int alloy_accel_is_running(uint16_t vid, uint16_t pid);

/*
 * Signal running daemon to re-read its config (SIGHUP).
 * 0 on success.
 */
int alloy_accel_reload(uint16_t vid, uint16_t pid);

/*
 * Ask running daemon to exit (SIGTERM).
 * 0 on success.
 */
int alloy_accel_stop(uint16_t vid, uint16_t pid);

/*
 * Spawn a detached daemon for the device (double-fork + exec of this binary).
 * No-op if one is already running.
 * Returns 0 once the daemon is confirmed up.
 */
int alloy_accel_spawn(uint16_t vid, uint16_t pid);

/*
 * Install (enable=1) or remove (enable=0) the per-device XDG autostart entry
 * that restarts the daemon at the next login.
 * Returns 0 on success.
 */
int alloy_accel_autostart_set(uint16_t vid, uint16_t pid, int enable);

#endif /* ALLOY_ACCEL_H */
