# SPDX-License-Identifier: GPL-2.0-only
#
# NixOS cannot consume the .deb/.rpm/AUR artifacts:
# so Nix gets its own path - alloyctl built from source into the store, plus NixOS
# module that installs the udev rules the supported way (services.udev.packages)
#
# Use it:
#   nix run     github:szymonwilczek/alloyctl
#   nix build   github:szymonwilczek/alloyctl
#
# On NixOS, add the module and enable it:
#   imports = [ inputs.alloyctl.nixosModules.default ];
#   programs.alloyctl.enable = true;
{
  description = "Linux CLI to control SteelSeries mice";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs =
    { self, nixpkgs }:
    let
      systems = [
        "x86_64-linux"
        "aarch64-linux"
      ];
      forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f nixpkgs.legacyPackages.${system});
    in
    {
      packages = forAllSystems (pkgs: rec {
        alloyctl = pkgs.callPackage ./nix/package.nix { };
        default = alloyctl;
      });

      nixosModules.default = import ./nix/module.nix;

      devShells = forAllSystems (pkgs: {
        default = pkgs.mkShell {
          inputsFrom = [ self.packages.${pkgs.system}.alloyctl ];
        };
      });
    };
}
