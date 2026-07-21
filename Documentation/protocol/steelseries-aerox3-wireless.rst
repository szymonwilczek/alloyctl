.. SPDX-License-Identifier: GPL-2.0-only
.. Copyright (C) 2026 Szymon Wilczek

=============================================
SteelSeries Aerox 3 Wireless (2.4 GHz) -- HID
=============================================

Device: ``1038:1838`` (2.4 GHz receiver mode), USB 2.0 full speed,
``bcdDevice 1.31``, firmware ``1.3.1``. The same silicon also enumerates as:

===============  ==========================================
USB ID           mode
===============  ==========================================
``1038:1838``    Aerox 3 Wireless, 2.4 GHz receiver **(this driver)**
``1038:1878``    Aerox 3 Wireless CS2 Dragon Lore, 2.4 GHz (protocol-identical)
``1038:183A``    Aerox 3 Wireless, USB **wired** mode (different opcodes)
``1038:187A``    Aerox 3 Wireless CS2 Dragon Lore, wired mode
===============  ==========================================

This is the first **wireless** device supported by alloyctl: on top of the usual
mouse configuration it reports a battery gauge. Findings below were obtained by
reverse engineering on real hardware (hidraw probing, HID report-descriptor
analysis) and cross-checked against `rivalcfg
<https://github.com/flozz/rivalcfg>`_ and the community capture notes at
`gort818/aerox3-wireless <https://github.com/gort818/aerox3-wireless>`_.

The wireless flag
=================

The receiver speaks the same 64-byte vendor protocol as recent wired
SteelSeries mice, but **every configuration opcode carries the ``0x40`` bit**
relative to its wired value. Verified on hardware: the wired color command
``0x21`` and wired battery query ``0x92`` are silent, while their flagged forms
``0x61`` and ``0xD2`` are acknowledged. Throughout this page the wired opcode is
noted next to the wireless one.

The firmware-version query ``0x90`` is the exception -- it is not flagged.

USB interfaces
==============

The receiver exposes five HID interfaces:

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
     - mouse input (8 buttons, 16-bit X/Y, wheel, AC pan)
   * - 1
     - input1
     - ``0x01``
     - keyboard (NKRO) for keyboard button bindings
   * - 2
     - input2
     - ``0x0C``
     - consumer control (media-key bindings)
   * - 3
     - input3
     - ``0xFFC0``
     - **configuration**: 64 B in/out, 512 B feature
   * - 4
     - input4
     - ``0xFFC1``
     - vendor **events**: 64 B input only

All configuration traffic goes through **interface 3** using HID output
reports. The device does not use numbered reports: a raw ``write()`` on the
hidraw node is a ``0x00`` report-number byte followed by the 64-byte payload
(65 bytes total). None of the interfaces carry an interrupt OUT endpoint, so the
kernel delivers the writes as control ``SET_REPORT`` transfers.

Command acknowledgement
=======================

Every *recognized* command is acknowledged on the interrupt IN endpoint of
interface 3 with a 64-byte packet that echoes the command byte (e.g. ``0x61 ...``
yields ``61 00 ...``). Unrecognized commands produce **no** response, which
makes the ACK a reliable "command accepted" signal.

The whole ``0x60``--``0x6F`` block echoes, so an ACK there confirms only that
the opcode is inside the recognized range, not that each byte drives a distinct
feature (see the open questions at the end).

Commands (output reports, interface 3)
======================================

``0x6D`` -- DPI presets (wired ``0x2D``)
----------------------------------------

::

   0x6D <count> <active> <w1> [<w2> ...]

* ``count``  -- number of presets configured (1--5)
* ``active`` -- 0-based index of the active preset (same encoding as the
  ``0xAD`` notification below)
* ``wN``     -- one sensor byte **per preset** (single value, no separate X/Y)

DPI range 100--18000 in steps of 100. The sensor is a TrueMove Air; DPI does not
map linearly to the wire byte, so the driver carries the exact table
(100 -> ``0x00``, 400 -> ``0x04``, 800 -> ``0x09``, 1600 -> ``0x12``,
18000 -> ``0xD6``). Captured example, five levels 400/800/1200/2400/3200 with
the first active::

   6d 05 00 04 09 0d 1b 26

The presets are onboard CPI levels; colors, effects, polling and button
bindings are a single global configuration shared by every level.

``0x6B`` -- polling rate (wired ``0x2B``)
-----------------------------------------

::

   0x6B <rate>

====  ======
Hz    byte
====  ======
1000  0x00
500   0x01
250   0x02
125   0x03
====  ======

