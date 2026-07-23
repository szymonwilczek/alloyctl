# SPDX-License-Identifier: GPL-2.0-only
# Build alloyctl from source into the Nix store
{
  lib,
  stdenv,
  pkg-config,
  ncurses,
}:

stdenv.mkDerivation (finalAttrs: {
  pname = "alloyctl";
  version = lib.fileContents ../VERSION;

  src = ../.;

  nativeBuildInputs = [ pkg-config ];
  buildInputs = [ ncurses ];

  # PREFIX drives BINDIR;
  # UDEVDIR is absolute in the Makefile, so override it
  # to land under the store path instead of /usr/lib
  makeFlags = [
    "PREFIX=${placeholder "out"}"
    "UDEVDIR=${placeholder "out"}/lib/udev/rules.d"
  ];

  enableParallelBuilding = true;

  doCheck = true;
  checkTarget = "test";

  meta = {
    description = "Linux CLI/TUI to control SteelSeries mice";
    homepage = "https://github.com/szymonwilczek/alloyctl";
    license = lib.licenses.gpl2Only;
    mainProgram = "alloyctl";
    platforms = lib.platforms.linux;
  };
})
