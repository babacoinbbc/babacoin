# Babacoin Core V2

**A more capable, accessible, non-custodial BBC wallet on the existing Babacoin blockchain.**

[Releases](https://github.com/babacoinbbc/babacoin/releases) ·
[Build and test results](https://github.com/babacoinbbc/babacoin/actions) ·
[Migration and settings](doc/v2-migration.md) ·
[Platform support](doc/platform-support.md)

## V2.0.0 Release Candidate 1

V2 combines a redesigned day/night desktop wallet, safer wallet handling and a
larger, test-gated platform matrix. Download the matching package from the
[V2.0.0-rc.1 release page](https://github.com/babacoinbbc/babacoin/releases/tag/v2.0.0-rc.1).
Read its release status, checksums, source manifest and migration notes first.
Release assets are assembled only after every required platform job passes.

This is a **release candidate**, not a claim that all historical node versions
agree or that software is free of defects. V1 and the earlier V2 test release
remain preserved. Publishing a candidate does not remotely upgrade nodes or
activate a new blockchain. Production adoption and network-rule changes require
separate operator coordination and review.

## What changes in V2

- **Day and night themes:** persistent appearance, keyboard switching and a
  redesigned navigation panel, balance summary and recent-activity view.
- **Clear financial information:** the existing balance calculations, pending,
  immature and watch-only fields remain intact. Activity bars describe only the
  displayed recent transactions; they are not price or investment-return charts.
- **Better startup feedback:** the splash appears before application font setup;
  unnecessary system-font logging is removed. A pixel-font crash has a regression
  test. Wallet verification, rescans and block validation remain enabled.
- **Safer persistence:** interrupted startup rescans retain the previous checkpoint
  and retry instead of skipping unread transactions; corrected disk-index field decoding, strict BDB 4.8.30
  selection, fail-closed backups and test reports that reject premature exits.
- **Correct peer attribution:** invalid mixed-sender DKG messages no longer blame
  a single unrelated sender without the individual checks.
- **Broader packaging:** Ubuntu LTS, Debian, Windows and both Mac architectures,
  plus explicit ARM64 and Windows-on-ARM emulation validation targets.
- **Isolated previews:** every package includes a launcher that creates new
  regtest data and separate GUI preferences with external P2P disabled.

The wallet remains a full-node client. Blockchain synchronization, wallet loading
and a requested rescan can take substantial time; a prettier interface does not
remove those safety checks.

## Platform coverage

| Platform | Package / validation scope |
|---|---|
| Ubuntu 18.04, 20.04, 22.04, 24.04, 26.04 LTS | Separate x86_64 and ARM64 userspaces; full native build/test gate |
| Debian 12 and 13 | Separate x86_64 and ARM64 userspaces; full native build/test gate |
| Raspberry Pi OS 64-bit | Debian 12/13 ARM64 compatibility target; physical Pi hardware/display testing remains separate |
| Windows x64 | Native UCRT64 build; tests on a separate clean Windows runner |
| Windows 11 ARM64 | The **same x64 Windows package under emulation**; no native ARM64 Windows executable is claimed |
| macOS Intel / Apple Silicon | Separate native builds/tests; bundled GUI, ad hoc signed |

The [support matrix](doc/platform-support.md) distinguishes configured targets,
actual completed jobs, hardware testing and unsupported systems. There is no
universal binary for every operating system. Android/iOS, 32-bit Windows/ARM,
FreeBSD and other systems are roadmap work, not shipped support. Old operating
systems still require appropriate vendor security maintenance.

## Try the interface without touching a production wallet

Extract the package into a **new directory** and run:

- Windows: `TEST-WALLET.cmd`
- Linux/macOS: `bash TEST-WALLET.sh`

Each launch creates fresh temporary regtest data and GUI preferences. No
production wallet is copied or opened. Test coins have no mainnet value.
Use **Day / Night** or **Ctrl+Shift+L** to change appearance.

Directly starting `babacoin-qt` or the Mac application uses normal settings and
can open an existing wallet. Read [migration instructions](doc/v2-migration.md)
before doing that. Never put your real `wallet.dat` into a disposable test folder.

## Existing network, explicit rules

V2 retains the audited `main` network identity and schedules; this release adds
no new collateral activation, reward schedule or genesis block.

| Parameter | Existing mainnet value |
|---|---|
| Ticker | BBC |
| Proof of Work | GhostRider |
| Target block spacing | 120 seconds |
| P2P port | `6678` |
| RPC port | `17668`; keep RPC private |
| Genesis hash | `84de4877419c696744198422de2628087ae2270c73fb370ab4cfe2fe01061854` |
| Message start | `03 5d d7 2f` |
| Public-key / script / private-key version bytes | `25 / 102 / 205` |

The existing `main` source already differs from published V1. In particular:

| Existing rule | Schedule / behavior |
|---|---|
| Smartnode collateral | 1,800,000 BBC through block 1,249,999; 2,000,000 BBC from block 1,250,000 |
| Founder destination schedule | Existing address change at block 1,250,000; enforcement must be read with the actual validation rules |
| Spork key rotation | Takes effect on upgrade, not through a new height gate |

All historical collateral tiers remain recorded so historical registrations can
still be decoded and validated. Registration validity and current payment
eligibility are different checks. V1 has no 2,000,000-BBC final tier. Do not infer
network-wide agreement from matching ports or a successful P2P handshake.

**No automatic collateral top-up or coin movement is performed.** An existing
collateral UTXO cannot be enlarged by sending additional coins to its address.
Smartnode owners must use a separately reviewed registration/migration process.
ChainLocks and related protections also depend on active quorums and spork state;
never assume those protections are active solely because the software runs.

## Settings and data

Default data directories, unless `-datadir` or saved GUI preferences override them:

| OS | Default directory |
|---|---|
| Linux | `~/.babacoin/` |
| macOS | `~/Library/Application Support/BabaCoin/` |
| Windows | `%APPDATA%\BabaCoin\` |

Configuration belongs in `babacoin.conf`. Cookie authentication is available
for local RPC; never publish RPC credentials, seeds or private keys. Keep RPC
bound to loopback and do not expose it through the public P2P firewall rule.
Examples and the distinction between `-datadir`, `-conf` and `-uiconfigdir` are in
[the settings guide](doc/v2-migration.md).

The verified Linux download helper is optional and **fresh-install only**:

```sh
# Review the tagged source and select the release explicitly.
BBC_VERSION=v2.0.0-rc.1 bash contrib/install.sh
```

It verifies the exact distribution/architecture package and SHA256, rejects
unsafe archives and writes a new user-owned installation directory. It does not
start a node, create a wallet, modify configuration, require sudo or alter a
firewall, service or OS security setting. Existing data/installations are refused.
Legacy automatic seed/OS-upgrade scripts now exit safely; see
[operator helpers](contrib/README.md). Never pipe an unreviewed installer into a shell.

## Validation and release integrity

The release gate builds the daemon, CLI, transaction utility and Qt GUI. It runs
343 core test cases, six Qt groups, transaction-utility and RPCAuth tests,
secp256k1 and UniValue suites, source-integrity checks, archive-safety tests and
fresh-wallet smoke tests. Platform logs record their exact source commit and
package versions. A failed target prevents release assembly.

The audit also exercised encrypted/unencrypted V1 test-wallet round trips,
backup restoration, message signatures, localhost-only P2P and bounded historical
block/UTXO comparisons. These are **bounded tests**, not complete chain validation
or a certification of every real wallet. Existing wallet files were not used as
build fixtures. See [validation notes](doc/wallet-desktop-validation.md).

Release files include `SHA256SUMS`, per-archive hashes and `RELEASE-MANIFEST.json`.
The assembler checks architecture, source revision and the complete expected
package set. Mac GUI applications are ad hoc signed and are not Apple notarized;
no Microsoft publisher certificate is claimed. Mac CLI tools retain documented
Homebrew dependencies. Linux packages record their runtime libraries and build
packages. Dependencies retain their [own licenses](doc/release/dependency-notices.md).

## Mission and development goals

Our goal is a capable, understandable, non-custodial BBC wallet that protects
users through explicit controls, transparent releases and compatibility testing.
We prioritize reliable recovery and clear network state over cosmetic claims or
promised financial returns.

1. **V2 foundation:** accessible day/night UI, safer persistence and backups,
   reproducible validation paths, platform-specific packages and English docs.
2. **Release confidence:** independent review, signed/notarized distribution,
   broader migration fixtures, physical Raspberry Pi testing and longer history
   replays. Signing requires the project's actual publisher credentials.
3. **Wallet capabilities:** improve hardware-wallet integration, offline signing,
   accessibility and transaction workflows after design and security review.
4. **Wider clients:** investigate mobile and additional OS targets with explicit
   threat models and real runtime tests. They are not advertised as delivered.
5. **Network evolution:** publish proposals, compatibility analysis, activation
   plans and operator guidance before changing consensus or governance rules.

Features become supported when implementation and tests exist. No investment
returns, exchange listings, mobile availability or network-wide migration are
promised by this roadmap.

## Build, contribute and review

- [Ubuntu build guide](doc/ubuntu-compatibility.md)
- [Desktop validation](doc/wallet-desktop-validation.md)
- [V2 release notes](doc/release/v2.0.0-rc.1.md)
- [Contribution guide](CONTRIBUTING.md)

Use disposable build environments and `make check`. Do not disable tests to
obtain release artifacts or run source-build experiments on a production node.
The protected `main` branch requires an independent approving review. That
protection is retained; passing CI does not replace human review.

Babacoin Core is distributed under the [MIT license](COPYING). Project resources:
[website](https://babacoin.network), [explorer](https://explorer.babacoin.network),
[repository](https://github.com/babacoinbbc/babacoin).
