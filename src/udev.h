/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef ALLOY_UDEV_H
#define ALLOY_UDEV_H

#include <stdio.h>

/*
 * Emit udev rules file granting unprivileged access to the /dev/hidraw*
 * node of every mouse in the driver registry.
 *
 * Rules are generated from the registry itself, so they always match exactly
 * the devices this build supports - new driver is covered the moment it is
 * linked in, with nothing to hand-maintain.
 */
void alloy_udev_rules_write(FILE *out);

#endif /* ALLOY_UDEV_H */
