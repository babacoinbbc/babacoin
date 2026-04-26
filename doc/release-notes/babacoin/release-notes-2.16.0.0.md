# Babacoin Core v2.16.0.0 — Release Notes (DRAFT)

**Status:** Draft. Not released. This file describes the planned
contents of the first restoration-phase release. It will be finalized
and dated once testnet validation completes.

---

## Overview

This is the **first release of the Babacoin v2 restoration line**. It
focuses on operational stability — making the chain easy to sync, easy
to bootstrap, and easy to maintain — without changing consensus rules
or token economics.

It is a **drop-in upgrade** from v1.0.0. No hard fork, no migration,
no breaking changes. Existing wallets and smartnodes continue to work
without modification.

For the long-form story behind this release line, see the
[Restoration phase notice](../README.md) and
[CONTRIBUTING.md](../CONTRIBUTING.md).

For the version-numbering scheme, see
[doc/versioning.md](versioning.md).

---

## What changed

### Sync reliability — checkpoints

Mainnet `chainparams.cpp` now ships with 16 checkpoints spanning
genesis to block 925,000, derived from a fully-synced mainnet node.
Combined with `nMinimumChainWork` and `defaultAssumeValid` set to
match block 925,000, this lets new nodes:

- Skip per-block PoW re-validation up to the last checkpoint
- Reject any chain shorter than the established one
- Skip signature validation for blocks at or below 925,000 during
  initial sync (purely a performance change — full validation still
  happens for everything above)

The previous v1.0.0 chainparams shipped with only the genesis
checkpoint, which meant every new node had to fully validate the
entire ~929k-block history against GhostRider PoW. On modest hardware
this could take many hours.

**Effect:** dramatically faster initial sync for new nodes; no change
for existing synced nodes.

### Sync reliability — collateral map correction

`SmartnodeCollaterals` now contains the single collateral value that
has actually been used on the chain throughout its history:

```cpp
{ {INT_MAX, 1800000 * COIN} }
```

The previous v1.0.0 chainparams listed `10,000,000 BBC` here, but no
smartnode has ever been registered at that amount. The mismatch caused
historical ProRegTx to fail validation with `bad-protx-collateral`,
blocking IBD on certain code paths.

This was verified empirically by scanning all 568 currently-active
smartnodes and resolving each one's collateral output via
`getrawtransaction`. Result: 568/568 at exactly 1,800,000 BBC.

**Effect:** historical blocks now validate correctly during IBD; no
behavior change for anyone running a node that's already synced.

**Note on future changes:** A coordinated collateral increase is
planned for a later release as a way to retire zombie smartnodes left
by defunct MNaaS providers. That is a separate change and is *not*
in this release.

### Bootstrap distribution

Two new pieces of infrastructure for fast first-time sync:

1. **`doc/bootstrap.md`** — user-facing guide explaining how to use
   `bootstrap.zip` and `powcache.dat` on Linux, macOS, and Windows.
2. **`contrib/install.sh`** — the Linux one-shot installer now
   automatically downloads `bootstrap.zip` and `powcache.dat` from
   the latest GitHub release, on fresh installs only. Existing
   data directories are not touched. `BBC_BOOTSTRAP=0` opts out.

Releases of `bootstrap.zip` and `powcache.dat` are produced by hand
from a fully-synced node and attached to the GitHub release. They
are signed by the release maintainer.

### Governance and documentation

- **`CONTRIBUTING.md`** — adds a Restoration Phase project-status
  section above the existing contributor workflow. New contributors
  can see at a glance what work is happening and how to get involved.
- **`doc/versioning.md`** (new) — documents the four-part
  `[A].[B].[C].[D]` version scheme used going forward.
- **`README.md`** — adds a Restoration Phase callout and links to
  the contribution and versioning docs.

These are documentation-only changes; no code or consensus rules are
affected.

---

## What did NOT change

This is intentionally a small, conservative release. The following are
out of scope:

- **Consensus rules** — not touched.
- **Spork pubkey** — same as v1.0.0. Spork rotation is planned for a
  future release as a coordinated upgrade.
- **Founder address** — same as v1.0.0.
- **Block reward / emission schedule** — same as v1.0.0.
- **Smartnode collateral amount** — still 1,800,000 BBC.
- **Genesis, magic bytes, P2P port, RPC port** — unchanged.
- **GhostRider PoW algorithm** — unchanged.

If you're a smartnode operator or miner, this release does not require
any operational change beyond installing the new binary.

---

## Upgrading

### From v1.0.0

```bash
# Linux (manual)
sudo systemctl stop babacoind
# replace binaries with the v2.16.0.0 build
sudo systemctl start babacoind

# Linux (one-shot installer, fresh nodes)
curl -fsSL https://raw.githubusercontent.com/babacoinbbc/babacoin/main/contrib/install.sh | bash

# macOS / Windows
# download the GUI build from the release page and install over the existing one
```

`wallet.dat` and `babacoin.conf` are untouched. Chain data is
compatible — no reindex required.

### From v2.0.x test releases

Those releases were broken and have been removed from the repository.
If your node has been running v2.0.0-test, v2.0.1, or v2.0.2, the
chain data may be corrupt:

```bash
sudo systemctl stop babacoind
cd ~/.babacoin
rm -rf blocks chainstate evodb llmq peers.dat banlist.dat anchors.dat
# wallet.dat and babacoin.conf NOT touched
# install v2.16.0.0
sudo systemctl start babacoind
# alternatively, fetch bootstrap.zip from the release page first
# to skip the from-scratch sync
```

---

## Test methodology

Before tagging v2.16.0.0, the changes in this branch will be validated
on testnet:

1. Build a `dev/v2.0.0` binary and deploy it to a testnet node.
2. Verify that the node syncs cleanly from genesis using the new
   checkpoint list.
3. Verify that ProRegTx validation passes for historical and future
   blocks.
4. Run the node for at least one week without a regression.
5. Deploy to one mainnet observer node (no impact to other users)
   and verify it tracks the network for at least one week.

Only after both pass does this become an actual `v2.16.0.0` tag.

---

## Acknowledgements

- The original Babacoin developers (v1.0.0 codebase)
- The Bitoreum / Crystal Bitoreum project, whose restoration model
  this release line draws inspiration from
- The Yerbas project, for the checkpoint and bootstrap patterns
  used here
- Everyone who runs a node, mines, or operates a smartnode on the
  network — the chain has been alive because of you.
