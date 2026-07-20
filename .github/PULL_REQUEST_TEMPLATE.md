<!-- SPDX-License-Identifier: GPL-2.0-only -->

## What and why

<!-- What does this change do, and why? Link any issue it closes. -->

## Checklist

- [ ] This PR targets **`alloy-next`**, not `main`.
- [ ] `make format` run; `make test` passes.
- [ ] Commits are `feat:` / `fix:`, each with a body and a `Signed-off-by:`
      (DCO) trailer.
- [ ] `make check-patch` passes locally.

### Adding a new driver? Also:

- [ ] Dedicated directory under `drivers/` containing the driver C file (with `Maintainer:`) and optionally a custom `_art.txt` file.
- [ ] Packet-builder tests under `tests/drivers/`.
- [ ] Protocol notes under `Documentation/protocol/`.
- [ ] A `MAINTAINERS` entry for the new driver (you become its maintainer).
