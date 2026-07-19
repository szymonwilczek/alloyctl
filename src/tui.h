/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef ALLOY_TUI_H
#define ALLOY_TUI_H

#include "driver.h"

/*
 * Ask the user which connected device to configure.
 * Draws full-screen chooser listing each candidate's name and VID:PID;
 * arrows/j/k move, enter picks, esc/q aborts.
 * Returns the chosen index into drivers[], or -1 if the user aborted.
 */
int alloy_tui_select_device(const struct alloy_driver *const *drivers,
			    int count);

/* Run the full-screen interface on opened device */
int alloy_tui_run(struct alloy_device *dev);

#endif /* ALLOY_TUI_H */
