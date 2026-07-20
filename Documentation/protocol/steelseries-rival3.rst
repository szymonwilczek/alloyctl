.. SPDX-License-Identifier: GPL-2.0-only
.. Copyright (C) 2026 Szymon Wilczek

================================================
SteelSeries Rival 3 (Gen 1, wired) -- HID
================================================

Devices: ``1038:1824`` and ``1038:184C`` (firmware v0.37+ revision, same
protocol; findings below verified on real ``184C`` hardware).

Cross-checked against the `rivalcfg <https://github.com/flozz/rivalcfg>`_
``rival3`` profile; the ACK behaviour, report size and firmware response format
were confirmed by probing.

USB interfaces
==============

Same four-interface layout as the Gen 2 (mouse input, keyboard, vendor
``0xFF00``, vendor ``0xFFC0``), but the vendor reports are **32 bytes**, not 64.
Configuration goes to interface 3 as unnumbered HID output reports: ``write()``
= ``0x00`` report number + 32-byte payload (33 bytes total).

Command acknowledgement: none
=============================

Unlike the Gen 2, this firmware does **not** echo commands on the interrupt IN
endpoint. Only the firmware version query answers. Consequence: the driver
sends fire-and-forget and cannot detect whether a command was accepted.

Commands (output reports, interface 3)
======================================

Every command starts with ``<cmd> 0x00``.

``0x0B 0x00`` -- DPI presets
----------------------------

::

   0x0B 0x00 <count> <active> <v1> [<v2> ...]

One byte per preset (no separate X/Y), same TrueMove Core table as the Gen 2
(200--8500 DPI, step 100, ``byte ~= dpi / 43.1``). ``active`` is 1-based.

As on the Gen 2, the presets are onboard **CPI levels** only: the CPI button
cycles the active CPI value, while colors, effects, polling rate and button
bindings are one global configuration shared by every level. Pressing the
button flashes a fixed, firmware-chosen indicator color for the new level
before the configured lighting returns -- a level indicator, not per-level
lighting.

``0x04 0x00`` -- polling rate
-----------------------------

``0x04 0x00 <rate>`` with the same encoding as Gen 2: 1000 Hz -> ``0x01``,
500 -> ``0x02``, 250 -> ``0x03``, 125 -> ``0x04``.

``0x05 0x00`` -- zone color (one zone per write)
------------------------------------------------

::

   0x05 0x00 <zone> <R> <G> <B> <brightness>

* zone ``0x01`` -- strip top
* zone ``0x02`` -- strip middle
* zone ``0x03`` -- strip bottom
* zone ``0x04`` -- **logo**
* zone ``0x00`` -- all zones at once

``brightness`` is ``0x00``-``0x64`` (0-100 %) and rides in every color write --
there is no separate global brightness command. Setting a color while an effect
runs switches that back to steady behaviour on the next effect write, so
alloyctl orders effect-first, colors-second.

``0x06 0x00`` -- global light effect
------------------------------------

::

   0x06 0x00 <effect>

==============  ======
effect          byte
==============  ======
rainbow shift   0x00
breath fast     0x01
breath          0x02
breath slow     0x03
steady          0x04
rainbow breath  0x05
disco           0x06
==============  ======

Global (all zones), unlike the Gen 2 per-zone rainbow mask. The breathing modes
pulse the configured zone colors.

``0x07 0x00`` -- button mapping
-------------------------------

::

   0x07 0x00 <field1[2]> ... <field8[2]>

Eight **2-byte** fields (Gen 2 uses 5-byte fields) in the order
Button1..Button6, ScrollUp, ScrollDown. First byte selects the action, second
carries the keycode where applicable:

* ``0x00`` -- disabled
* ``0x01``-``0x06`` -- mouse button N
* ``0x30`` -- DPI cycle
* ``0x31`` / ``0x32`` -- scroll up / down
* ``0x33 <keycode>`` -- keyboard key (Gen 2 uses 0x51)
* ``0x34 <code>`` -- multimedia key (Gen 2 uses 0x61)

``0x09 0x00`` -- save to onboard flash
--------------------------------------

Commits the live configuration to persistent memory. One save commits
everything: the flash configuration is global (single set of colors, effects,
polling and bindings plus the CPI level table); there are no per-level
profile slots.

``0x10 0x00`` -- firmware version
---------------------------------

Responds with two raw bytes ``<major> <minor>``; the probed unit answered
``0x27 0x00`` -> version 39.0.

Not supported by this hardware
==============================

Per-zone rainbow masks, reactive click color and the power-up lighting selector
are Gen 2 features; this firmware has the global effect list instead.

Acceleration / deceleration / angle snapping are not onboard here either, as on
the Gen 2: SteelSeries applies them host-side, not in firmware, and alloyctl
does the same via its pointer-transform daemon (see
:doc:`../architecture/pointer-transform`).
