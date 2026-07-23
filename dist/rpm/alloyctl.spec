# SPDX-License-Identifier: GPL-2.0-only
#
# Source RPM spec for COPR / Fedora dist-git.
# This is the from-source build channel.
Name:           alloyctl
Version:        0.1.0
Release:        1%{?dist}
Summary:        Linux CLI/TUI to control SteelSeries mice

License:        GPL-2.0-only
URL:            https://github.com/szymonwilczek/alloyctl
Source0:        %{url}/archive/refs/tags/v%{version}/%{name}-%{version}.tar.gz

BuildRequires:  gcc
BuildRequires:  make
BuildRequires:  ncurses-devel
BuildRequires:  pkgconfig
# provides the %%{_udevrulesdir} macro used below
BuildRequires:  systemd-rpm-macros

# ncursesw runtime, named per family;
# boolean dep so the same spec covers Fedora/RHEL
# (ncurses-libs) and openSUSE (libncurses6)
Requires:       (ncurses-libs or libncurses6)

%description
Full-screen terminal replacement for SteelSeries Engine: DPI, polling rate,
lighting, button mapping and more, for every setting the hardware really
exposes. One portable binary, community-driven per-mouse drivers.

%prep
%autosetup

%build
%make_build

%install
# install target also regenerates the per-device hidraw rule from the just-built binary;
# point it at the buildroot and the system udev dir
%make_install PREFIX=%{_prefix} UDEVDIR=%{_udevrulesdir}

%files
%license LICENSE
%doc README.rst
%{_bindir}/alloyctl
%{_udevrulesdir}/70-alloyctl-uinput.rules
%{_udevrulesdir}/71-alloyctl-hidraw.rules

%changelog
* Fri Jul 24 2026 Szymon Wilczek <swilczek.lx@gmail.com> - 0.1.0-1
- Initial COPR/Fedora packaging.
