# Babacoin Core — v1.0.0

[![Build Status](https://github.com/babacoinbbc/babacoin/actions/workflows/build-release.yml/badge.svg)](https://github.com/babacoinbbc/babacoin/actions)
[![GitHub Release](https://img.shields.io/github/v/release/babacoinbbc/babacoin)](https://github.com/babacoinbbc/babacoin/releases/tag/v.1.0.0)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

> ### 🚀 Stable release: v1.0.0
>
> v1.0.0 is the current production release running across the network.
> All nodes (mining, smartnodes, wallets) should run this version.
>
> **→ [Download v1.0.0](https://github.com/babacoinbbc/babacoin/releases/tag/v.1.0.0)**
>
> The release page includes a `bootstrap.zip` snapshot for fast first-time
> sync. Place it inside your `~/.babacoin/` directory before starting the
> node.

---

## What is Babacoin?

Babacoin (BBC) is a decentralized financial technology that is fast, reliable, and secure, with negligible transaction costs. It operates on its own blockchain — a fork of the Raptoreum codebase — featuring:

- **ASIC-resistant Proof of Work** — minable with both CPUs and GPUs
- **Smartnode consensus** — immune to 51% attacks
- **CoinJoin privacy** — built-in balance mixing directly within the wallet
- **Low transaction fees** — designed for everyday use

Official website: [https://babacoin.network](https://babacoin.network)  
Block Explorer: [https://explorer.babacoin.network](https://explorer.babacoin.network)

---

## Build Targets

### Platform Support

Full multi-platform build support with automated GitHub Actions CI/CD:

| Platform | Architecture | Status |
|---|---|---|
| Ubuntu 20.04 LTS | x86_64, ARM64 | Supported |
| Ubuntu 22.04 LTS | x86_64, ARM64 | Supported |
| Ubuntu 24.04 LTS | x86_64, ARM64 | Supported |
| Ubuntu 26.04 LTS | x86_64 | Preview — GitHub's 26.04 runner is still in beta |
| macOS Intel | x86_64 | Supported |
| macOS Apple Silicon | arm64 | Supported |
| Raspberry Pi 4 / 5 | ARM64 | Supported |
| Windows | x64 | Supported |

### Technical Changes

- **C++17** standard adopted across all builds
- **OpenSSL 3.x** compatibility — fixed deprecated `SSL_library_init()` call
- **BerkeleyDB 5.3** support — Ubuntu 20.04+ ships `libdb5.3` instead of `libdb4.8`
- **GCC 12+** compatibility — resolved `u_int8_t` type conflict in BDB atomic headers
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

| File | Platform |
|---|---|
| `babacoin-v.1.0.0-linux-ubuntu20.04-x86_64.tar.gz` | Ubuntu 20.04 |
| `babacoin-v.1.0.0-linux-ubuntu22.04-x86_64.tar.gz` | Ubuntu 22.04 |
| `babacoin-v.1.0.0-macos-x86_64.tar.gz` | macOS Intel |
| `babacoin-v.1.0.0-macos-arm64.tar.gz` | macOS Apple Silicon |
| `babacoin-v.1.0.0-raspberry-pi-arm64.tar.gz` | Raspberry Pi 4 / 5 |
| `babacoin-v.1.0.0-windows-x86_64.zip` | Windows x64 |

---

## Building from Source

### Ubuntu 20.04 / 22.04 / 24.04

Install dependencies:

```bash
sudo apt-get update
sudo apt-get install -y \
    build-essential libtool autotools-dev automake pkg-config \
    libssl-dev libevent-dev libboost-all-dev \
    libdb5.3-dev libdb5.3++-dev \
    libminiupnpc-dev libzmq3-dev python3
```

Build:

```bash
git clone https://github.com/babacoinbbc/babacoin.git
cd babacoin
./autogen.sh
./configure \
    --with-incompatible-bdb \
    BDB_LIBS="-ldb_cxx-5.3" \
    BDB_CFLAGS="-I/usr/include"
make -j$(nproc)
```

---

### Ubuntu 18.04 (BDB 4.8)

```bash
sudo apt-get install -y libdb4.8-dev libdb4.8++-dev
./autogen.sh
./configure
make -j$(nproc)
```

---

### macOS (Intel and Apple Silicon)

Install dependencies via Homebrew:

```bash
brew install autoconf automake libtool pkg-config \
    openssl@3 libevent boost miniupnpc zeromq berkeley-db@5
```

Build:

```bash
./autogen.sh
export PKG_CONFIG_PATH="$(brew --prefix openssl@3)/lib/pkgconfig"
export LDFLAGS="-L$(brew --prefix openssl@3)/lib -L$(brew --prefix berkeley-db@5)/lib"
export CPPFLAGS="-I$(brew --prefix openssl@3)/include -I$(brew --prefix berkeley-db@5)/include"
./configure --with-incompatible-bdb
make -j$(sysctl -n hw.logicalcpu)
```

---

### Raspberry Pi 4 / 5 (ARM64 — Native Build)

```bash
sudo apt-get install -y \
    build-essential libssl-dev libevent-dev libboost-all-dev \
    libdb5.3-dev libdb5.3++-dev libzmq3-dev

./autogen.sh
./configure \
    --with-incompatible-bdb \
    BDB_LIBS="-ldb_cxx-5.3" \
    BDB_CFLAGS="-I/usr/include"
make -j4
```

---

### Windows (Cross-compile from Ubuntu)

```bash
sudo apt-get install -y g++-mingw-w64-x86-64 mingw-w64

cd depends
make HOST=x86_64-w64-mingw32 -j$(nproc) NO_QT=1
cd ..

./autogen.sh
CONFIG_SITE=$PWD/depends/x86_64-w64-mingw32/share/config.site \
./configure \
    --host=x86_64-w64-mingw32 \
    --with-incompatible-bdb
make -j$(nproc)
```

---

### Docker

```bash
cd ci
docker build -f Dockerfile.builder -t babacoin-builder .
docker run --rm -v $(pwd)/..:/babacoin-src babacoin-builder bash -c \
  "cd /babacoin-src && ./autogen.sh && \
   ./configure --with-incompatible-bdb BDB_LIBS='-ldb_cxx-5.3' && \
   make -j$(nproc)"
```

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

Every push to a version tag (`v*`) triggers the GitHub Actions pipeline defined in `.github/workflows/build-release.yml`. The workflow compiles binaries for every supported platform in parallel and uploads them automatically to the GitHub Releases page. The Ubuntu 26.04 job is marked `continue-on-error` because GitHub's 26.04 runner is still in preview; if it fails, the rest of the release still publishes.

Manual builds can be triggered from the Actions tab at any time using the `workflow_dispatch` option.

---

## Development

The `main` branch is kept stable at all times. Feature development happens in dedicated branches and is merged through Pull Requests.

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
