.. SPDX-License-Identifier: GPL-2.0-only
.. Copyright (C) 2026 Szymon Wilczek

============
Contributing
============

alloyctl lives or dies by its drivers, and every driver is a contribution.
This page is the full set of rules; the root :ghsrc:`CONTRIBUTING.rst` is just
a pointer to it.

Short version
=================

#. Open pull requests against the ``alloy-next`` branch, not ``main``. See
   :doc:`branching`.
#. One mouse is one file under ``drivers/``. Read
   :doc:`adding-a-driver` before writing code.
#. Match the existing code style; ``make format`` enforces it.
#. ``make test`` must pass, and new packet builders need new tests.
#. Every commit is ``feat:``/``fix:``, has a body explaining *why*, and carries
   your ``Signed-off-by:`` (DCO).
#. Run ``make check-patch`` before you push; it runs the same gate CI does.

Code style
==========

Code is written in the Linux kernel style: tabs for indentation, 80-column
soft limit, ``lower_snake_case`` for functions and variables, braces on their
own line for functions. The rules are encoded in :ghsrc:`.clang-format`.

Format your changes and check them before committing::

   make format         # rewrite every tracked C/H file in place
   make check-format   # what CI runs; fails on any deviation

Few conventions the formatter cannot see:

* Keep packet **builders** pure -- config in, bytes out -- and separate from
  the **ops** that transmit them. Builders are what the unit tests exercise
  without hardware.
* Only advertise ``ALLOY_CAP_*`` bits the hardware truly has. The UI shows
  ``N/A (device)`` for the rest; never fake a capability.
* Document what you learn on the wire under
  :doc:`Documentation/protocol/ <../protocol/index>`, negative results
  included.

Testing
=======

Tests build without ncurses against a mocked HID transport, so they need no
hardware::

   make test

Cases live one file per mouse under ``tests/drivers/`` plus driver-independent
cases under ``tests/core/``. A test file registers its cases with the
``ALLOY_TEST()`` macro and is picked up by the Makefile wildcard automatically
-- you never edit a central list. When you add a driver, add
``tests/drivers/<vendor>_<model>.c`` with the exact byte sequences you verified
on hardware.

.. _maintainership:

Maintainership
==============

alloyctl is community-driven: every mouse is one driver, and every driver has a
maintainer listed in the :ghsrc:`MAINTAINERS` file.

**The first person to contribute a working driver becomes its maintainer.**
That is the default and it is automatic -- you do not have to ask. You keep the
role until you explicitly hand it to someone else (change the ``M:`` line in a
pull request they agree to).

A pull request that adds a **brand-new** driver -- one for a mouse not already
in the tree -- must, in the same pull request:

#. add a ``Maintainer: Your Name <you@example.com>`` line to the driver header,
   right under the protocol-notes line, and
#. add a new section to the :ghsrc:`MAINTAINERS` file, in alphabetical order,
   with ``M:``, ``S: Maintained``, and one ``F:`` line each for the driver, its
   test file, and its protocol document.

This is what lets the project route a pull request that touches a driver to the
right person. ``MAINTAINERS`` is the single source of truth; ``scripts/get-maintainer``
resolves it, and ``.github/CODEOWNERS`` is **generated from it** (``make
codeowners``, with ``make check-codeowners`` and a CI job failing if the two
drift, so they never disagree). Query it locally with::

   git diff --name-only origin/main... | scripts/get-maintainer            # Name <email> (@handle)
   git diff --name-only origin/main... | scripts/get-maintainer --github   # just the handles

Review routing is a hybrid: GitHub uses ``CODEOWNERS`` to natively request a
review from a driver's maintainer when a pull request touches their driver
(and, if the maintainer branch protection is enabled, can require their
approval to merge). Because ``CODEOWNERS`` only reaches maintainers who are
repository collaborators, the ``Maintainer review request`` workflow @-mentions
any maintainer who is not a collaborator, so nobody is silently skipped -- and
neither path pings a maintainer who opened the pull request themselves.

Changing an existing driver does not transfer its maintainership; when you do,
request review from the maintainer named in ``MAINTAINERS``.

Commit style
============

Subjects are conventional-commit prefixes without a scope::

   feat: add SteelSeries Rival 3 driver
   fix: clamp Gen 1 DPI to the sensor table

* Use ``feat:`` for new behaviour, ``fix:`` for corrections. Keep the subject
  under ~72 columns, imperative mood, no trailing period.
* Every commit has a body that explains **why** the change exists and what it
  affects, wrapped at 75 columns. A one-line commit is not enough for anything
  but the most trivial change.
* Sign every commit off under the Developer Certificate of Origin::

     git commit -s

  which appends ``Signed-off-by: Your Name <you@example.com>`` using your
  ``git config`` identity. By signing off you certify the DCO
  (https://developercertificate.org/).

Local validation
================

Before pushing, run the contributor-side gate::

   make check-patch

It checks the commits on your branch the way CI does: conventional ``feat:``/
``fix:`` subjects, a non-empty body, a DCO sign-off on every commit, trailing
whitespace, ``clang-format``, and a clean build. Fix anything it reports before
you open a pull request.
