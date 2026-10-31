# Babacoin Core

[![Build Status](https://github.com/babacoinbbc/babacoin/actions/workflows/build-release.yml/badge.svg)](https://github.com/babacoinbbc/babacoin/actions)
[![GitHub Release](https://img.shields.io/github/v/release/babacoinbbc/babacoin)](https://github.com/babacoinbbc/babacoin/releases/tag/v.1.0.0)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

> ### 🚀 Stable release: v1.0.0
>
> v1.0.0 is the latest published stable release. This does not establish which
> versions every active network participant currently runs.
> The `main` source identifies itself as v2.0.0 and contains additional network
> rules. Do not treat it as an OS-only replacement for this release. See the
> [compatibility validation guide](doc/ubuntu-compatibility.md).
> Yerel inceleme bulguları: [Türkçe denetim raporu](doc/audit-2026-10-30-tr.md).
>
> **→ [Download v1.0.0](https://github.com/babacoinbbc/babacoin/releases/tag/v.1.0.0)**
>
> The release page includes a `bootstrap.zip` snapshot. The daemon does not
> automatically import ZIP files. Never extract a snapshot over an existing
> wallet or data directory. Validate its raw block files in a separate directory
> before considering it for a new installation; copied chainstate is not proof
> that the blocks have been validated.

---

## What is Babacoin?

Babacoin (BBC) is a decentralized financial technology that is fast, reliable, and secure, with negligible transaction costs. It operates on its own blockchain — a fork of the Raptoreum codebase — featuring:

- **ASIC-resistant Proof of Work** — minable with both CPUs and GPUs
- **Smartnodes and quorum-based ChainLocks** — protection depends on active quorums and spork configuration
- **CoinJoin privacy** — built-in balance mixing directly within the wallet
- **Low transaction fees** — designed for everyday use

Official website: [https://babacoin.network](https://babacoin.network)  
Block Explorer: [https://explorer.babacoin.network](https://explorer.babacoin.network)

---

## Build Targets

### Platform Support

Ubuntu LTS validation targets are 18.04, 20.04, 22.04, 24.04 and 26.04 on
x86_64 and ARM64. The workflow builds in each target Ubuntu userspace and runs
unit/GUI tests and an isolated wallet smoke test. A configured target does not
mean its checks have passed. See [validation and wallet portability](doc/ubuntu-compatibility.md)
for reproduction steps and limitations. Windows, macOS and Raspberry Pi hardware
need their own runtime validation.

### Technical Changes

- **C++17** standard adopted across all builds
- **OpenSSL 3.x** compatibility — fixed deprecated `SSL_library_init()` call
- **Portable wallets** — Berkeley DB 4.8 is required by default; other database versions require explicit opt-in
- **BDB compiler compatibility** — retain the atomic builtin patch and verify the BDB 4.8 source archive
- **CI/CD** — Dockerfile.builder updated to Ubuntu 22.04 LTS
- **GitHub Actions** — automated build and release pipeline for all supported platforms

### Network Changes

These are consensus and network rules. Every change below is height-gated except the
spork key, which takes effect the moment a node upgrades.

| Change | New value | Activates at |
|---|---|---|
| Smartnode collateral | `2,000,000 BBC` (was 1,800,000) | block **1,250,000** |
| Founder / dev wallet | `B6uz2zqVRvL6RWw9w1vYmahiuQPRm9G8gT` | block **1,250,000** |
| Spork key | `BH9codHixVqU6ShdJXgaEQ3xZjXKEs1P2P` | on upgrade (no height gate) |

The founder and spork keys were rotated because the previous wallet was compromised.
The historical founder address (`BRBeLPQNg7PMJa9BfqB2U2JY6EjQPEDjFF`) and every past
collateral tier remain in the consensus tables — removing them would invalidate blocks
that already paid them and make the existing chain unsyncable.

#### What operators must do

- **Smartnode operators** — top up collateral from 1,800,000 to 2,000,000 BBC before
  block 1,250,000. Nodes still at 1,800,000 stop earning at that height. You can raise
  collateral early; 2,000,000 BBC is accepted before the activation height too.
- **Miners** — upgrade before block 1,250,000. From that height the coinbase must pay
  the new founder address; blocks paying the old one will be rejected.
- **Spork key holder** — record `spork show` output *before* upgrading, then re-sign and
  broadcast all sporks with the new key *immediately after*. Sporks signed by the old key
  are dropped on upgrade and every spork reverts to its default of OFF, including
  `SPORK_19_CHAINLOCKS_ENABLED`. The network has no ChainLocks protection until they are
  re-broadcast.

---

## Download

Pre-built binaries are available on the [Releases page](https://github.com/babacoinbbc/babacoin/releases/tag/v.1.0.0):

| Published v1.0.0 file | Platform |
|---|---|
| `babacoin-ubuntu18-1.0.0-SNAPSHOT.zip` | Ubuntu 18.04 |
| `babacoin-ubuntu20-1.0.0-SNAPSHOT.zip` | Ubuntu 20.04 |
| `babacoin-macos-1.0.0-SNAPSHOT.zip` | macOS |
| `babacoin-win-1.0.0-SNAPSHOT.zip` | Windows x64 |

These are legacy release assets, not evidence that the edited source has passed
compatibility validation. New Ubuntu build outputs include separate SHA-256 files.

---

## Building from Source

### Ubuntu 18.04 through 26.04 LTS

Follow the [Ubuntu compatibility guide](doc/ubuntu-compatibility.md). It supplies
an isolated build path, BDB 4.8 source verification, the pinned BLS dependency,
and tests. Do not open a production wallet as a build test.

### Raspberry Pi 4 / 5

Use the same ARM64 Ubuntu build path from the guide on a 64-bit OS. The native
ARM64 matrix builds replace the former separate untested Pi package path.
Raspberry Pi hardware still needs its own runtime validation.

### Docker

Use the disposable container command in the [compatibility guide](doc/ubuntu-compatibility.md).
It builds BDB 4.8 and BLS explicitly, retains tests, and records library/package
versions. Do not substitute the system BDB 5.3 for a portable wallet build.

### Windows and macOS

Existing cross-platform build recipes are in `.github/workflows/build-release.yml`
and `.github/workflows/build-missing.yml`. These platforms require their own
validation. In particular, the legacy macOS recipe explicitly opts into a
non-portable system Berkeley DB; an Ubuntu result does not validate that wallet
format or make it safe to exchange production wallet files across these builds.

---

## Running the Node

Start the daemon:

```bash
./src/babacoind -daemon
```

Check sync status:

```bash
./src/babacoin-cli getblockcount
./src/babacoin-cli getinfo
```

Stop the daemon:

```bash
./src/babacoin-cli stop
```

Default data directories:

| OS | Path |
|---|---|
| Linux | `~/.babacoincore/` |
| macOS | `~/Library/Application Support/BabacoinCore/` |
| Windows | `%APPDATA%\BabacoinCore\` |

---

## Smartnodes

Smartnodes secure the network, enable InstantSend, and provide CoinJoin mixing. Operators earn a share of block rewards in return.

### Collateral Schedule

| Block Range | Required Collateral |
|---|---|
| 1 — 88,720 | 600,000 BBC |
| 88,721 — 132,720 | 800,000 BBC |
| 132,721 — 176,720 | 1,000,000 BBC |
| 176,721 — 220,720 | 1,250,000 BBC |
| 220,721 — 264,720 | 1,500,000 BBC |
| 264,721 — 1,249,999 | 1,800,000 BBC |
| **1,250,000+** | **2,000,000 BBC** |

Existing 1,800,000 BBC smartnodes keep earning until block 1,249,999. Operators must
top up to 2,000,000 BBC before that height to continue receiving rewards; collateral
can be raised early, as 2,000,000 BBC is accepted before the activation height too.

### Reward Distribution

Block reward — split from the 5,000 BBC subsidy plus regular fees:

| Share | Recipient | Source |
|---|---|---|
| **20%** | Smartnode operators | `nCollaterals` reward percentage, from block 5,761 |
| **5%** | Founder / development fund | `nFounderPayment`, from block 250 |
| **75%** | Miner | remainder of the coinbase |

Special transaction fees are split separately and do **not** follow the percentages
above (`nFutureRewardShare`):

| Share | Recipient |
|---|---|
| **80%** | Smartnode operators |
| **20%** | Miner |
| **0%** | Founder |

Smartnode payments only start once the network has at least 10 smartnodes; below that
the entire block reward goes to the miner.

Founder address: `B6uz2zqVRvL6RWw9w1vYmahiuQPRm9G8gT` (from block 1,250,000)

| Block Range | Founder Address |
|---|---|
| 250 — 1,249,999 | `BRBeLPQNg7PMJa9BfqB2U2JY6EjQPEDjFF` |
| **1,250,000+** | **`B6uz2zqVRvL6RWw9w1vYmahiuQPRm9G8gT`** |

---

## Network Parameters

| Parameter | Value |
|---|---|
| Ticker | BBC |
| Algorithm | GhostRider (ASIC-resistant) |
| Block Time | 2 minutes |
| Block Reward | 5,000 BBC |
| Halving Interval | 210,240 blocks (~400 days) |
| P2P Port | 6678 |
| RPC Port | 6679 |
| Address Prefix | `B` |
| Genesis Block | `84de4877419c696744198422de2628087ae2270c73fb370ab4cfe2fe01061854` |

### DNS Seeds

```
seed00.babacoin.network
seed01.babacoin.network
seed02.babacoin.network
seed03.babacoin.network
seed04.babacoin.network
seed05.babacoin.network
seed06.babacoin.network
seed07.babacoin.network
seed08.babacoin.network
seed09.babacoin.network
seed10.babacoin.network
```

---

## Problems Babacoin Aims to Solve

Babacoin aspires to create a transparent and scalable financial system that makes cryptocurrencies accessible to everyone:

**1. User-Friendly Wallets**  
Mobile wallets for Android and iOS to simplify cryptocurrency management for everyday users.

**2. Cryptocurrency Adoption**  
A payment gateway with a free plugin for small businesses. Customers pay in BBC; merchants receive fiat currency directly to their accounts.

**3. Exchange Accessibility**  
Removing high listing fees and launching Bitroeum Exchange — an integrated platform where any coin can be listed and traded against Bitroeum.

**4. Inclusive Financial Opportunities**  
Smartnode deployment with modest collateral requirements, allowing anyone to earn BBC while contributing to network security and stability.

Full roadmap: [https://babacoin.network](https://babacoin.network)

---

## Automated CI/CD

Version tags (`v*`) trigger `.github/workflows/build-release.yml`. The Ubuntu
matrix runs on 18.04, 20.04, 22.04, 24.04 and 26.04 userspaces using currently
available 24.04 host runners. Every Ubuntu build/test job must succeed before a
**draft** release can be assembled. Existing releases are preserved. There is no
optional failing Ubuntu target and no automatic public publication.

Manual builds remain available through `workflow_dispatch`. The legacy missing
platforms workflow only creates build artifacts; it no longer overwrites a release.

---

## Development

Changes must pass compatibility and safety checks before release. Review network
rule differences separately from build changes; a successful compilation is not
a network compatibility or fund-safety certification.

Run unit tests:

```bash
make check
```

Run functional tests:

```bash
test/functional/test_runner.py
```

Please read [CONTRIBUTING.md](CONTRIBUTING.md) before submitting a Pull Request. All contributions are welcome.

---

## License

Babacoin Core is released under the **MIT License**.  
See [COPYING](COPYING) or [https://opensource.org/licenses/MIT](https://opensource.org/licenses/MIT).

---

## Links

| Resource | URL |
|---|---|
| Website | https://babacoin.network |
| Block Explorer | https://explorer.babacoin.network |
| GitHub | https://github.com/babacoinbbc/babacoin |
| Releases | https://github.com/babacoinbbc/babacoin/releases |
