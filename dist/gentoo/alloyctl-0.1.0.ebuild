# Copyright 2026 Szymon Wilczek
# Distributed under the terms of the GNU General Public License v2

# Drop this into overlay as app-misc/alloyctl and run `pkgdev manifest`
# to generate its Manifest;
# bump the version by renaming the file (${PV} tracks the tag)

EAPI=8

inherit toolchain-funcs udev

DESCRIPTION="Linux CLI/TUI to control SteelSeries mice"
HOMEPAGE="https://github.com/szymonwilczek/alloyctl"
SRC_URI="https://github.com/szymonwilczek/alloyctl/archive/refs/tags/v${PV}.tar.gz -> ${P}.tar.gz"

LICENSE="GPL-2"
SLOT="0"
KEYWORDS="~amd64 ~arm64"

# TUI links the wide-character ncurses;
# := triggers rebuild on soname bumps
RDEPEND=">=sys-libs/ncurses-6:=[unicode(+)]"
DEPEND="${RDEPEND}"
BDEPEND="virtual/pkgconfig"

src_compile() {
	# Makefile appends its own required flags to CFLAGS via +=,
	# so the environment's CFLAGS still ride along;
	# hand it Gentoo's compiler
	emake CC="$(tc-getCC)"
}

src_install() {
	# install target lays out the binary and both udev rules,
	# regenerating the per-device hidraw rule from the just-built binary;
	# point it at ${D} and Gentoo's udev directory
	emake install DESTDIR="${D}" PREFIX="/usr" UDEVDIR="$(get_udevdir)/rules.d"

	dodoc README.rst
}

pkg_postinst() {
	udev_reload
	elog "On non-logind systems, add yourself to the 'input' group for"
	elog "/dev/hidraw*, /dev/input and /dev/uinput access:"
	elog "  usermod -aG input <user>"
}

pkg_postrm() {
	udev_reload
}
