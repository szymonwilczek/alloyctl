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
* ``active`` -- 0-based index of the initially active preset (same encoding
  as the ``0xAD`` notification below; sending it 1-based selected the next
  level, which SAVE then latched to flash -- see #41)
* ``xN yN``  -- per-axis sensor step bytes (see table below)

DPI range 200--8500 in steps of 100. The sensor is a TrueMove Core; DPI does
not map linearly to the wire byte. Formula approximation:
``byte ~= round(dpi / 43.1)``, exact values in the table used by the driver
(200 -> ``0x04``, 800 -> ``0x12``, 1600 -> ``0x24``, ..., 8500 -> ``0xC5``).

The presets are onboard **CPI levels**, nothing more: the CPI button cycles
only the active CPI value. Colors, effects, polling rate and button bindings
are a single global configuration shared by every level (verified on
hardware: after a save, cycling the CPI button changes CPI only).

Pressing the CPI button also makes the firmware flash a fixed, per-level
indicator color before the configured lighting returns. That color is chosen
by the firmware and is not configurable -- it is a level indicator, not
per-level lighting (and is easily mistaken for stored per-profile colors).

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

Hardware-verified (#23): the mask only **enrolls zones into an already
running rainbow engine** - the engine itself is the rainbow byte of ``0x27``.
While that byte is ``0x00`` the device ignores ``0x22`` entirely (ACKed, no
visible effect), even on zones freshly lit by ``0x21``. Turning the engine on
(re)enrolls every zone; masked ``0x21`` writes then carve static zones out of
the cycle.

``0x26`` -- reactive color
--------------------------

::

   0x26 <enable> 0x00 <R> <G> <B>   ; color flashed on button click

``enable`` is ``0x01`` to arm the flash, ``0x00`` (with a zero color) to
disable it. All hardware-verified (fw ``1.1.6``):

* The flash **overlays** whatever the zones currently show - static colors,
  the rainbow cycle or unlit zones alike.
* The enable byte is mandatory. A short ``0x26 <R> <G> <B>`` write is still
  ACKed, but the firmware then latches a **black** flash - the effect looks
  completely dead while every command "succeeds" (the #24 trap).
* The color is **not persisted** by ``0x11`` and resets on power-up, so the
  host must re-arm it after every replug.

``0x27`` -- default lighting at power-up
----------------------------------------

::

   0x27 <rainbow> <reactive>  ; each 0x00 or 0x01

Despite the name, the mode also applies **immediately** when written, not
only at the next power-up (verified: writing ``0x27 0x01 0x00`` starts the
rainbow on the spot, ``0x27 0x00 0x01`` turns the zones off live). At wake-up
the reactive bit lights nothing by itself - it only selects whether clicking
triggers the flash before the host reconfigures the zones; once any ``0x21``/
``0x22`` write lands, the ``0x26`` overlay works in every mode regardless.

The rainbow byte is in fact the **live master switch of the rainbow engine**
(#23): ``0x22`` masks only work while it is set, and turning it on enrolls
every zone in the cycle. Startup preference and running engine share this
one flag - a configuration with rainbow zones therefore necessarily wakes up
cycling too; the firmware cannot store "startup off" together with a live
rainbow. alloyctl derives the effective byte as "any rainbow zone OR the
startup choice" and sends ``0x27`` **first** in every lighting apply, before
the ``0x22`` mask and the ``0x21`` statics.

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

One save commits everything: the flash configuration is global (single set of
colors, effects, polling and bindings plus the CPI level table); there are no
per-level profile slots to iterate.

``0x90`` -- firmware version (discovered on hardware)
-----------------------------------------------------

::

   0x90

Responds with ``0x90`` followed by an ASCII string, e.g. ``"1.1.6 +e57ff6a1"``.
Found by probing the device.

Unsolicited events (interface 2)
================================

The vendor interface (``0xFF00``, 64-byte input reports) streams
device-initiated notifications. One is known, discovered on hardware
(fw ``1.1.6 +e57ff6a1``):

``0xAD`` -- CPI level switch
----------------------------

::

   0xAD <count> <active> <wire1> ... <wireN>

Emitted every time the active CPI level changes, including switches made with
the physical CPI button. ``active`` is **0-based**, the same encoding as the
``active`` field of command ``0x34``, and the wire bytes repeat the level
table in the ``0x34`` sensor encoding. Captured example with levels
800/900/1800 and level 2 going active::

   ad 03 01 12 14 29 00 ... 00

alloyctl's TUI listens on this interface to keep the ACTIVE level indicator in
sync with the hardware button - the only known device-to-host state channel.
Because it is push-only and there is no read-back, alloyctl cannot learn the
active level at launch, so it does **not** push the DPI table (and thus the
active level) at startup; doing so would force the last-saved level over
whatever the mouse is actually running.

Read-back
=========

No command to read the current configuration back from the device has been
found (``0x10``, ``0x12``, ``0x92`` all go unanswered). Consequence for
alloyctl: the pre-session baseline used by REVERT is kept in a host-side state
file, seeded with driver defaults on first run. The ``0xAD`` notification
above is push-only: it reports level switches as they happen but cannot be
queried.

Acceleration / deceleration / angle snapping -- not firmware
============================================================

These three have no onboard command and cannot be stored on the mouse. They are
not a gap in this reverse engineering: SteelSeries Engine offers them, but
applies them **host-side** (they need the software running and are never written
to the device), and ``rivalcfg`` - which only drives onboard HID config -
defines no such setting. Probing finds no acknowledged command.

alloyctl implements them host-side instead, as a pointer-transform daemon; see
:doc:`../architecture/pointer-transform`.
