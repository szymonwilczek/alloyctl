#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
#
# Reload udev after the rules are removed on uninstall.
# Best effort: container or build chroot may have no running udev.
set -eu

if command -v udevadm >/dev/null 2>&1; then
	udevadm control --reload >/dev/null 2>&1 || true
	udevadm trigger >/dev/null 2>&1 || true
fi

exit 0
