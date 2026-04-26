# Babacoin Versioning Scheme

This document describes how Babacoin Core release versions are numbered
and what each component means. The scheme is adapted from the one used
by Bitoreum (Crystal Bitoreum), which itself is derived from Dash and
Bitcoin Core conventions.

## Current scheme — `[A].[B].[C].[D]`

Babacoin follows a four-part version number:

```
                 A . B . C . D
                 │   │   │   │
                 │   │   │   └─ Patch / minor feature
                 │   │   └───── Critical update or security fix
                 │   └───────── Dash upstream major version
                 └───────────── Babacoin major iteration
```

### Component meanings

**A — Babacoin major iteration.**
Increases when the project undergoes a significant restructuring or
governance transition. The original Babacoin development used `1`.
The current restoration / takeover release line uses `2`. A future
generation that breaks compatibility with the v2 line in some
fundamental way would use `3`.

**B — Dash upstream major version.**
Tracks which Dash Core feature level the codebase is at. Babacoin's
v1.0.0 was based on a code base that corresponds roughly to Dash 0.16
features (DIP-3 deterministic masternodes, ChainLocks, InstantSend
quorums). Future releases that pull in features from newer Dash
versions (e.g. Dash 18 platform features, Dash 19 quorum changes)
will bump this number.

**C — Critical update or security fix.**
Increases when a critical bug is fixed, a security vulnerability is
patched, or a coordinated network upgrade (e.g. spork rotation, hard
fork) is shipped. Users running the previous `C` value MUST upgrade.

**D — Patch / minor feature.**
Bug fixes, optimizations, GUI polish, build improvements, and other
non-critical changes. Skipping a `D` release is safe.

### Example

```
v2.17.1.0
 │  │  │ │
 │  │  │ └─ no patch yet on top of the critical update
 │  │  └─── 1st critical update on the v2.17 line
 │  └────── codebase tracks Dash 17 features
 └───────── 2nd major iteration of Babacoin (post-restoration)
```

A subsequent maintenance release fixing typos in that release would
be `v2.17.1.1`.


## Pre-release identifiers

For builds that are not full production releases, an identifier can be
appended to indicate maturity:

| Suffix | Meaning |
|---|---|
| `-rc.N` | Release candidate (`v2.17.1.0-rc.1`) — feature-frozen, in testing |
| `-dp.N` | Developer preview (`v2.17.0.0-dp.3`) — work in progress, not for production |
| `-beta.N` | Public beta — wider audience, not yet stable |

The choice between `-rc` and `-dp` is roughly:
- `-dp` means "we're not sure this is right yet, but we want feedback"
- `-rc` means "we think this is the release, please find anything we missed"


## Tag and release artifact naming

Git tags and release artifacts use the version exactly as printed,
without a leading `v` prefix in user-facing places where it would be
redundant, but **with** the `v` in tag names for sortability:

- Git tag: `v2.17.1.0`
- GitHub release name: `Babacoin Core v2.17.1.0`
- Binary archive: `babacoin-v2.17.1.0-linux-ubuntu24.04-x86_64.tar.gz`


## Historical mapping

For reference, this is how the existing release(s) fit the new
scheme:

| Old tag | New equivalent | Notes |
|---|---|---|
| `v.1.0.0` | `v1.0.0.0` (legacy) | Original release. Stays at its existing tag for compatibility. |
| `v2.0.0-test` | (deleted) | Bad release, never should have shipped. Removed from repo. |
| `v2.0.1` | (deleted) | Bad release. Removed. |
| `v2.0.2` | (deleted) | Bad release. Removed. |
| (planned) `v2.16.0.0` | First restoration release | Feature-level Dash 16, no critical updates yet, no patch. |

The `v.1.0.0` tag is preserved as-is to avoid breaking any external
references (block explorers, mining pools, exchanges) that link to it.


## Why this scheme?

Most cryptocurrency projects start with a three-part version number
(`major.minor.patch`) inherited from Bitcoin. As they evolve and
diverge from upstream, the meaning of "minor" gets muddy: is a new
feature minor? Is a Dash upstream merge minor? Is a critical security
fix a patch?

The four-part scheme resolves this by making the upstream-tracking
component explicit (`B`) and separating it from project-specific
critical updates (`C`). It's slightly more verbose than necessary for
small projects, but the clarity is worth it for a chain that:

- Is operated by a community rather than a single developer
- Pulls features from upstream Dash periodically
- Occasionally requires coordinated network upgrades (forks)
- Needs unambiguous "must upgrade" vs. "nice to have" signaling

Acknowledgements: this scheme is taken directly from Bitoreum
(Crystal Bitoreum), which faced very similar restoration / takeover
constraints to Babacoin.
