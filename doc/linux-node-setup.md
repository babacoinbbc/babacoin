# Automatic Ubuntu/Debian node setup and updates

This is the **headless mainnet full-node** installer. It installs `babacoind`,
`babacoin-cli` and `babacoin-tx`, not the Qt desktop runtime. It uses the latest
published stable GitHub Release every time it runs; it never installs a draft
or release candidate. A newer executable does not change network rules on
other participants. Read the selected release's migration/network notices.

## One setup command

Download the entry point from the official repository and run it:

```bash
curl -fL --proto '=https' --proto-redir '=https' \
  https://raw.githubusercontent.com/babacoinbbc/babacoin/main/install-node.sh \
  -o install-node.sh && bash install-node.sh
```

Inspect downloaded administration scripts before running them. Setup uses
`sudo` for package installation and process/service management when necessary.
The standalone entry point fetches its two helpers from one immutable source
commit. A Git checkout uses its adjacent helpers instead.

Run the same command for future updates. There is no version number to edit.
Setup requests normal shutdown of the selected running daemon, waits for it
to exit, backs up wallet/configuration files, replaces only its three program
files, and starts the new version with the same data directory, account and
arguments. For an identified systemd service it stops and starts that service;
its unit configuration, user and startup policy are retained. No forced-kill
fallback is issued by setup; existing systemd stop-timeout policy still applies.

## What is detected

- Ubuntu **18.04, 20.04, 22.04, 24.04, 26.04** and Debian **12, 13**.
- x86_64 and aarch64, including corresponding 64-bit Debian-based Pi systems.
- The exact native runtime packages, including Boost, OpenSSL, MiniUPnP and
  libevent/pthreads ABI versions. Other distributions/versions and 32-bit
  systems stop with an explanation; foreign ABIs are never substituted.
- The actual running daemon executable and held data-directory lock, rather
  than whichever older binary happens to appear first in `PATH`.

Ubuntu 24.04 needs the Ubuntu 24.04 archive and Boost 1.83, not an Ubuntu 22.04
or Debian 12 archive requiring Boost 1.74. Package checksums, source identity,
ELF architecture and executable hashes are verified before an old node stops.
GitHub HTTPS metadata/checksums provide integrity under that account's trust;
this is not an independent code-signing or malware-classification guarantee.

## Locations and simple launchers

With no existing running daemon, defaults are:

```text
Programs/tools: ~/.local/opt/babacoin/
Node data:      ~/.babacoin/
```

The account is the existing daemon owner, or the invoking sudo user for a new
installation. Direct root invocation uses root's account. Running processes
are restarted under their original account. Keep custom service account homes
and program/data folders separate; use `--install-root` when needed.

Setup prints absolute paths to these generated files:

```bash
bash ~/.local/opt/babacoin/start-babacoin.sh
bash ~/.local/opt/babacoin/babacoin-cli.sh getnetworkinfo
bash ~/.local/opt/babacoin/babacoin-cli.sh getblockchaininfo
bash ~/.local/opt/babacoin/stop-babacoin.sh
```

Backups and `INSTALLATION.json` are kept under the printed program/tools root.
An already installed but stopped node managed by this tool retains its recorded
data location. For an unmanaged stopped installation, explicitly select its
data path; inactive arbitrary third-party services are not automatically adopted.
No existing wallet, configuration, chain data, firewall or service unit is deleted
or rewritten. Existing executable files are backed up before replacement;
replacement failures restore the old programs and attempt normal restart.

## Bootstrap

Default `--bootstrap auto` downloads the verified full bootstrap only when
neither `blocks/` nor `chainstate/` exists and no automatic bootstrap input or
`.old` file is already present. It verifies both archive and raw payload hashes
and mainnet identity, then publishes a new `bootstrap.dat` without overwriting
an existing file. No wallet/configuration/chainstate comes from the ZIP.

The node still validates every imported block normally and synchronizes newer
blocks from peers. Bootstrap saves network downloading, **not validation work**;
it does not guarantee a short initial sync. Never cycle through old binaries or
delete wallets/chain data just because validation takes time.

```bash
bash install-node.sh --bootstrap never
bash install-node.sh --bootstrap always --data-dir /srv/babacoin-data
bash install-node.sh --data-dir /srv/babacoin-data --install-root /opt/bbc-node
bash install-node.sh --no-start --bootstrap never
bash install-node.sh --plan
```

`always` permits full-file import alongside an existing chain but still never
overwrites an existing bootstrap input. Multiple running daemons require an
explicit matching `--data-dir`. Non-mainnet nodes, included configuration files,
custom external wallet/block paths and ambiguous process ownership require a
manual reviewed upgrade. A locked Qt wallet is not terminated by this tool.

For automation, run this command when **you** choose to update. No periodic
background updater or new boot service is silently registered. Newly installed
nodes start in the background unless `--no-start` is selected; existing systemd
services retain their existing boot behavior. Check `debug.log` and the CLI
after startup; a started process is not proof that synchronization completed.
