.. SPDX-License-Identifier: GPL-2.0-only
.. Copyright (C) 2026 Szymon Wilczek

====================================================
SteelSeries Aerox 3 Wireless (Bluetooth) -- HID/GATT
====================================================

Device: ``1038:183A`` on the **Bluetooth** bus (Linux ``HID_ID`` bus ``0x0005``).
The same Aerox 3 Wireless documented in :doc:`steelseries-aerox3-wireless`
reached over its Bluetooth link instead of the 2.4 GHz receiver. Over Bluetooth
it is a Bluetooth Low Energy device and configuration rides HID-over-GATT, so
the transport differs from both the receiver and the USB-wired paths even though
the payloads are shared.

Findings below were reverse engineered on real hardware: SteelSeries GG driving
the mouse over Bluetooth on Windows, captured with ``btvs.exe`` piped into
Wireshark (HCI), and the GATT database dumped on Linux with ``bluetoothctl``.

Identity
========

Over Bluetooth the PnP ID re-brands the vendor: the mouse reports
``Modalias usb:v0111p183Ad010B`` and Linux binds it as::

  HID_ID=0005:00000111:0000183A
  HID_NAME=Aerox 3

so the driver matches on the Bluetooth bus by **product id alone** (``0x183A``),
ignoring the vendor id. The 2.4 GHz receiver enumerates as ``1038:1838`` on USB;
``1038:183A`` on the USB bus (``0x0003``) is instead the wired mode, which is not
covered here.

GATT layout
===========

The vendor configuration channel is an ordinary **HID Report characteristic**
under the standard HID service, not a proprietary service, so the kernel's
HID-over-GATT (hog) driver exposes the mouse as a single ``/dev/hidrawN`` node
and the vendor writes go to a numbered Output report on it.

===================  ========================================  =================
GATT handle          characteristic                            note
===================  ========================================  =================
service ``0x0014``   Human Interface Device (``0x1812``)        HID service
value ``0x002f``     Report (``0x2A4D``)                        config channel
desc ``0x0030``      Report Reference = ``04 02``               **report id 4, Output**
service ``0x0010``   Battery Service (``0x180F``)               standard battery
value ``0x0012``     Battery Level (``0x2A19``)                 charge %, read/notify
===================  ========================================  =================

Config writes are ATT **Write Command** (write-without-response) to handle
``0x002f`` carrying ``<opcode> <payload...>``; over hidraw the same bytes are an
Output report, written as ``[0x04] <opcode> <payload...>`` (report id 4 first).
The write is fire-and-forget: the mouse never echoes an ACK, and the only
notifications on the link are ordinary mouse input reports.

Opcodes: the wired values, unflagged
====================================

The receiver path OR-s ``0x40`` into every opcode (see
:doc:`steelseries-aerox3-wireless`). Over Bluetooth the opcodes are the **plain
wired values** -- the receiver opcode without bit 6 -- so the driver reuses the
receiver's packet builders and clears ``0x40`` from the command byte.

============  =============  =============  ======================================
knob          BT opcode      receiver       payload
============  =============  =============  ======================================
CPI           ``0x2d``       ``0x6d``       ``<count> <active> <wire per preset>``
sleep timer   ``0x29``       ``0x69``       ``<ms, 3-byte LE>`` (min x 60000; empty = never)
dim + smart   ``0x23``       ``0x63``       ``<level> 01 <smart> 00 <dim ms, 3-byte LE>``
============  =============  =============  ======================================

The DPI wire values are the same TrueMove Air table as the receiver path. The
``0x23`` command carries brightness in ``<level>``, but brightness is not
editable over Bluetooth (GG greys it out); GG pins it to full (``0x0f``) and the
driver does the same, driving ``0x23`` only for the dim-timer and
smart-illumination fields it also carries.

Captured examples (payload only, report id 4 omitted)::

  2d 05 00 03 04 0d 1b 26     CPI: 5 presets, active 0, wire 03/04/0d/1b/26
  29 e0 93 04                 sleep 5 min  (0x0493E0 = 300000 ms)
  23 0f 01 01 00 30 75        dim 30 s (0x7530 = 30000 ms), smart on

What Bluetooth locks out
========================

GG exposes only four knobs over Bluetooth -- CPI, the sleep timer, the LED dim
timer and smart illumination -- and greys out the rest; the firmware accepts
nothing else on this link. Polling rate, LED colors and effects, button
remapping, High-Efficiency mode and receiver pairing are all unavailable, so the
driver advertises none of them.

Negative results, confirmed in the captures:

* **No save/commit opcode.** GG sends no ``0x11``/``0x51`` after a change; the
  writes take effect on their own. Whether they survive a power cycle without a
  commit is not yet verified.
* **No connect handshake.** GG writes nothing to the vendor channel on connect.
* **Battery is not on the vendor channel.** There is no ``0xD2`` query over
  Bluetooth; charge is the standard GATT Battery Service (``0x180F``). Reading it
  is not driven yet, so the TUI shows ``-- (on Bluetooth)``.

Open questions
==============

* Does ``0x23`` byte ``<level>`` actually change brightness over Bluetooth, or is
  it ignored on this link? GG always sends ``0x0f``; the driver mirrors that.
* Do writes persist across a mouse power cycle without a save step?
