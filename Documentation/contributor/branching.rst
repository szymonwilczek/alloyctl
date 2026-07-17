.. SPDX-License-Identifier: GPL-2.0-only
.. Copyright (C) 2026 Szymon Wilczek

=========================
Branch model and releases
=========================

alloyctl uses a two-branch model:

``main``
    Stable, released code only. ``main`` is the default branch, so it is the
    first thing a visitor sees, and every commit on it corresponds to a
    published release.

``alloy-next``
    The integration branch. **All development lands here first.** New drivers,
    fixes, and documentation are merged into ``alloy-next``, shaken out, and
    only then promoted to ``main``.

Where to open pull requests
===========================

Open every pull request against ``alloy-next``.

``main`` is the default branch, so GitHub will offer it first; if you open a
pull request against ``main`` by mistake, the ``PR router`` workflow
automatically retargets it to ``alloy-next`` and leaves a comment. If that
surfaces conflicts, rebase your branch onto ``alloy-next`` and force-push.

Promotion to ``main`` happens through a single ``alloy-next`` -> ``main`` pull
request that **only the maintainer opens**. A contributor never merges into
``main`` directly.

Releases
========

Releases are cut by the maintainer pushing a signed tag; nobody else publishes
releases. The version lives in the bare ``VERSION`` file at the repository
root, and a tag must equal ``v`` + that file (``make check-version-tag``
enforces it). The single portable ``alloyctl`` binary is the release artifact,
so one build serves every distribution.

Pushing a ``vX.Y.Z`` tag triggers the ``Release`` workflow, which builds the
binary, writes a ``SHA256SUMS`` manifest, signs it with cosign keyless, and
publishes a GitHub Release. Whether it is a pre-release depends on the branch
the tag sits on:

Pre-release
    A tag whose commit is on ``alloy-next`` but not yet on ``main`` (or any tag
    carrying a ``-rc`` suffix) publishes as a **pre-release**. This is how an
    integration build is shared for testing.

Release
    Once ``alloy-next`` is promoted to ``main`` and the tag's commit is part of
    ``main``, the same tag publishes as a normal release.

So the typical flow is: land pull requests on ``alloy-next``, maintainer tag release
candidates there as pre-releases, and when a candidate is solid, maintainer promotes
``alloy-next`` to ``main`` and tags the final version there.
