.. SPDX-License-Identifier: GPL-2.0-only

========
alloyctl
========

Full-screen terminal replacement for SteelSeries Engine on Linux, written in C.
Panes, modals, live preview, and every setting the hardware really has.

Supported hardware
===================

.. list-table::
   :header-rows: 1
   :widths: 40 40 20

   * - Device
     - USB ID
     - Status
   * - SteelSeries Rival 3 Gen 2
     - ``1038:1870``
     - Fully Supported
   * - SteelSeries Rival 3
     - ``1038:1824``, ``1038:184c``
     - Fully Supported

With several supported mice connected, pick one with
``alloyctl --device VID:PID``.

This project is mice only for now (keyboards are maybe in the scope for the future).
Want yours supported? See
`Documentation/contributor/adding-a-driver.rst
<Documentation/contributor/adding-a-driver.rst>`_ -- one file, one mouse,
community drivers are the whole point of this project.

Features
========

* **Button remapping** -- every physical control, including scroll directions,
  to mouse buttons, CPI toggle, keyboard keys, or off.
* **Per-zone RGB** -- each LED zone addressed individually, not one color
  smeared over everything. The color picker modal offers R/G/B steppers, a
  preset palette and hex entry, all previewed live on the hardware.
* **Hardware lighting effects** -- everything the firmware can run on its own:
  per-zone rainbow cycling, reactive click color, and the power-up lighting
  choice. Drivers declare what their mouse supports; nothing is emulated
  host-side.
* **Two CPI presets** on interval sliders across the sensor's real range, plus
  the active-preset toggle.
* **Polling rate** stepper with a live waveform.
* **Live preview** -- changes hit the mouse instantly but are not persisted;
  **SAVE** commits to onboard flash, **REVERT** restores your session baseline.
  Settings your hardware lacks are shown as ``N/A (device)``, never faked.

Building
========

.. code-block:: sh

   make            # needs gcc/clang, ncursesw, pkg-config
   make test       # unit tests, no hardware required
   ./alloyctl

No root needed as long as your ``/dev/hidraw*`` nodes are writable by your user
(most desktop distributions handle this via udev already; otherwise add a udev
rule for your mouse's VID/PID).

Keys
====

===========  =============================
Key          Action
===========  =============================
Tab / S-Tab  Cycle panes
j/k, arrows  Move within a pane
h/l          Adjust stepper (H/L: fast)
a            Set active CPI preset
Enter        Open modal / Press button
Esc          Close modal
q            Quit
===========  =============================

Documentation
=============

Full manual can be found under ``Documentation/`` and builds to HTML
with Sphinx::

   make htmldocs   # output in Documentation/_build/html/index.html

Highlights:

* `Adding a driver <Documentation/contributor/adding-a-driver.rst>`_
* `Contributing <Documentation/contributor/contributing.rst>`_
* `Reverse-engineered protocol notes <Documentation/protocol/>`_

Design notes
============

* Mice provide no configuration read-back, so REVERT restores the baseline
  persisted under ``~/.config/alloyctl/`` (driver defaults on the very first
  run) -- your defaults, not the application's.

Disclaimer
==========

alloyctl is an independent, unofficial project. It is not affiliated with,
endorsed by, or sponsored by SteelSeries ApS. "SteelSeries" and "SteelSeries
Engine" are trademarks of their respective owners, used here only to describe
hardware compatibility.

License
=======

GPL-2.0-only. See `LICENSE <LICENSE>`_.
