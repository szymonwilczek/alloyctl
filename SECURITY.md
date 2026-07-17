<!-- SPDX-License-Identifier: GPL-2.0-only -->
<!-- Copyright (C) 2026 Szymon Wilczek -->

# Security Policy

alloyctl talks to hardware over `/dev/hidraw*` and emits raw device packets, so
the security-relevant surface is memory safety in the C code, path handling when
locating hidraw nodes, device commands that could corrupt or brick a mouse, and
the integrity of released binaries.

## Reporting a vulnerability

Report privately through
[GitHub Private Vulnerability Reporting](https://github.com/szymonwilczek/alloyctl/security/advisories/new)
for `github.com/szymonwilczek/alloyctl`. If you cannot use that, email
**swilczek.lx@gmail.com** with the details.

Please do **not** open a public issue for an exploitable vulnerability until it
has been triaged and a fix or disclosure is coordinated.

Include, as far as you can:

* affected version (see the `VERSION` file or the release tag),
* the driver or component involved and the mouse model, if hardware-specific,
* a description of the impact and, ideally, steps or a proof of concept to
  reproduce it.

## Supported versions

alloyctl is pre-1.0 and ships from a single line of development. Security fixes
land on `alloy-next` and go out in the next release; there are no separate
maintenance branches for older versions yet. Always test against the latest
release before reporting.

## Release integrity

Every release ships a `SHA256SUMS` manifest signed with cosign keyless. Verify a
download before trusting it:

```sh
cosign verify-blob \
  --bundle SHA256SUMS.cosign.bundle \
  --certificate-identity-regexp \
    '^https://github\.com/szymonwilczek/alloyctl/\.github/workflows/release\.yml@refs/tags/v' \
  --certificate-oidc-issuer https://token.actions.githubusercontent.com \
  SHA256SUMS
sha256sum -c SHA256SUMS
```
