.. SPDX-License-Identifier: GPL-2.0-only
.. Copyright (C) 2026 Szymon Wilczek

========
alloyctl
========

------------------------------------------------------
Linux CLI to control and configure SteelSeries devices
------------------------------------------------------

SYNOPSIS
========

**alloyctl** [*OPTIONS*]

DESCRIPTION
===========

**alloyctl** is a lightweight, dependency-free configuration tool and full-screen
terminal user interface for SteelSeries mice and keyboards on Linux.

When invoked without device-modifying arguments, **alloyctl** discovers connected
devices and launches an interactive ncurses-based dashboard allowing live adjustments
of sensor resolution (CPI/DPI), USB polling rates, per-zone RGB illumination,
button remapping, Windows/Meta key lock, and onboard flash persistence.

When invoked with command-line flags, **alloyctl** performs the requested
configuration changes non-interactively, applies them to the target hardware,
updates the local baseline state, and exits immediately.

OPTIONS
=======

General Options
---------------

-h, --help
    Display a summary of available command-line options, descriptions, and examples,
    then exit.

-v, --version
    Display version information and exit.

-l, --list
    List all supported SteelSeries device models, device types (mouse or keyboard),
    and their corresponding USB Vendor and Product IDs (VID:PID).

-d, --device <VID:PID>
    Target a specific connected device identified by hexadecimal Vendor ID and
    Product ID (for example, ``1038:1824``). If omitted, **alloyctl** auto-selects
    the connected device or presents an interactive chooser if multiple devices are attached.

--dump-udev
    Generate standard udev rules granting unprivileged read/write access to
    supported ``/dev/hidraw*`` nodes for all registered drivers, then exit.

Common Configuration Options
----------------------------

These options apply to both mouse and keyboard devices.

--brightness <0-100>
    Set global LED illumination brightness percentage (0 to 100).

--polling <hz>
    Set the USB report rate in Hertz (for example, 125, 250, 500, or 1000 Hz).

--color <[zone:]hex>
    Set illumination color using a 6-character hex code (``RRGGBB``, optionally
    prefixed with ``#``) or CSS 3-character shorthand (``RGB``). An optional zero-based
    zone index may prefix the hex code to target a specific LED zone (for example,
    ``0:FF0000`` for zone 0 red, or ``00FF88`` for all zones).

--fx <[zone:]mode>
    Set lighting effect mode. Supported values include ``steady`` / ``static`` (mode 0),
    ``breath`` / ``breathing`` (mode 1), or a driver-defined numeric mode ID. An optional
    zero-based zone index may prefix the mode (for example, ``1:breath``).

--save
    Commit the live configuration settings to the device's onboard flash memory so
    they persist across power cycles and system reboots.

Mouse Configuration Options
---------------------------

These options apply exclusively to mouse devices. Passing them when targeting a keyboard
will result in an error.

--dpi <cpi>, --cpi <cpi>
    Set the primary sensor CPI (counts per inch) resolution. The value is automatically
    clamped to the sensor's supported minimum, maximum, and step size.

--accel <0-100>
    Configure host-side pointer acceleration curve intensity (0 to 100).

--decel <0-100>
    Configure host-side pointer deceleration curve intensity (0 to 100).

--snap <0-45>
    Set host-side angle snapping threshold in degrees (0 to disable).

--high-efficiency [on|off]
    Toggle the wireless power-saving high-efficiency mode (supported wireless devices only).

--accel-daemon <VID:PID>
    Launch the host-side evdev pointer transform daemon in the background for the specified
    device.

--accel-stop <VID:PID>
    Terminate any running host-side pointer transform daemon for the specified device.

Keyboard Configuration Options
------------------------------

These options apply exclusively to keyboard devices. Passing them when targeting a mouse
will result in an error.

--meta-lock [on|off], --win-lock [on|off]
    Enable or disable the Windows / Meta key lock. When enabled (``on`` or ``1``),
    accidental Windows key presses during gaming are suppressed in hardware.

INTERACTIVE TUI MODE
====================

Running **alloyctl** with no configuration arguments launches the full-screen terminal interface.

Keybindings
-----------

- **Tab / Shift-Tab**: Navigate between panes.
- **Up / Down / j / k**: Navigate settings, presets, and color palettes within the active pane.
- **Space / Enter**: Toggle checkboxes, activate presets, or select buttons.
- **S**: Save live configuration to device onboard flash.
- **Q / Ctrl-C**: Quit **alloyctl**.

CONFIGURATION FILES
===================

State files are stored in INI format under:

    ``$XDG_CONFIG_HOME/alloyctl/<vendor>_<product>.conf``
    (defaults to ``~/.config/alloyctl/<vendor>_<product>.conf``)

These files preserve user settings across sessions and configure parameters such as
active CPI levels, per-zone illumination colors, polling rates, and button bindings.

EXIT STATUS
===========

0
    Success. Requested command, configuration change, or TUI session completed cleanly.

1
    Error. Invalid command-line argument, invalid option value, unsupported hardware
    capability, device-type mismatch, or missing permission to access ``/dev/hidraw*`` / ``/dev/uinput``.

130
    Interactive device selection menu was cancelled by user (SIGINT / Esc / Q).

EXAMPLES
========

Launch interactive TUI:
    $ alloyctl

Set DPI to 1600 and polling rate to 1000 Hz on Rival 3:
    $ alloyctl --device 1038:1824 --dpi 1600 --polling 1000 --save

Set illumination brightness to 50% and enable Windows key lock on keyboard:
    $ alloyctl --brightness 50 --meta-lock on

Set LED zone 0 to green (``#00FF00``) with static effect:
    $ alloyctl --color 0:00FF00 --fx 0:steady

Generate udev rules for unprivileged device access:
    $ alloyctl --dump-udev | sudo tee /usr/lib/udev/rules.d/71-alloyctl-hidraw.rules
    $ sudo udevadm control --reload && sudo udevadm trigger

SEE ALSO
========

**udev**\ (7), **udevadm**\ (8), **evdev**\ (4)

Online documentation: <https://github.com/szymonwilczek/alloyctl>
