.. SPDX-License-Identifier: GPL-2.0-only
.. Copyright (C) 2026 Szymon Wilczek

====================================
SteelSeries Rival 600 (wired) -- HID
====================================

Devices: ``1038:1724`` (SteelSeries Rival 600) and ``1038:172E`` (SteelSeries Rival 600 Dota 2 Edition).

Cross-checked against the `rivalcfg <https://github.com/flozz/rivalcfg>`_
``rival600`` profile.

.. note:: This driver implementation was ported from rivalcfg specifications
          and is awaiting testing on physical hardware.

USB interfaces
==============

Configuration goes to interface 0 as unnumbered HID reports.

Command acknowledgement: none
=============================

Commands on output reports are sent fire-and-forget without command echo on the
interrupt IN endpoint.

Commands (output reports, interface 0)
======================================

``0x03 0x00`` -- DPI presets
----------------------------

::

   0x03 0x00 0x01 <v1> 0x00 0x42   # preset 1
   0x03 0x00 0x02 <v2> 0x00 0x42   # preset 2

Values correspond to the TrueMove 3+ sensor (100--12000 DPI in steps of 100,
mapped linearly as ``(dpi - 100) / 100`` from ``0x00`` to ``0x77``).

``0x04 0x00`` -- polling rate
-----------------------------

::

   0x04 0x00 <rate>

* ``0x01`` -- 1000 Hz
* ``0x02`` -- 500 Hz
* ``0x03`` -- 250 Hz
* ``0x04`` -- 125 Hz

``0x05 0x00`` -- zone illumination (8 zones)
--------------------------------------------

::

   0x05 0x00 <led_id> [0x00 x 4] <led_id> <duration_le16> [0x00 x 14] 0x01 [0x00 x 4] 0x01 <R> <G> <B>

Configures lighting for each of the 8 RGB zones:

* ``0x00`` -- Wheel LED
* ``0x01`` -- Logo LED
* ``0x02`` -- Left Strip, Top LED
* ``0x03`` -- Right Strip, Top LED
* ``0x04`` -- Left Strip, Middle LED
* ``0x05`` -- Right Strip, Middle LED
* ``0x06`` -- Left Strip, Bottom LED
* ``0x07`` -- Right Strip, Bottom LED

``0x31 0x00`` -- buttons mapping
--------------------------------

::

   0x31 0x00 <b1_5B> ... <b7_5B>

Each of the 7 button fields is 5 bytes (35 bytes total):

* Byte 0: action type (``0x01``-``0x05`` for mouse buttons, ``0x30`` for CPI cycle, ``0x31`` for scroll up, ``0x32`` for scroll down, ``0x51`` for keyboard, ``0x61`` for media, ``0x00`` for disabled).
* Byte 1: keycode or media code when keyboard/multimedia action is selected.
* Bytes 2--4: padding (``0x00``).

Default mappings:

* Button 1: Left Click (``0x01``)
* Button 2: Right Click (``0x02``)
* Button 3: Middle Click (``0x03``)
* Button 4: Side Back (``0x04``)
* Button 5: Side Forward (``0x05``)
* Button 6: Side Front (Disabled, ``0x00``)
* Button 7: CPI Cycle (``0x30``)

``0x09 0x00`` -- save to flash
------------------------------

::

   0x09 0x00

Persists the live configuration into onboard flash memory.
