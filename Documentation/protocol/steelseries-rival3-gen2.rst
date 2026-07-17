.. SPDX-License-Identifier: GPL-2.0-only
.. Copyright (C) 2026 Szymon Wilczek

========================================
SteelSeries Rival 3 Gen 2 (wired) -- HID
========================================

Device: ``1038:1870``, USB 2.0 full speed, ``bcdDevice 1.01``.

Findings below were obtained by reverse engineering on real hardware (hidraw
probing, HID report-descriptor analysis) and cross-checked against the
`rivalcfg <https://github.com/flozz/rivalcfg>`_ project which added support for
this device in v4.16.0.

USB interfaces
==============

The mouse exposes four HID interfaces:

.. list-table::
   :header-rows: 1
   :widths: 8 10 16 66

   * - iface
     - hidraw
     - usage page
     - purpose
   * - 0
     - input0
     - ``0x01``
     - mouse input (8 buttons, 16-bit X/Y, wheel)
   * - 1
     - input1
     - ``0x01/0x0C``
     - keyboard + consumer control (media keys)
   * - 2
     - input2
     - ``0xFF00``
     - vendor, 64 B input / 64 B output
   * - 3
     - input3
     - ``0xFFC0``
     - **configuration**: 64 B in/out, 514 B feature

All configuration traffic goes through **interface 3** using HID output
reports. The device does not use numbered reports: a raw ``write()`` on the
hidraw node must be prefixed with a ``0x00`` report-number byte followed by the
64-byte payload (65 bytes total).

Command acknowledgement
=======================

Every *recognized* command is acknowledged on the interrupt IN endpoint of
interface 3 with a 64-byte packet that echoes the command byte (e.g. sending
``0x21 ...`` yields ``21 00 00 ...``). Unrecognized commands produce **no**
response. This makes the ACK a reliable "command accepted" signal and a safe
way to detect protocol support.

Commands (output reports, interface 3)
======================================

``0x34`` -- DPI presets
-----------------------

::

   0x34 <count> <active> <x1> <y1> [<x2> <y2> ...]

* ``count``  -- number of presets being configured (1--5)
* ``active`` -- 1-based index of the initially active preset
* ``xN yN``  -- per-axis sensor step bytes (see table below)

DPI range 200--8500 in steps of 100. The sensor is a TrueMove Core; DPI does
not map linearly to the wire byte. Formula approximation:
``byte ~= round(dpi / 43.1)``, exact values in the table used by the driver
(200 -> ``0x04``, 800 -> ``0x12``, 1600 -> ``0x24``, ..., 8500 -> ``0xC5``).

``0x2B`` -- polling rate
------------------------

::

   0x2B <rate>

====  ======
Hz    byte
====  ======
1000  0x01
500   0x02
250   0x03
125   0x04
====  ======

``0x21`` -- zone colors
-----------------------

::

   0x21 <mask> <R1> <G1> <B1> <R2> <G2> <B2> <R3> <G3> <B3>

Three RGB zones on the illumination strip:

* bit0 (``0b001``) -- zone 1, strip **top**
* bit1 (``0b010``) -- zone 2, strip **middle**
* bit2 (``0b100``) -- zone 3, strip **bottom**

The RGB triplets are positional: zone N always reads its color from triplet N
regardless of the mask, the mask only selects which zones latch the value.
Setting a static color clears the rainbow effect on the selected zones.

Verified on hardware: per-zone updates work (e.g.
``0x21 0x02 000000 FFFFFF`` updates only the middle zone).

There is **no separate logo LED** on the Gen 2 -- the Gen 1 logo zone was
replaced by the 3-zone bottom strip.

``0x23`` -- LED brightness
--------------------------

::

   0x23 <brightness>          ; 0x00-0x64 (0-100 %)

``0x22`` -- rainbow effect
--------------------------

::

   0x22 <mask>                ; same zone mask as 0x21

``0x26`` -- reactive color
--------------------------

::

   0x26 <R> <G> <B>           ; color flashed on button click

All-zero payload disables the effect.

``0x27`` -- default lighting at power-up
----------------------------------------

::

   0x27 <rainbow> <reactive>  ; each 0x00 or 0x01

``0x2A`` -- button mapping
--------------------------

::

   0x2A <field1[5]> <field2[5]> ... <field8[5]>

Eight 5-byte fields at fixed offsets, one per physical control:

======  ===========  ===================
offset  control      default action
======  ===========  ===================
0x00    Button 1     left click (0x01)
0x05    Button 2     right click (0x02)
0x0A    Button 3     middle (0x03)
0x0F    Button 4     back (0x04)
0x14    Button 5     forward (0x05)
0x19    Button 6     DPI toggle (0x30)
0x1E    Scroll up    0x31
0x23    Scroll down  0x32
======  ===========  ===================

First byte of each field selects the action:

* ``0x00`` -- disabled
* ``0x01``--``0x06`` -- mouse button N
* ``0x30`` -- DPI cycle
* ``0x31`` / ``0x32`` -- scroll up / down
* ``0x51 <keycode>`` -- keyboard key (standard USB HID usage ID)
* ``0x61 <code>`` -- multimedia key

``0x11 0x00`` -- save to onboard flash
--------------------------------------

Commits the current live configuration to persistent memory. Without it every
change is live-only and lost on replug -- which is exactly what a live preview
needs.

``0x90`` -- firmware version (discovered on hardware)
-----------------------------------------------------

::

   0x90

Responds with ``0x90`` followed by an ASCII string, e.g. ``"1.1.6 +e57ff6a1"``.
Found by probing the device.

Read-back
=========

No command to read the current configuration back from the device has been
found (``0x10``, ``0x12``, ``0x92`` all go unanswered). Consequence for
alloyctl: the pre-session baseline used by REVERT is kept in a host-side state
file, seeded with driver defaults on first run.

Not supported by this hardware
==============================

Acceleration / deceleration and angle snapping are not exposed by this protocol
family (no known command; SteelSeries Engine does not offer them for TrueMove
Core sensors either). Drivers advertise these capabilities per-device and the
UI disables the sections when absent.
