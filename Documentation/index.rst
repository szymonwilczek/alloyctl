.. SPDX-License-Identifier: GPL-2.0-only
.. Copyright (C) 2026 Szymon Wilczek

======================
alloyctl documentation
======================

| alloyctl is a full-screen terminal replacement for SteelSeries Engine on
  Linux, written in C.
| Panes, modals, live preview, and every setting the hardware really has.

This tree holds the project's narrative documentation. It is built with Sphinx
(``make htmldocs``) and hosted online, but every page is plain reStructuredText
and reads fine straight from the source tree.

Who are you?
============

New contributor
---------------

One mouse is one file under ``drivers/``. Community drivers are the whole point
of the project.

* :doc:`Contribution rules <contributor/contributing>`
* :doc:`Adding support for a new mouse <contributor/adding-a-driver>`

Reverse engineer
----------------

Wire protocols are documented from real-hardware probing, negative results
included.

* :doc:`SteelSeries Rival 3 Gen 1 <protocol/steelseries-rival3>`
* :doc:`SteelSeries Rival 3 Gen 2 <protocol/steelseries-rival3-gen2>`

User
----

Build with ``make``, run ``./alloyctl``. The :ghsrc:`README.rst` covers
supported hardware, features, and key bindings.

.. toctree::
   :hidden:

   contributor/index
   protocol/index

Disclaimer
==========

alloyctl is an independent, unofficial project. It is not affiliated with,
endorsed by, or sponsored by SteelSeries ApS. "SteelSeries" and "SteelSeries
Engine" are trademarks of their respective owners, used here only to describe
hardware compatibility.
