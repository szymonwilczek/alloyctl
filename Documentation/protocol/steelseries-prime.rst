.. SPDX-License-Identifier: GPL-2.0-only
.. Copyright (C) 2026 Szymon Wilczek

=====================================
SteelSeries Prime (wired) -- HID
=====================================

Devices: ``1038:182E`` (SteelSeries Prime), ``1038:182A`` (SteelSeries Prime
Rainbow 6 Siege Black Ice Edition), and ``1038:1856`` (SteelSeries Prime CS:GO
Neo Noir Edition).

Cross-checked against the `rivalcfg <https://github.com/flozz/rivalcfg>`_
``prime`` profile.

USB interfaces
==============

Configuration goes to interface 0 as unnumbered HID reports.

Command acknowledgement: none
=============================

Commands on output reports are sent fire-and-forget without command echo on the
interrupt IN endpoint.

Commands (output reports, interface 0)
======================================

``0x61`` -- DPI presets
-----------------------

::

   0x61 <count> <active> <v1_le16> [<v2_le16> ...]

Supports 1 to 5 presets. Values are 2-byte little endian integers mapped as
``dpi / 50`` for the TrueMove Pro sensor (50--18000 DPI in steps of 50).
``active`` is the 0-based index of the currently selected preset.

``0x5D`` -- polling rate
------------------------

::

   0x5D <rate>

* ``0x01`` -- 1000 Hz
* ``0x02`` -- 500 Hz
* ``0x03`` -- 250 Hz
* ``0x04`` -- 125 Hz

``0x62 0x01`` -- wheel LED color
--------------------------------

::

   0x62 0x01 <R> <G> <B> [15 x 0x00] 0xFF

Sets the RGB color for the scroll wheel LED.

``0x5F`` -- wheel LED brightness
--------------------------------

::

   0x5F <wire_le16>

16-bit little endian value in range 0--256 (0 = off, 256 = 100% brightness).

``0x5B`` -- buttons mapping
---------------------------

::

   0x5B <b1_5B> ... <b6_5B>

Each of the 6 button fields is 5 bytes:

* Byte 0: action type (``0x01``-``0x05`` for mouse buttons, ``0x30`` for CPI cycle,
  ``0x31`` for scroll up, ``0x32`` for scroll down, ``0x51`` for keyboard,
  ``0x61`` for media, ``0x00`` for disabled).
* Byte 1: keycode or media code when keyboard/multimedia action is selected.
* Bytes 2--4: padding (``0x00``).

Default mappings:

* Button 1: Left Click (``0x01``)
* Button 2: Right Click (``0x02``)
* Button 3: Middle Click (``0x03``)
* Button 4: Side Back (``0x04``)
* Button 5: Side Forward (``0x05``)
* Button 6: CPI Cycle (``0x30``)

``0x59`` -- save to flash
-------------------------

::

   0x59

Persists the live configuration into onboard flash memory.