Note the encoding differs from the Rival 3 family (where 1000 Hz is ``0x01``).

``0x61`` -- zone color (wired ``0x21``)
---------------------------------------

::

   0x61 0x01 <zone> <R> <G> <B>

One write per zone. ``zone`` is ``0x00`` (strip top), ``0x01`` (middle) or
``0x02`` (bottom); the ``0x01`` sub-byte is constant across every capture.
Writing a static color clears the rainbow on that zone. Captured::

   61 01 00 ff 00 00      ; top zone solid red

There is no global "all zones" color command -- each zone is addressed
individually, unlike the Rival 3 Gen 2 masked ``0x21``.

**LED colors are not persisted.** They survive neither the ``0x51`` save nor a
sleep: whenever the mouse idles and the receiver re-enumerates (which it does
frequently, every few seconds under some conditions) the zones go dark and must
be re-sent by the host. This is a deliberate weight/battery trade-off on this
device; the alloyctl live-preview re-applies lighting on every change.

``0x62`` -- rainbow effect (wired ``0x22``)
-------------------------------------------

::

   0x62 <mask>

``mask`` enrolls zones into the rainbow cycle (bit0 top, bit1 middle, bit2
bottom; ``0xFF`` = all, which is what rivalcfg and the GG captures use).
Verified on hardware: with the engine on, ``0x62 0xFF`` cycles all three zones,
and a subsequent ``0x61`` static write on one zone freezes just that zone.

Like the Rival 3 Gen 2, the mask only works while the rainbow **engine** is on;
the engine is the rainbow byte of ``0x67`` (below), so alloyctl sends ``0x67``
first in every lighting apply.

``0x63`` -- illumination: brightness + smart mode + dim timer (wired ``0x23``)
------------------------------------------------------------------------------

A single unified command carries three illumination settings::

   0x63 <level> 0x01 <smart> 0x00 <dim0> <dim1> <dim2>

* ``level``  -- brightness, a **4-bit** value ``0x00``--``0x0F`` (16 steps);
  higher values clamp to ``0x0F``. SteelSeries GG maps its 0--100 % slider onto
  this range, and so does alloyctl. Hardware-verified: ``0x63 0x00`` blanks the
  LEDs, ``0x63 0x08`` is a clear mid dim, ``0x63 0x0F`` is full.
* ``smart``  -- Illumination Smart Mode toggle (``0x00`` off, ``0x01`` on): the
  firmware blanks the LEDs while the mouse is moving and restores them when it
  is stationary, to save battery.
* ``dim0..2`` -- dim-timer idle timeout, 3-byte little-endian milliseconds
  (0--1200 s, ``0`` = off); the LEDs dim after this idle time.

alloyctl currently drives only ``level`` (with ``smart`` and the dim timer sent
disabled); the two power-management fields are wireless features slated for a
later TUI addition.

``0x66`` -- reactive color (wired ``0x26``)
-------------------------------------------

::

   0x66 <enable> 0x00 <R> <G> <B>

``enable`` is ``0x01`` to arm the click flash, ``0x00`` (with a zero color) to
disable it. The enable byte is mandatory. The color is not persisted by
``0x51`` and resets on sleep, so the host re-arms it on every apply.

``0x67`` -- default lighting / rainbow master (wired ``0x27``)
--------------------------------------------------------------

::

   0x67 <rainbow> <reactive>   ; each 0x00 or 0x01

The rainbow byte is the live master switch of the rainbow engine (a ``0x62``
mask sent while it is off is ignored), so alloyctl derives it as "any rainbow
zone OR the startup choice wants rainbow" and sends ``0x67`` first in every
lighting apply -- the same engine ordering as the Rival 3 Gen 2.

``0x69`` -- sleep timer (wired ``0x29``, wireless only)
-------------------------------------------------------

::

   0x69 <t0> <t1> <t2>

Idle time before the mouse sleeps, as 3-byte little-endian milliseconds,
0--20 minutes (``0`` = never). This is the "Battery Saver" stepper in GG.
Captured: 5 min = 300000 ms::

   69 e0 93 04            ; 0x000493E0 little-endian

Driven by alloyctl only as a future wireless-power setting; the driver does not
push it yet.

``0x6A`` -- button mapping (wired ``0x2A``)
-------------------------------------------

::

   0x6A <field1[5]> <field2[5]> ... <field8[5]>

Eight 5-byte fields at fixed offsets, identical in layout to the Rival 3 Gen 2:

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

First byte of each field selects the action: ``0x00`` disabled,
``0x01``--``0x06`` mouse button N, ``0x30`` DPI cycle, ``0x31``/``0x32``
scroll up/down, ``0x51 <keycode>`` keyboard key, ``0x61 <code>`` multimedia key.

