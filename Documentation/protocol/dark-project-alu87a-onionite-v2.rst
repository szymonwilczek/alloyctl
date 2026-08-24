.. SPDX-License-Identifier: GPL-2.0-only
.. Copyright (C) 2026 Szymon Wilczek

==================================================
Dark Project ALU87A Onionite V2 (wired TKL) -- HID
==================================================

Device: ``342D:E40F`` (Dark Project ALU87A Onionite V2 Gaming Keyboard).

Reverse-engineered from direct hidraw probing, Web Lab analysis, and USB packet capture analysis on real hardware.

USB interfaces
==============

The keyboard exposes three USB interfaces under USB configuration 1:

* **Interface 0** (Boot Keyboard): Standard 8-byte boot keyboard endpoint + 3-bit LED output report.
* **Interface 1** (Consumer / NKRO / Mouse): Consumer media keys, NKRO bitmap, and mouse/macro report endpoints.
* **Interface 2** (Vendor Configuration): Vendor-defined HID collection (Usage Page ``0xFF01`` / Usage ``0x01``). Carries 256-byte HID Feature Reports (Report ID ``0x07``) used for all configuration reads and writes.

Command structure (HID Feature Reports, Report ID 0x07)
=======================================================

Communication is performed using Linux ``HIDIOCSFEATURE`` and ``HIDIOCGFEATURE`` ioctls on Interface 2.
All reports begin with Report ID byte ``0x07`` followed by a 255-byte payload (total 256 bytes).

Commands
========

``0x07 0x82 <profile>`` -- Read profile configuration
-----------------------------------------------------

::

   0x07 0x82 <profile_num> [253 x 0x00]

Reads the 256-byte onboard profile payload from the keyboard for the specified profile (``0x01`` = Profile 1, ``0x02`` = Profile 2, ``0x03`` = Profile 3).

The returned report contains:

* Byte 9: Active effect ID (``HidEffectID``)
* Byte 10 + HidEffectID: Brightness level (``0x00`` .. ``0x04`` = 0%, 25%, 50%, 75%, 100%)
* Byte 24 + HidEffectID: Speed level (``0x01`` .. ``0x05``)
* Byte 38 + HidEffectID: Multi-color mode flag (``0x00`` = static color, ``0x08`` = multi-color)
* Byte 59: Red component (0..255)
* Byte 67: Green component (0..255)
* Byte 75: Blue component (0..255)
* Byte 8: Polling rate index (``0x01`` = 125 Hz, ``0x02`` = 250 Hz, ``0x03`` = 500 Hz, ``0x04`` = 1000 Hz)
* Byte 88: Debounce latency index (``0x00`` = 0ms, ``0x01`` = 2ms, ``0x02`` = 8ms, ``0x03`` = 12ms)

``0x07 0x02 <profile_data...>`` -- Write profile / lighting configuration
-------------------------------------------------------------------------

::

   0x07 0x02 [254-byte profile payload]

Writes the updated configuration directly to the keyboard and commits it to onboard flash memory.

Supported hardware effects (``HidEffectID``):

* ``0`` -- Wave
* ``1`` -- Color Cycle
* ``2`` -- Random
* ``3`` -- Breathing
* ``4`` -- Footprint (reactive)
* ``5`` -- Solid (static 24-bit RGB color)
* ``6`` -- Spiral Wave
* ``7`` -- Star
* ``8`` -- River
* ``9`` -- Ripples (reactive)
* ``10`` -- Trigger (reactive)
* ``11`` -- Color Discharge (reactive)
* ``12`` -- Sine Wave
* ``13`` -- Rain

``0x07 0x01 <profile>`` -- Switch active onboard profile
--------------------------------------------------------

::

   0x07 0x01 <profile_num> [253 x 0x00]

Switches the active onboard profile (1, 2, or 3).

``0x07 0x09 <profile> <enabled> ...`` -- Configure Snap Tap (SOCD)
------------------------------------------------------------------

::

   0x07 0x09 <profile_num> <enabled> [4 x 0x00] <mode> <key1> <key2> [245 x 0x00]

Configures hardware Snap Tap (SOCD / Null Bindings) counter-strafing in the keyboard MCU:

* ``<enabled>``: ``0x01`` = enabled, ``0x00`` = disabled
* ``<mode>``: ``0x00`` = Last Input Priority, ``0x01`` = Absolute Key 1, ``0x02`` = Absolute Key 2, ``0x03`` = Neutral
* ``<key1>`` / ``<key2>``: Standard USB HID Keycodes (defaults: ``0x04`` = 'A', ``0x07`` = 'D')

Hardware Event Notifications (Input Reports on Interface 2)
===========================================================

The keyboard sends asynchronous 8-byte input reports when state is toggled via onboard shortcuts:

* ``0x05 0xA4 0xF0 ... <status>``: Sent on ``Fn + Right Shift`` (Snap Tap toggle). Byte 7 holds the new status (``0x01`` = ON, ``0x00`` = OFF).
* ``0x05 0xA4 0xF1 <profile> ...``: Sent on profile switch shortcuts. Byte 3 holds the active profile index (``0x01`` .. ``0x03``).
