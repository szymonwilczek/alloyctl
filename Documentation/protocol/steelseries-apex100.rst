.. SPDX-License-Identifier: GPL-2.0-only
.. Copyright (C) 2026 Szymon Wilczek

====================================
SteelSeries Apex 100 (wired) -- HID
====================================

Device: ``1038:160E`` (SteelSeries Apex 100 Gaming Keyboard).

Reverse-engineered from direct hidraw probing and USB packet capture analysis on real hardware.

USB interfaces
==============

The keyboard exposes three USB interfaces under USB configuration 1:

* **Interface 0** (Boot Keyboard): Standard 8-byte boot keyboard endpoint + 3-bit LED output report.
* **Interface 1** (Vendor Configuration): Vendor-defined HID collection (Usage Page ``0xFFC0`` / ``0xFFC1``). Carries 32-byte Output and Input reports used for all configuration commands.
* **Interface 2** (Consumer / NKRO): Consumer media keys (Report ID 1) and full key matrix reports (Report ID 2).

Hardware shortcuts
==================

* **Windows / Meta Key Lock**: Toggled directly in hardware on the keyboard by pressing **Fn (SteelSeries Key) + Windows Key**. When active, the Win Lock indicator LED on the keyboard lights up and the Windows key is suppressed at the controller level.

Command acknowledgement
=======================

Commands are written as 32-byte HID output reports to Interface 1 (``/dev/hidrawN`` matching interface 1). Configuration writes are fire-and-forget; queries (such as firmware version query ``0x10``) return 32-byte responses on the interrupt IN endpoint.

Commands (32-byte output reports, interface 1)
==============================================

``0x04 0x00`` -- Polling rate
-----------------------------

::

   0x04 0x00 <rate> [30 x 0x00]

* ``0x01`` -- 1000 Hz
* ``0x02`` -- 500 Hz
* ``0x03`` -- 250 Hz
* ``0x04`` -- 125 Hz

``0x05 0x00`` -- Illumination brightness
-----------------------------------------

::

   0x05 0x00 <brightness> [30 x 0x00]

Sets the blue LED backlight brightness. ``<brightness>`` is an integer percentage byte ranging from ``0x00`` (0% / LED off) to ``0x64`` (100% / maximum brightness).

``0x07 0x00`` -- Lighting effect
--------------------------------

::

   0x07 0x00 <effect> [30 x 0x00]

Configures the illumination animation mode and speed:

* ``0x01`` -- Steady (static illumination)
* ``0x02`` -- Breathing (slow speed)
* ``0x03`` -- Breathing (medium speed)
* ``0x04`` -- Breathing (fast speed)

``0x09 0x00`` -- Save to flash memory
-------------------------------------

::

   0x09 0x00 [30 x 0x00]

Commits the current live polling rate, brightness level, and lighting effect to onboard flash memory so settings persist across power cycles.

``0x10 0x00`` -- Firmware version query
---------------------------------------

::

   0x10 0x00 [30 x 0x00]

Returns a 32-byte report on the interrupt IN endpoint where the first byte indicates the firmware revision in BCD format (e.g. ``0x41`` for firmware ``0.41``).
