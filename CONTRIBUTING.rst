.. SPDX-License-Identifier: GPL-2.0-only
.. Copyright (C) 2026 Szymon Wilczek

============
Contributing
============

alloyctl lives or dies by its drivers, and every driver is a contribution. One
mouse is one file under ``drivers/``; the build and the runtime registry pick
it up automatically.

Open pull requests against the ``alloy-next`` branch, not ``main`` -- ``main``
carries only released code. A pull request opened against ``main`` is retargeted
automatically. See `the branch model
<Documentation/contributor/branching.rst>`_.

The full contribution rules -- code style, testing, commit style, DCO sign-off,
and local validation -- live in `Documentation/contributor/contributing.rst
<Documentation/contributor/contributing.rst>`_.

In short:

* Read `Adding support for a new mouse
  <Documentation/contributor/adding-a-driver.rst>`_ before writing code.
* Match the kernel code style; ``make format`` enforces it.
* ``make test`` must pass, and new packet builders need new tests under
  ``tests/drivers/``.
* The first person to contribute a working driver becomes its maintainer. A
  pull request adding a brand-new driver must also add a ``Maintainer:`` line to
  the driver header and a section to the `MAINTAINERS <MAINTAINERS>`_ file.
* Every commit is ``feat:``/``fix:``, has a body explaining *why*, and carries
  your ``Signed-off-by:`` (DCO).
* Run ``make check-patch`` before you push; it runs the same gate CI does.
