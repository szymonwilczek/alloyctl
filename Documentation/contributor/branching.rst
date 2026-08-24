.. SPDX-License-Identifier: GPL-2.0-only
.. Copyright (C) 2026 Szymon Wilczek

=========================
Branch model and releases
=========================

alloyctl follows the standard GitHub Flow model centered around the ``main`` branch:

The primary development and release branch. All pull requests are opened against
and merged into ``main``. Every release tag points to a commit on ``main``.

Where to open pull requests
===========================

Open every pull request directly against ``main``.

Feature branches (such as ``driver/<name>``, ``feat/<name>``, or ``fix/<name>``)
should be branched off the latest ``main`` and kept up to date via rebase.

Releases
========

Releases are cut by the maintainer pushing a signed tag on ``main``; nobody else publishes
releases. The version lives in the bare ``VERSION`` file at the repository root, and a
tag must equal ``v`` + that file (``make check-version-tag`` enforces it). The single
portable ``alloyctl`` binary is the release artifact, so one build serves every distribution.

Pushing a ``vX.Y.Z`` tag triggers the ``Release`` workflow, which builds the binary,
writes a ``SHA256SUMS`` manifest, signs it with cosign keyless, and publishes a GitHub
Release:

Pre-release
    Any tag carrying a pre-release suffix (such as ``-rc1``) publishes as a **pre-release**.

Release
    Standard release tags (such as ``v0.3.0``) publish as a normal public release.
