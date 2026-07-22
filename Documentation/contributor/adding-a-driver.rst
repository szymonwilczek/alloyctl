.. SPDX-License-Identifier: GPL-2.0-only
.. Copyright (C) 2026 Szymon Wilczek

==============================
Adding support for a new mouse
==============================

One mouse is one driver, and each driver lives in its own directory under ``drivers/``.
For example, the Rival 3 driver is located at ``drivers/steelseries_rival3/steelseries_rival3.c``.

Nothing else in the tree needs to change: the build picks the C files up
automatically via wildcard and ``ALLOY_DRIVER_REGISTER()`` adds the driver to the runtime registry
through a dedicated ELF section.

Because registration rides in that ELF section, the drivers built into the
binary are just the ones whose objects get linked. Plain ``make`` links them
all; ``make DRIVERS="..."`` restricts the set (see ``make list-drivers``), which
lets users trim to their own hardware. The test binary always links every
driver, so add your packet-builder tests as usual.

Before you write code
=====================

#. Find the USB IDs: ``lsusb | grep -i steel`` (e.g. ``1038:1870``).
#. Find the configuration interface. Dump the HID report descriptors:

   .. code-block:: sh

      for d in /sys/class/hidraw/hidraw*; do
              grep -H . $d/device/uevent | grep -E 'HID_ID|HID_PHYS'
              xxd -p $d/device/report_descriptor | tr -d '\n'; echo
      done

   You are looking for a vendor usage page (``06 xx ff``) interface with
   64-byte input **and** output reports -- on recent SteelSeries mice that is
   interface 3 (usage page ``0xFFC0``).
#. Reverse engineer or cross-check the command set. The `rivalcfg
   <https://github.com/flozz/rivalcfg>`_ device profiles are an excellent
   starting point. Document everything you learn under
   :doc:`Documentation/protocol/ <../protocol/index>` -- negative results too
   (commands that do not exist matter as much as ones that do).
#. Probing tip: recent firmwares ACK every recognized command by echoing the
   command byte on the interrupt IN endpoint. Silence means "unknown command",
   which makes careful probing safe-ish. Never send writes you do not
   understand to ``0x11``-style flash save commands while experimenting.

Driver directory
================

Create a new directory ``drivers/<vendor>_<model>/`` and place your driver C file inside it as ``drivers/<vendor>_<model>/<vendor>_<model>.c``.
Copy the skeleton below and fill it in.
:ghsrc:`drivers/steelseries_rival3/steelseries_rival3.c` is the reference implementation to
imitate.

.. code-block:: c

   // SPDX-License-Identifier: GPL-2.0-only
   /*
    * <Vendor> <Model> (<wired/wireless>), USB ID xxxx:yyyy.
    *
    * Protocol notes live in Documentation/protocol/<your-mouse>.rst.
    * Maintainer: Your Name <you@example.com>
    */
   #include <string.h>

   #include "driver.h"
   #include "art_mymouse.h" // generated from mymouse_art.txt; see the art note below

   /*
    * Keep packet builders pure (config in, bytes out) and separate from the ops
    * that transmit them:
    * Builders get unit tested without hardware in tests/drivers/<vendor>_<model>.c.
    */

   static int mymouse_apply_dpi(struct alloy_device *dev,
                                const struct alloy_config *cfg)
   {
           uint8_t buf[ALLOY_HID_REPORT_SIZE];
           size_t len = 0;

           /* build the packet ... */
           return alloy_hid_cmd(&dev->hid, buf, len);
   }

   /* ... the remaining ops ... */

   static const uint16_t mymouse_polling_rates[] = { 1000, 500, 250, 125 };

   static const struct alloy_led_zone mymouse_zones[] = {
           /* One entry per independently addressable LED zone.
            * Order defines the zone index used across the config and the UI */
           { .name = "LOGO", .def_color = { 0xFF, 0x00, 0x00 } },
   };

   static const struct alloy_button mymouse_buttons[] = {
           /* Order defines the button index in struct alloy_config */
           { "Button 1 (Left)", { ALLOY_ACT_MOUSE, 1 } },
           /* ... */
   };

   /*
    * Optional:
    * ASCII art of your mouse is read from a text file, not embedded here.
    * Create a file named `<vendor>_<model>_art.txt` in your driver's directory.
    *
    * Mark the LED zones (Z1, Z2, ...) and the side buttons (B4, B5, ...)
    * like the Rival 3 drivers do. Prefix a character with "$N"
    * (N = 1..8) to paint it in the live color of zone N; "$i" paints the
    * following character in a static native "info" tint, for guide labels
    * (the "B4 --" pointers) so they read as annotation, not art; "$$"
    * renders a literal dollar. Every marker takes no column. Zone-painted
    * characters follow the user's colors in the main view and animate
    * in the illumination preview.
    *
    * At build time, the Makefile generates `build/art_<vendor>_<model>.h`
    * (build/ is on the include path), which exposes `alloy_art_<vendor>_<model>`.
    * Include it as `#include "art_<vendor>_<model>.h"` at the top of your
    * driver file and set `.ascii_art = alloy_art_<vendor>_<model>` below.
    *
    * To use the generic fallback art from `defaults/mouse.txt`, simply don't create
    * the text file, `#include "default_art.h"`, and set `.ascii_art = alloy_default_mouse_art`.
    */

   static const struct alloy_driver_ops mymouse_ops = {
           .apply_dpi = mymouse_apply_dpi,
           .apply_polling = mymouse_apply_polling,
           .apply_colors = mymouse_apply_colors,
           .apply_buttons = mymouse_apply_buttons,
           /* Only wire up what the hardware really supports;
            * also set the matching ALLOY_CAP_* bits below */
           .save = mymouse_save,
   };

   static const struct alloy_driver mymouse = {
           .name = "<Vendor> <Model>",
           .vendor_id = 0x1038,
           .product_id = 0xFFFF,
           .interface = 3,
           .dpi = { .min = 200, .max = 8500, .step = 100, .max_presets = 5 },
           .polling_rates = mymouse_polling_rates,
           .num_polling_rates = ALLOY_ARRAY_SIZE(mymouse_polling_rates),
           .zones = mymouse_zones,
           .num_zones = ALLOY_ARRAY_SIZE(mymouse_zones),
           .buttons = mymouse_buttons,
           .num_buttons = ALLOY_ARRAY_SIZE(mymouse_buttons),
           .caps = 0,
           .ascii_art = alloy_art_mymouse, // from the generated header
           .ops = &mymouse_ops,
           .config_defaults = alloy_config_generic_defaults,
   };

   ALLOY_DRIVER_REGISTER(mymouse);

