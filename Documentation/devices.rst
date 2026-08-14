.. SPDX-License-Identifier: GPL-2.0-only
.. Copyright (C) 2026 Szymon Wilczek

=================
Supported devices
=================

alloyctl currently supports the following SteelSeries gaming mice on Linux:

.. list-table::
   :header-rows: 1
   :widths: 35 25 20 20

   * - Device
     - Connectivity
     - Device ID
     - Protocol doc
   * - SteelSeries Prime
     - USB (wired)
     - ``1038:182e``, ``1038:182a``, ``1038:1856``
     - :doc:`protocol/steelseries-prime`
   * - SteelSeries Rival 3 (Gen 1)
     - USB (wired)
     - ``1038:1824``, ``1038:184c``
     - :doc:`protocol/steelseries-rival3`
   * - SteelSeries Rival 3 Gen 2
     - USB (wired)
     - ``1038:1870``
     - :doc:`protocol/steelseries-rival3-gen2`
   * - SteelSeries Aerox 3 Wireless
     - 2.4 GHz USB / Wired
     - ``1038:1838``
     - :doc:`protocol/steelseries-aerox3-wireless`
   * - SteelSeries Aerox 3 Wireless (BT)
     - Bluetooth LE
     - ``BUS_BLUETOOTH``
     - :doc:`protocol/steelseries-aerox3-wireless-bt`

Device support is modular: each mouse has its own dedicated driver under ``drivers/``.
To contribute support for a new mouse, see :doc:`contributor/adding-a-driver`.
