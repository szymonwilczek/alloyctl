.. SPDX-License-Identifier: GPL-2.0-only
.. Copyright (C) 2026 Szymon Wilczek

==========================================
Host-side pointer transform (accel / snap)
==========================================

Acceleration, deceleration and angle snapping are **not** firmware features of
any supported mouse. No HID command sets them; `rivalcfg
<https://github.com/flozz/rivalcfg>`_ exposes no such setting for these devices;
and probing finds no acknowledged command. SteelSeries Engine offers them on
Windows, but applies them **host-side** in its always-running process - they
stop working when Engine is closed and are never written to the mouse's onboard
memory, unlike CPI, polling rate and button mapping.

alloyctl does the same on Linux, and because it works below the display server
it behaves identically on X11 and every Wayland compositor.

How it works
============

Per-device daemon (``alloyctl --accel-daemon VID:PID``) sits in the input
pipeline, beneath libinput:

#. Resolve the mouse's **motion** evdev node by scanning ``/sys/class/input``
   for the node whose ``device/id`` matches the VID:PID and whose
   ``capabilities/rel`` advertises ``REL_X`` and ``REL_Y``. This is the boot
   pointer interface, not the vendor hidraw interface used for configuration.
#. ``EVIOCGRAB`` it - an exclusive grab, so the raw device no longer reaches
   the compositor.
#. Create a ``/dev/uinput`` virtual pointer that mirrors the real device's
   ``EV_KEY``/``EV_REL``/``EV_ABS``/``EV_MSC`` capabilities, so buttons, wheel
   and everything else pass through untouched.
#. For each ``SYN_REPORT`` batch, transform the accumulated ``REL_X``/``REL_Y``
   (angle snapping, then a speed-dependent gain, with sub-count remainder carry)
   and re-emit through the virtual pointer.

The transform math (``src/accel_transform.c``) is pure, integer-only and unit
tested; the device I/O and lifecycle live in ``src/accel.c``.

The kernel releases ``EVIOCGRAB`` automatically when the file descriptor closes
or the process dies, so a crash never leaves the physical mouse unusable - the
real device simply reappears to the compositor.

Cooperate, never overwrite
==========================

Opening the TUI changes nothing about how the mouse behaves: alloyctl only
*probes* whether a daemon is already running and reflects that. It never grabs
the device, spawns a daemon or applies a transform until the user explicitly
enables the engine, and disabling it drops the grab so motion returns to raw.
Values initialise from the real running state and the persisted config.

Lifecycle and permissions
=========================

* **Persistence:** enabling the engine writes a per-device XDG autostart entry
  (``~/.config/autostart/alloyctl-accel-<vid>-<pid>.desktop``) that restarts the
  daemon at login, so the choice survives a reboot - without a systemd dependency.
  One daemon per device, guarded by a pidfile in ``$XDG_RUNTIME_DIR``.
* **Live edits:** the TUI writes the config the daemon watches and sends
  ``SIGHUP``; the daemon re-reads it.
* **Permissions (rootless):** the daemon needs ``/dev/uinput`` (root-only by
  default) and the mouse's ``/dev/input`` motion node (``root:input`` by
  default - *not* seat-accessible out of the box). ``make install`` ships
  ``70-alloyctl-uinput.rules`` granting both to the active seat (``uaccess``):
  ``/dev/uinput`` directly and the evdev nodes of SteelSeries (VID ``1038``)
  devices. On non-logind systems, ``input``-group membership is the fallback
  (takes effect at the next login).

Interaction with the compositor
===============================

Compositor still applies its own pointer acceleration to the virtual device -
exactly as Windows re-accelerates SteelSeries' transformed counts. For
a pure curve, set the compositor/libinput acceleration profile to *flat* for the
``alloyctl virtual pointer`` device (it carries a distinct, targetable name).
