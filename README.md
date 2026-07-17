# alloyctl

Full-screen terminal replacement for SteelSeries Engine on Linux,
written in C. Think lazygit, but for your mouse: panes, modals, live
preview, and every setting the hardware really has.

## Supported hardware

| Device                    | USB ID      | Status |
|---------------------------|-------------|--------|
| SteelSeries Rival 3 Gen 2 | `1038:1870` | full   |

Mice only for now. Want yours supported? See
[docs/adding-a-driver.md](docs/adding-a-driver.md) - one file, one
mouse, community drivers are the whole point of this project.

## Features

* **Button remapping** - every physical control, including scroll
  directions, to mouse buttons, CPI toggle, keyboard keys, or off.
* **Per-zone RGB** - each LED zone addressed individually, not one
  color smeared over everything. The color picker modal offers R/G/B
  steppers, a preset palette and hex entry, all previewed live on the
  hardware.
* **Hardware lighting effects** - everything the firmware can run on
  its own: per-zone rainbow cycling, reactive click color, and the
  power-up lighting choice. Drivers declare what their mouse
  supports; nothing is emulated host-side.
* **Two CPI presets** on interval sliders across the sensor's real
  range, plus the active-preset toggle.
* **Polling rate** stepper with a live waveform.
* **Live preview** - changes hit the mouse instantly but are not
  persisted; **SAVE** commits to onboard flash, **REVERT** restores
  your session baseline. Settings your hardware lacks are shown as
  `N/A (device)`, never faked.

## Building

```sh
make            # needs gcc/clang, ncursesw, pkg-config
make test       # unit tests, no hardware required
./alloyctl
```

No root needed as long as your `/dev/hidraw*` nodes are writable by
your user (most desktop distributions handle this via udev already;
otherwise add a udev rule for your mouse's VID/PID).

## Keys

| Key         | Action                        |
|-------------|-------------------------------|
| Tab / S-Tab | cycle panes                   |
| j/k, arrows | move within a pane            |
| h/l         | adjust stepper (H/L: fast)    |
| a           | set active CPI preset         |
| Enter       | open modal / press button     |
| Esc         | close modal                   |
| q           | quit                          |

## Design notes

* Protocol documentation from reverse engineering lives under
  [docs/protocol/](docs/protocol/).
* The mice provide no configuration read-back, so REVERT restores the
  baseline persisted under `~/.config/alloyctl/` (driver defaults on
  the very first run) - your defaults, not the application's.
* Code style is the Linux kernel's, enforced by `.clang-format` and
  CI. Commits follow `feat:`/`fix:` with a DCO sign-off.

## License

GPL-2.0-only. See [LICENSE](LICENSE).
