<!-- SPDX-License-Identifier: GPL-2.0-only -->
<!-- Copyright (C) 2026 Szymon Wilczek -->

# Governance

alloyctl is a community-driven project with a single lead. Its aim is to make
adding a mouse as easy as writing one driver file, while keeping the released
code trustworthy. Governance is deliberately lightweight and modelled based on
the rule: **being a maintainer is a responsibility, not a grant of power.**

## Roles

### Project lead

Szymon Wilczek (**@szymonwilczek**) is the project lead. The lead:

- merges every pull request -- no one else has merge or commit access;
- owns `main` and cuts releases by pushing tags (see
  [branch model](Documentation/contributor/branching.rst));
- has the final say on technical direction and disputes;
- grants and revokes repository collaborator access;
- enforces the [Code of Conduct](CODE_OF_CONDUCT.md).

### Driver maintainer

Anyone listed on an `M:` line in [MAINTAINERS](MAINTAINERS) for a driver. **The
first person to contribute a working driver becomes its maintainer** by default.

A driver maintainer is *responsible* for their driver:

- reviewing pull requests that touch it (GitHub requests their review through
  `.github/CODEOWNERS`, or the review workflow @-mentions them if they are not
  a collaborator);
- keeping it working and answering questions about it;
- setting an honest status on the `S:` line -- `Maintained` if they will look
  after it, `Odd Fixes` if only occasionally.

This role does **not** grant merge or commit access. You own the review and upkeep
of a file, not the tree.

### Contributor

Anyone who opens a pull request. Contributors do not need to be maintainers or
collaborators; every change is welcome through the normal pull-request flow onto
`alloy-next`.

### Collaborator (earned)

A collaborator is a maintainer the lead has granted repository access to. This
is the tier where *trust* -- not just responsibility -- is recognised: only a
collaborator can be requested as a review by `CODEOWNERS` natively.

Collaborator access is **earned, not automatic**. The lead grants it to a
maintainer who has shown a sustained, reliable track record. A brand-new driver
author is a maintainer immediately (responsibility) but not a collaborator
(power) until they have earned it.

## Driver lifecycle

- **New driver.** Lands on `alloy-next` through a pull request that adds the
  driver, its tests, its protocol notes, a `Maintainer:` header line, and a
  `MAINTAINERS` entry with `S: Maintained` or `S: Odd Fixes`.
- **Handover.** A maintainer may pass the role on by changing the `M:` line in a
  pull request the new maintainer agrees to.
- **Orphaned.** If a maintainer stops responding, the lead sets the driver's
  status to `S: Orphan`. Anyone may then adopt it by putting their own name on
  the `M:` line.
- **Removal.** A driver that stays orphaned and broken may be removed. It can
  always be brought back by a new maintainer.

## Decisions and reviews

- Development happens on `alloy-next`; `main` carries only released code.
- A pull request that touches a driver should be reviewed by that driver's
  maintainer. The lead makes the final merge decision and performs the merge.
- Disagreements are resolved by discussion; if consensus is not reached, the
  lead decides.
- Promotion of `alloy-next` to `main`, and every release tag, is done by the
  lead alone.

## Changing this document

Governance changes are proposed by pull request and decided by the lead.

See also: [CONTRIBUTING](CONTRIBUTING.rst),
[MAINTAINERS](MAINTAINERS),
[branch model](Documentation/contributor/branching.rst),
[Code of Conduct](CODE_OF_CONDUCT.md),
[Security policy](SECURITY.md).
