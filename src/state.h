/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Host-side configuration state.
 *
 * Supported mice provide no command to read their configuration back,
 * so the baseline used by REVERT cannot come from the device.
 *
 * Instead the last configuration saved by alloyctl is persisted under
 * $XDG_CONFIG_HOME/alloyctl/ per device;
 * on first run the driver factory defaults seed the baseline.
 */
#ifndef ALLOY_STATE_H
#define ALLOY_STATE_H

#include "driver.h"

/*
 * Load the persisted baseline for the given driver into cfg.
 * Falls back to driver factory defaults when no state file exists
 * or when the file fails to parse.
 * Returns 0 when the file was used, 1 when defaults were used,
 * -1 on hard errors (defaults still set).
 */
int alloy_state_load(const struct alloy_driver *drv, struct alloy_config *cfg);

/* Persist cfg as the new baseline for the given driver */
int alloy_state_store(const struct alloy_driver *drv,
		      const struct alloy_config *cfg);

#endif /* ALLOY_STATE_H */