Ground rules
============

* **LED zones are the heart of it.** Getting per-zone color commands right is
  the main value of a driver -- "set everything to one color" is not enough.
  Name the zones after what the user sees (LOGO, STRIP TOP, ...), in the order
  the wire protocol indexes them.
* **Honesty about capabilities.** Only set ``ALLOY_CAP_*`` bits for what the
  hardware really does; the UI shows ``N/A (device)`` for the rest. Do not fake
  acceleration support by scaling DPI.
* **No configuration read-back?** Most SteelSeries firmwares cannot report
  their current settings; alloyctl handles that with the host-side baseline,
  your driver does not need to care.
* **Tests.** Add packet-builder tests in
  ``tests/drivers/<vendor>_<model>.c`` with the exact byte sequences you
  verified on hardware. The file is picked up by the Makefile wildcard.
* **Maintainership.** By contributing the first working driver for a mouse you
  become its maintainer -- responsible for reviewing and keeping it working, not
  granted merge access. Your pull request must add a ``Maintainer:`` line to the
  driver header (shown in the skeleton above) **and** a new section to the
  :ghsrc:`MAINTAINERS` file, kept in alphabetical order. See
  :ref:`maintainership` in the contribution rules.
* **Style.** ``make format`` before committing; CI enforces it.
* **Commits.** ``feat:``/``fix:`` prefix, a body explaining the why, and your
  ``Signed-off-by:`` (DCO). See :doc:`contributing`.

Wireless drivers
================

Wireless mice are supported and form their own driver family, distinguished by a
battery gauge. The reference is
:ghsrc:`drivers/steelseries_aerox3_wireless/steelseries_aerox3_wireless.c`; its
protocol write-up is :doc:`../protocol/steelseries-aerox3-wireless`.

A wireless driver differs from a wired one in a few ways:

#. **Battery.** Set the ``ALLOY_CAP_BATTERY`` capability and implement
   ``ops->battery``, which fills ``*percent`` (0--100) and ``*charging`` and
   returns ``0``, or a negative value when the device reports no valid level --
   e.g. a 2.4 GHz receiver whose mouse is asleep or unlinked answers with an
   idle marker, not a charge. Report that honestly rather than faking ``0 %``.
#. **The receiver is not the mouse.** The USB receiver enumerates even with no
   mouse linked, and it typically re-enumerates as the mouse sleeps and wakes.
   Keep the transport resilient to the config node briefly disappearing, and do
   not assume a setting written once sticks across a sleep.
#. **Non-persistent state.** Some settings (LED colors on the Aerox 3 Wireless)
   are dropped on sleep and not even kept by the save command; the host must
   re-apply them. The live-preview reapplies lighting on change, which covers
   this for the TUI.
#. **Bluetooth.** If the mouse also pairs over Bluetooth, note that it usually
   exposes no vendor configuration interface there, so the driver only manages
   it over the 2.4 GHz receiver. Document the reduced Bluetooth mode rather than
   pretending to drive it.

Only the 2.4 GHz receiver's USB id belongs in the driver's ``product_id``; the
wired-mode and Bluetooth ids are different and, where the opcodes differ, a
separate driver.
