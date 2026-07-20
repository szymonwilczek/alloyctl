#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
#
# Standalone installer shipped inside the release tarball,
# so binary download works without cloning the source tree or running `make install`
#
# It installs two things:
#   - the alloyctl binary (which is also the pointer-transform daemon:
#     the TUI re-executes it with --accel-daemon),
#   - the udev rule that grants that daemon access to /dev/uinput and
#     the mouse's evdev node.
#
# Autostart entries are per-device and created at runtime when the user
# enables the engine, so nothing is installed for them here.
#
# Usage (from the unpacked tarball):
#   sudo ./install.sh                 # install to /usr/local
#   sudo ./install.sh --prefix /usr
#   sudo ./install.sh --uninstall
set -eu

PREFIX="${PREFIX:-/usr/local}"
DESTDIR="${DESTDIR:-}"
UDEVDIR="${UDEVDIR:-/usr/lib/udev/rules.d}"
RULE="70-alloyctl-uinput.rules"

action="install"
while [ $# -gt 0 ]; do
	case "$1" in
	--prefix)
		PREFIX="${2:?--prefix needs a path}"
		shift 2
		;;
	--prefix=*)
		PREFIX="${1#--prefix=}"
		shift
		;;
	--uninstall)
		action="uninstall"
		shift
		;;
	-h | --help)
		sed -n '3,26p' "$0"
		exit 0
		;;
	*)
		echo "install.sh: unknown argument '$1'" >&2
		exit 2
		;;
	esac
done

BINDIR="$PREFIX/bin"

# Resolve paths relative to this script so it works from any working directory.
# shellcheck disable=SC1007 # empty CDPATH scoped to this cd is intentional
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

if [ "$(id -u)" -ne 0 ]; then
	echo "install.sh: run as root (e.g. sudo ./install.sh)" >&2
	exit 1
fi

reload_udev() {
	# best effort:
	# container or build chroot may have no running udev
	if command -v udevadm >/dev/null 2>&1; then
		udevadm control --reload && udevadm trigger || true
	fi
}

if [ "$action" = "uninstall" ]; then
	rm -f "$DESTDIR$BINDIR/alloyctl"
	rm -f "$DESTDIR$UDEVDIR/$RULE"
	reload_udev
	echo "Removed alloyctl and the uinput udev rule."
	exit 0
fi

if [ ! -f "$here/alloyctl" ]; then
	echo "install.sh: alloyctl binary not found next to this script" >&2
	exit 1
fi
if [ ! -f "$here/$RULE" ]; then
	echo "install.sh: $RULE not found next to this script" >&2
	exit 1
fi

install -Dm755 "$here/alloyctl" "$DESTDIR$BINDIR/alloyctl"
install -Dm644 "$here/$RULE" "$DESTDIR$UDEVDIR/$RULE"
reload_udev

echo
echo "Installed alloyctl to $DESTDIR$BINDIR and the uinput udev rule."
echo "If udev was not reloaded above, run:"
echo "  sudo udevadm control --reload && sudo udevadm trigger"
echo "On non-logind systems, add your user to the 'input' group for"
echo "/dev/input and /dev/uinput access:  sudo usermod -aG input \$USER"
