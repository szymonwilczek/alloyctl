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
feature. This is why isolating ``0x68`` (High-Efficiency Mode) took a Windows GG
capture rather than blind probing -- the ACK alone could not tell it apart from
the unused opcodes in the block.

Waking the link
---------------

The ACK only arrives while the 2.4 GHz link is awake. After a second or two
with no mouse motion the link micro-sleeps, and until it wakes the config
interface answers **every** query with the ``40 ff`` idle marker (below)
instead of the echo -- it also pushes that marker unsolicited. A command sent
in that window is not rejected; the mouse is simply not listening yet. The
transport therefore treats a missing/idle response as "asleep, try again": it
re-sends the command a few times (a wake nudge is enough to bring the link
back), and it reads *until* the echo arrives rather than trusting the first
report, since an idle marker can land ahead of the ACK. Only after the whole
retry budget elapses is the command reported as unacknowledged. Background
polls (battery) use a shorter budget than config writes so a genuinely absent
mouse cannot stall the UI. This is the standard every wireless driver in the
tree inherits from the transport layer -- individual drivers do not re-implement
it.

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

alloyctl drives all three: ``level`` from the brightness slider, ``smart`` from
the illumination smart-mode toggle, and the dim timer from its idle-seconds
setting (clamped to the 1200 s ceiling, sent as little-endian ms).

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

``0x68`` -- High-Efficiency Mode (wired ``0x28``)
-------------------------------------------------

::

   0x68 <enable>   ; 0x01 on, 0x00 off

