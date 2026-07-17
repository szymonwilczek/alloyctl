.. SPDX-License-Identifier: GPL-2.0-only
.. Copyright (C) 2026 Szymon Wilczek

========
Protocol
========

Reverse-engineered HID protocol notes, one page per supported device. Findings
come from hidraw probing and HID report-descriptor analysis on real hardware,
mostly cross-checked against `rivalcfg <https://github.com/flozz/rivalcfg>`_.
Negative results (commands that do not exist) are documented too: they matter
as much as the ones that do.

.. toctree::
   :maxdepth: 1

   steelseries-rival3
   steelseries-rival3-gen2