``0x51 0x00`` -- save to onboard flash (wired ``0x11``)
-------------------------------------------------------

Commits the live configuration to persistent memory. As noted above, LED colors
are excluded from what the save persists.

``0x90`` -- firmware version
----------------------------

::

   0x90

Answers with a **bare ASCII string from byte 0** (no command echo, unlike the
Rival 3 Gen 2 which prefixes ``0x90``), NUL-terminated and followed by a 4-byte
build id, e.g.::

   31 2e 33 2e 31 00 7d 4e f2 79      ; "1.3.1"

``0xD2`` -- battery level (wired ``0x92``)
------------------------------------------

::

   0xD2

Answers ``0xD2 <b>``:

* charging  -- ``b & 0x80``
* level %   -- ``((b & 0x7F) - 1) * 5`` (level nibble 1..21 -> 0..100 %)

Captured ``d2 14`` -> ``0x14`` = 20 -> 95 %, not charging. When **no mouse is
linked to the receiver**, the query does not answer with a charge but with the
idle marker (see below), which the driver treats as "no reading".

Unsolicited events (interface 4)
================================

The vendor event interface (``0xFFC1``, 64-byte input only) streams
device-initiated notifications.

``0xAD`` -- CPI level switch
----------------------------

::

   0xAD <count> <active> <w1> ... <wN>

Emitted every time the active CPI level changes, including physical CPI-button
presses. ``active`` is 0-based, the wire bytes repeat the level table in the
``0x6D`` encoding. Captured with two levels 400/1600 toggling::

   ad 02 00 04 12
   ad 02 01 04 12

alloyctl listens here to keep the ACTIVE level indicator in sync with the
hardware button, exactly as on the Rival 3 Gen 2.

``40 ff`` -- receiver idle marker
---------------------------------

When the mouse is asleep or not linked, config-interface queries answer with a
report beginning ``40 ff ...`` instead of the expected echo. The battery op
keys off this to report "no reading" rather than a bogus 0 %.

Bluetooth mode
==============

The Aerox 3 Wireless also pairs over Bluetooth. This is selected by a **physical
switch on the underside of the mouse, not by a host command** -- there is no HID
opcode to toggle it, and over Bluetooth the mouse enumerates as a plain BT HID
device (bustype ``0x05``, product ``0x183A``, a single standard-HID interface)
with **no ``0xFFC0`` vendor configuration interface**. Consequently none of the
commands on this page are reachable over Bluetooth, and alloyctl -- which drives
the USB receiver -- does not manage the mouse in that mode.

SteelSeries GG shows a note that in Bluetooth mode angle snapping,
acceleration/deceleration, illumination and button bindings (except keyboard and
mouse-button bindings) revert to defaults, and the polling rate is forced to
125 Hz. This is not a bug: Bluetooth HID has far less bandwidth and a tighter
power budget than the 2.4 GHz link, so the firmware drops the bandwidth-hungry
and battery-hungry features and caps the poll rate to preserve battery.

Read-back
=========

No command reads the current configuration back from the device. The 512-byte
feature report on interface 3 reads back all zeros (``HIDIOCGFEATURE``), and no
read opcode was found. As on the other SteelSeries mice, the pre-session
baseline used by REVERT is kept host-side, seeded with driver defaults on first
run; the ``0xAD`` event is push-only.

Acceleration / deceleration / angle snapping -- not firmware
============================================================

As on every other supported mouse, these have no onboard command and are
implemented host-side by the pointer-transform daemon; see
:doc:`../architecture/pointer-transform`.

Open questions / not yet reverse engineered
===========================================

* **High-Efficiency Mode** (a GG battery-saver toggle, "extend battery life with
  automatic settings") was not identified. Because the whole ``0x60``--``0x6F``
  block ACKs regardless of function and the toggle has no observable effect on
  Linux, it cannot be pinned down without a SteelSeries GG USB capture on
  Windows. During probing, sending ``0x00`` across the unmapped opcodes of that
  block (``0x60``, ``0x64``, ``0x65``, ``0x68``, ``0x6C``, ``0x6E``, ``0x6F``)
  left the illumination stuck off until a power cycle, so one of them is a
  plausible illumination-master / High-Efficiency control -- a lead for a future
  capture.
* The receiver's frequent re-enumeration and the ``40 ff`` idle marker are
  documented empirically; the exact wake/sleep handshake (the ``bc 01``/``bc 00``
  power notifications seen in the gort818 captures) is not yet mapped.