The GG "High-Efficiency Mode" battery saver ("extend battery life with automatic
settings"). Reverse engineered from a Windows GG USB capture: toggling the mode
does **not** send a single self-contained opcode -- GG emits a three-command
bundle, and the ``0x68`` flag is only one part of it. Captured, mode turned on::

   63 00 01 01 00 00 00 00   ; illumination level forced to 0 (LEDs blanked)
   68 01                     ; High-Efficiency flag on
   6b 03                     ; polling forced to 125 Hz

and turned off again, the two forced registers are restored to the user's
values (here 1000 Hz and full brightness)::

   63 0f 01 01 00 ...        ; brightness restored
   68 00                     ; flag off
   6b 00                     ; 1000 Hz restored

So the mode *is* the bundle: the firmware does not drop polling or lighting on
its own from the flag, the host drives the saver. alloyctl mirrors this exactly
-- enabling forces 125 Hz and blanks the LEDs alongside ``0x68 0x01``; disabling
sends ``0x68 0x00`` and re-pushes the polling rate and brightness from the live
config. The dedicated flag ``0x68`` is what the earlier probing could not
isolate (the whole ``0x60``--``0x6F`` block ACKs), now pinned by the capture.

``0x69`` -- sleep timer (wired ``0x29``, wireless only)
-------------------------------------------------------

::

   0x69 <t0> <t1> <t2>

Idle time before the mouse sleeps, as 3-byte little-endian milliseconds,
0--20 minutes (``0`` = never). This is the "Battery Saver" stepper in GG.
Captured: 5 min = 300000 ms::

   69 e0 93 04            ; 0x000493E0 little-endian

Driven by alloyctl through ops->apply_sleep, cfg->sleep_min minutes converted to
the little-endian ms count (0 = never), clamped to the 20 min ceiling.

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

``0xBC`` -- power state notification
------------------------------------

::

   0xBC <state>   ; 0x01 wake / link, 0x00 sleep / unlink

Emitted when the linked mouse changes power state: ``0xBC 0x01`` when it wakes or
(re)links to the receiver, ``0xBC 0x00`` when it sleeps or drops off. Captured
across a 30 s sleep-timer cycle: the mouse idled out (``bc 00``), config queries
answered the ``40 ff`` idle marker for the whole nap, then a nudge woke it
(``bc 01``). The wake edge is what prompts the host to re-push the
non-persistent lighting, which the live-preview reapply already does.

``0x12`` -- unsolicited battery level
-------------------------------------

::

   0x12 <b>

A device-initiated battery report, same byte encoding as the ``0xD2`` query
(bit7 charging, ``((b & 0x7F) - 1) * 5`` = level %). Pushed right after a
``0xBC 0x01`` wake. Captured ``12 13`` -> ``0x13`` = 90 %. alloyctl does not
need to key off it -- the UI polls ``0xD2`` on a timer -- but it confirms the
gauge without a round-trip.

``0x80`` -- device-name marker
------------------------------

At the sleep boundary the interface also emits a one-off ``0x80`` report
carrying the ASCII device name (captured ``80 "Wireless Device"``). It conveys
no configuration and alloyctl ignores it; noted here so it is not mistaken for a
command echo.

``40 ff`` -- receiver idle marker
---------------------------------

When the mouse is asleep or not linked, config-interface queries answer with a
report beginning ``40 ff ...`` instead of the expected echo. The transport
skips this marker and re-sends the command to wake the link (see *Waking the
link* above); once the retries are exhausted the battery op keys off the marker
to report "no reading" rather than a bogus 0 %.

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

USB wired (cable) mode
======================

Plugging the mouse in by **cable** does not route it through this driver.
On the cable the mouse enumerates directly as ``1038:183A`` -- the same USB
product id it uses over Bluetooth, **not** the ``1038:1838`` receiver id this
driver binds to. alloyctl therefore does not detect the cabled Aerox at all,
and none of the wireless configuration (including the DEVICE/POWER sections of
the TUI) is available in that mode:

* **Detection** -- ``0x183A`` is not a registered driver (it appears in the tree
  only as ``bt_product_id``, used to light the Bluetooth indicator), so device
  enumeration skips it.
* **Protocol** -- this driver is built entirely around the receiver's wireless
  protocol (every opcode OR ``0x40``, config on interface 3, battery via
  ``0xD2``). The wired ``0x183A`` endpoint speaks the un-flagged wired opcode
  set on different interfaces; the flagged commands would go unacknowledged.
* **Semantics** -- the POWER settings describe running off the battery. On the
  cable the mouse is bus-powered and charging, so the sleep timer is moot and
  the gauge reports charging rather than a discharge level.

**Configuring the wireless features requires the 2.4 GHz receiver.** Supporting
the cabled mouse would be a separate wired-Aerox driver for ``0x183A`` with its
own opcode set and no ``ALLOY_CAP_BATTERY``; it is out of scope here.

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

Resolved by the Windows GG capture
==================================

The two items previously open here were pinned from a SteelSeries GG USB capture
on Windows (see the ``0x68`` command and the ``0xBC`` / ``0x12`` events above):

* **High-Efficiency Mode** is the ``0x68`` flag driven as a three-command bundle
  (``0x68`` + forced 125 Hz ``0x6B`` + blanked ``0x63``). This is the opcode the
  earlier ``0x60``--``0x6F`` probing could not discriminate.
* The wake/sleep handshake is the ``0xBC`` power notification (``01`` wake/link,
  ``00`` sleep/unlink), with an unsolicited ``0x12`` battery push after wake and
  a one-off ``0x80`` device-name marker at the sleep edge.

Open questions / not yet reverse engineered
===========================================

Every configuration opcode and event this receiver exposes is now identified,
driven and documented above; nothing on the 2.4 GHz *config* surface remains
open. What is left is on the transport and pairing side, surfaced by the
wireless TUI work:

* The ``0xBC`` wake/sleep notification is decoded (see
  `0xBC -- power state notification`_), but the config transport does not yet
  gate on it: it works around an idle link with a blind wake-retry (re-send
  until the echo arrives). Gating writes on the ``0xBC 0x01`` wake edge would
  let the host *know* the mouse is awake and replace the retry loop with an
  event-driven flush.
* **Dongle pairing.** Binding a fresh mouse to the receiver (mouse switched OFF,
  then held CPI while flicking to 2.4 GHz) only completes while SteelSeries GG is
  running, which means GG puts the *receiver* into a bind/listen mode over USB;
  the mouse-side gesture alone is not enough. The pairing opcode has not been
  captured yet. alloyctl carries the capability (``ALLOY_CAP_PAIRING``), a PAIR
  button in the DEVICE box and a two-step wizard modeled on the GG flow, but the
  ``ops->pair`` hook is a stub that returns ``ALLOY_PAIR_UNIMPLEMENTED`` -- so the
  wizard walks the user through the gesture yet cannot yet complete the bind, and
  only a mouse the receiver already knows can be driven. Reverse engineering the
  opcode needs a USBPcap capture of GG's "connect a new device" flow on Windows
  (cross-check the gort818 notes); the bind confirmation is likely the
  ``bc 01`` link-up on the event interface. Landing it is a one-function change
  in ``a3wl_pair``.
