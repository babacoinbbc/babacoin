# Bootstrap & PoW Cache — Quick Start Guide

This document explains how to use the `bootstrap.zip` and `powcache.dat`
files distributed alongside Babacoin Core releases to skip the long
initial sync.

## Why use a bootstrap?

Babacoin uses the GhostRider Proof-of-Work algorithm. Validating PoW
during the initial blockchain download (IBD) is computationally
expensive. A first-time sync of the full chain (currently ~929,000+
blocks) can take many hours on modest hardware.

Two files speed this up:

1. **`bootstrap.zip`** — a snapshot of the `blocks/`, `chainstate/`,
   `evodb/`, and `llmq/` directories from a fully-synced node. Drop
   this in your data directory and the wallet imports it on first
   startup, jumping to the snapshot's block height.

2. **`powcache.dat`** — a cache of GhostRider PoW computations. Even
   when validating cached blocks, GhostRider re-hashes can be slow.
   This file pre-populates the cache and dramatically reduces CPU
   load during validation.

## Where to put the files

The data directory location depends on your operating system:

| OS | Default path |
|---|---|
| Linux | `~/.babacoin/` |
| macOS | `~/Library/Application Support/Babacoin/` |
| Windows | `%APPDATA%\Babacoin\` (typically `C:\Users\<You>\AppData\Roaming\Babacoin\`) |

If you've configured a custom `-datadir`, use that path instead.

## Step-by-step usage

### Linux / macOS

```bash
# 1. Stop the wallet if it's running
babacoin-cli stop          # or: sudo systemctl stop babacoind

# 2. Back up your wallet.dat (always, before any datadir surgery)
cp ~/.babacoin/wallet.dat ~/wallet.dat.backup-$(date +%Y%m%d)

# 3. Remove existing chain data (do NOT touch wallet.dat or babacoin.conf)
cd ~/.babacoin
rm -rf blocks chainstate evodb llmq peers.dat banlist.dat anchors.dat

# 4. Download the latest bootstrap from the releases page
wget https://github.com/babacoinbbc/babacoin/releases/latest/download/bootstrap.zip

# 5. Extract into the data directory
unzip bootstrap.zip
rm bootstrap.zip

# 6. (Optional but recommended) Add the powcache
wget https://github.com/babacoinbbc/babacoin/releases/latest/download/powcache.dat

# 7. Start the wallet
babacoind -daemon            # or: sudo systemctl start babacoind

# 8. Verify (give it ~30 seconds to load)
sleep 30
babacoin-cli getblockchaininfo | grep -E '"blocks"|"headers"'
```

You should see `blocks` and `headers` jump to the snapshot's height
within a few minutes (rather than hours of from-zero IBD).

### Windows

1. Close Babacoin Core completely (also check the system tray).
2. Make a copy of `wallet.dat` from `%APPDATA%\Babacoin\` to a safe
   location.
3. In `%APPDATA%\Babacoin\`, delete the following — but **do not**
   delete `wallet.dat` or `babacoin.conf`:
   - `blocks` (folder)
   - `chainstate` (folder)
   - `evodb` (folder)
   - `llmq` (folder)
   - `peers.dat`
   - `banlist.dat`
   - `anchors.dat` (if present)
4. Download `bootstrap.zip` from the latest release page.
5. Extract its contents directly into `%APPDATA%\Babacoin\`. After
   extraction the data directory should once again contain `blocks/`,
   `chainstate/`, etc.
6. (Optional) Download `powcache.dat` and place it in the same
   data directory.
7. Launch Babacoin Core. The wallet will load the imported chain
   data and resume normal sync from the snapshot height.

## Verification

After startup, the GUI's debug window (Tools → Information) or the
CLI command `babacoin-cli getblockchaininfo` should show:

- `blocks`: at or near the bootstrap's snapshot height
- `headers`: same or slightly higher (catching up to network tip)
- `verificationprogress`: close to 1.0
- `initialblockdownload`: still `true` until you fully catch up

The wallet then resumes a normal incremental sync for any new blocks
produced since the snapshot.

## Frequently asked questions

**Is this safe? Could the bootstrap be tampered with?**
The chain data is validated against the PoW and the on-chain consensus
rules even when imported from a bootstrap. Additionally, the
`checkpointData` compiled into the binary will reject any chain that
doesn't match the recorded hashes. A tampered bootstrap would either
fail validation or be rejected at the next checkpoint.

That said, only download `bootstrap.zip` from the official Babacoin
GitHub releases page — never from third-party mirrors.

**Do I need to use the bootstrap?**
No. A normal first sync from peers will produce identical chain data;
it just takes longer. Bootstraps are purely a performance optimization.

**My wallet.dat is from a much older version. Is it compatible?**
Yes. `wallet.dat` format is forward-compatible across Babacoin Core
versions. The bootstrap only replaces the chain data, not your wallet.

**Can I share my own synced node's data as a bootstrap?**
Yes — that's how official bootstraps are produced. Stop the node,
`tar czf bootstrap.tar.gz blocks chainstate evodb llmq powcache.dat
sporks.dat`, and you have a snapshot. For a public release, sign it
with the project's release key.
