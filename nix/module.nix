# SPDX-License-Identifier: GPL-2.0-only
#
# NixOS module:
#   installs alloyctl and, crucially, registers its udev rules the way NixOS
#   actually reads them.
#   Package dropping files in /usr/lib/udev is ignored on NixOS;
#   services.udev.packages is the supported hook, and it picks up
#   $out/lib/udev/rules.d/*.rules that nix/package.nix produced.
{
  config,
  lib,
  pkgs,
  ...
}:

let
  cfg = config.programs.alloyctl;
in
{
  options.programs.alloyctl = {
    enable = lib.mkEnableOption "alloyctl, a TUI to control SteelSeries mice";

    package = lib.mkOption {
      type = lib.types.package;
      default = pkgs.callPackage ./package.nix { };
      defaultText = lib.literalExpression "pkgs.callPackage ./package.nix { }";
      description = "The alloyctl package to install.";
    };
  };

  config = lib.mkIf cfg.enable {
    environment.systemPackages = [ cfg.package ];
    services.udev.packages = [ cfg.package ];
  };
}
