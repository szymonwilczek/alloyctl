#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
#
# Reload udev so the freshly installed rules take effect without a reboot.
# Runs after install/upgrade from the .deb and .rpm packages.
# Best effort: container or build chroot may have no running udev.
set -eu

if command -v udevadm >/dev/null 2>&1; then
	udevadm control --reload >/dev/null 2>&1 || true
	udevadm trigger >/dev/null 2>&1 || true
fi

echo "alloyctl: installed. On non-logind systems, add yourself to the 'input'"
echo "group for /dev/hidraw*, /dev/input and /dev/uinput access:"
echo "  sudo usermod -aG input \$USER"

exit 0
