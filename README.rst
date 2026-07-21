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
   * - SteelSeries Aerox 3 Wireless *(2.4 GHz receiver)*
     - ``1038:1838``
   * - SteelSeries Rival 3 Gen 2
     - ``1038:1870``
   * - SteelSeries Rival 3
     - ``1038:1824``, ``1038:184c``

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

Choosing drivers
----------------

Plain ``make`` builds every driver in the tree. You can trim the binary to just
the hardware you use:

.. code-block:: sh

   make list-drivers                                           # valid driver names
   make DRIVERS="steelseries_rival3_gen2"                      # build only this one
   make DRIVERS="steelseries_rival3 steelseries_rival3_gen2"   # subset

Only the named drivers' code and embedded art are compiled and linked; an
unknown name is a hard error listing the valid ones. Released binaries always
ship the full driver set -- this is a source-build convenience.

Installing
==========

TUI itself runs straight from the build tree. Installing matters mainly for
the **pointer-transform daemon** (host-side acceleration/deceleration/angle
snapping): it needs the ``70-alloyctl-uinput.rules`` udev rule for ``/dev/uinput``
and evdev access, and a binary in a stable location so the autostart entry keeps
working across reboots.

From a release download (no source tree):

.. code-block:: sh

   tar -xzf alloyctl-<version>-linux-x86_64.tar.gz
   cd alloyctl-<version>-linux-x86_64
   sudo ./install.sh                               # or: sudo ./install.sh --prefix /usr
   sudo ./install.sh --uninstall                   # to remove it again

From source:

.. code-block:: sh

   sudo make install    # PREFIX, DESTDIR, BINDIR, UDEVDIR overridable
   sudo make uninstall

Both install the binary and the udev rule, then reload udev. On non-logind
systems, add yourself to the ``input`` group for ``/dev/input`` and
``/dev/uinput`` access: ``sudo usermod -aG input $USER``.

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

Online hosted documentation can be found at https://alloy.szymon-wilczek.me

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
