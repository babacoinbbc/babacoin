# V2 migration, configuration and recovery

## Release status and network review

V2.0.0-rc.1 is a release candidate. It retains the audited main-branch network
identity and schedules, including changes that already existed relative to V1.
It does not trigger a remote installation or move coins. This is a mandatory
network upgrade for V1 validators: 2,000,000 BBC registrations are allowed
immediately, without an opening-height restriction, and V1 can reject the first
block containing one. Do not wait until block 1,250,000 to upgrade. See the
[operator announcement](release/mandatory-network-upgrade.md).
Check operator/miner adoption and the current chain/spork state before a
production migration. Arbitrary historical-version agreement is not guaranteed.

Both 1,800,000 and 2,000,000 BBC collateral can qualify before height 1,250,000.
The old tier loses reward eligibility starting with that block; the final tier
has no opening block and can be registered in advance. Payment selection uses
the candidate block height at expiry, preventing a one-block extension of the
old tier. Historical registration amounts remain recognized for replay.
The existing founder destination schedule also changes there;
actual enforcement must be read with the founder-validation code. The spork key
rotation is effective on upgrade. Quorum/ChainLocks availability depends on the
network and valid signed sporks, not only on installing this binary. Do not
paste a spork private key, RPC secret, mnemonic or wallet password into GitHub.

A collateral output is an exact-value UTXO. Sending more coins to its address
cannot enlarge that output. No helper automatically spends or re-registers a
smartnode collateral. Plan such actions separately with verified ownership and
operator settings; do not treat a software update as a coin-transfer instruction.

## Existing wallet: preserve the recovery path

1. Record the running version, selected network, actual data directory, current
   height and synchronization state. Saved GUI preferences may override defaults.
2. Create a private backup using the running wallet's backup facility / local
   `backupwallet` RPC. If RPC backup fails, stop and investigate; do not copy a
   live wallet as a fallback. A filesystem copy is appropriate only after a
   clean shutdown. Retain the working binary and a recoverable data snapshot.
3. Verify the release checksums and source/platform manifest. Extract into a
   new application directory. Do not overwrite the only working installation.
4. Validate a backup copy in an isolated directory with external P2P disabled
   before considering production use. The included preview launcher creates a
   NEW regtest wallet; it does not validate your real wallet backup.
5. Stop the old application cleanly before any deliberate production start.
   Never let two applications open the same wallet/data directory concurrently.
6. Recheck address ownership, transaction history, pending/immature/watch-only
   amounts, synchronization and smartnode state. Keep RPC private. A successful
   open alone does not prove all balances or network activations agree.

Cross-version tests used newly generated encrypted and unencrypted wallets.
They are not a blanket guarantee for every large, previously damaged or
feature-rich production wallet. No automatic `-upgradewallet`, key export,
reindex, bootstrap import or OS upgrade is part of V2 setup.

## Paths and independent settings

| Setting | Meaning |
|---|---|
| `-datadir=/absolute/path` | Blockchain, wallet and node data; directory must exist |
| `-conf=/absolute/path/babacoin.conf` | Explicit configuration file |
| `-uiconfigdir=/absolute/path` | Separate GUI preferences only; existing absolute directory required |
| `-regtest=1` | Independent development network and test coins |
| `-connect=0 -listen=0 -dnsseed=0 -discover=0 -upnp=0 -listenonion=0 -maxconnections=0` | Offline P2P test settings |

Default data directories: Linux `~/.babacoin/`, Mac
`~/Library/Application Support/BabaCoin/`, Windows `%APPDATA%\BabaCoin\`.
`-uiconfigdir` does not select a wallet or a network. Do not confuse test GUI
preferences with a production data backup.

## Local RPC configuration

For an operator who deliberately needs local RPC, an example `babacoin.conf` is:

```ini
server=1
rpcbind=127.0.0.1
rpcallowip=127.0.0.1
rpcport=17668
```

Local cookie authentication avoids putting a static RPC password in this
example. Do not expose RPC port 17668 to the Internet. Mainnet P2P uses port
6678; P2P and RPC are different services. A GUI preview does not require RPC.
For remote management use a separately reviewed secure transport rather than
opening public RPC. The restored `share/rpcauth/rpcauth.py` helper is available
when an operator explicitly requires hashed RPC credentials.

Appearance preferences are stored locally. Day/Night switching and Ctrl+Shift+L
persist the theme. Existing font family and scaling controls remain available.
The selected-network badge identifies the configured network; it does not
claim that synchronization or quorum protection has completed.

## Slow startup and rescanning

Startup may include database loading, block-index verification, wallet loading,
rescanning and network synchronization. V2 gives earlier visual feedback and
logs font setup timing. It does not skip verification or mark a required rescan
complete simply to show a balance sooner.

Do not leave recovery flags such as `-rescan`, `-zapwallettxes` or
`-salvagewallet` in routine launch scripts without understanding why they are
required. Do not add explicit overrides that suppress a scan required by the
wallet. Preserve the last startup log and investigate the phase that consumes
time; a small fresh-wallet timing is not a benchmark for a large real wallet.

An interrupted, aborted or unreadable-block startup rescan now rejects wallet
initialization before advancing its persisted scan locator. Restart retries
from the previous checkpoint; this prioritizes correct transaction discovery
over showing an incomplete balance. It does not implement per-block checkpoint
acceleration or eliminate the time needed for a required scan.

## Fresh Linux package helper

Run the reviewed tagged `contrib/install.sh` as a normal user with an explicit
`BBC_VERSION`. It refuses existing data, binaries, services and running nodes.
It selects only exact Ubuntu LTS or Debian 12/13 packages and native x86_64 /
aarch64 architecture, verifies the archive and installs into a new user-owned
folder. `BBC_INSTALL_DIR` can select another new absolute destination.

The helper never uses sudo, installs dependencies, changes OS security updates,
interrupts apt/dpkg, changes firewall rules, enables services, starts a daemon or
creates a wallet. Install the matching runtime libraries deliberately using your
OS package manager; package/library lists are included in the release. Legacy
Oracle/seed/OS-upgrade helpers now exit without changes. Review `contrib/README.md`.

## Optional Explorer discovery (desktop wallet)

In **Settings → Options → Network**, enable **Discover additional peers from
Babacoin Explorer**. It is off by default, changes immediately and persists in
the current GUI preference profile. `babacoin-qt -explorerpeers=1` or
`-explorerpeers=0` explicitly overrides the preference; this is a desktop option,
not a new `babacoind` HTTP client.

The fixed HTTPS endpoint is the one used by the Explorer's public network page:
`https://explorer.babacoin.network/ext/getnetworkpeers/internal`. The Explorer
sees the client's public IP. The request contains no wallet data, credentials,
balance or chain height. TLS peer verification stays enabled; redirects and
cookies are not followed/stored. Requests have a 15-second timeout and a 64-KiB
response limit, at startup and at most once per 30 minutes while enabled.

Only routable numeric IPv4/IPv6 candidates on mainnet P2P port 6678 are accepted,
up to 16 per response and one per network group. They enter ordinary address
discovery with one shared source bucket, not the privileged manual `addnode`
list. Existing seeds, configured nodes, bans and full peer/block validation
remain effective. No configuration file is rewritten and no peer is whitelisted.

Fetching pauses on test/regtest, disabled networking, shutdown, configured P2P
proxies, explicit `-connect`, `-onlynet`, `-dnsseed=0` or nonpositive connection
limits. Turning the preference off aborts its request and stops refreshes;
already learned ordinary addresses age normally. Explorer outages or a changed
API do not prevent the wallet opening or normal discovery continuing.

## Smartnodes and the Web Wallet

The [Smartnode wizard guide](smartnode-setup.md) covers existing-collateral
registration, separate fee funds, public BLS keys, review and cancellation.
Its final broadcast requires explicit consent; preparation alone does not submit
a transaction. A remote VPS is installed and administered separately.

The **Web Wallet** tab opens `https://web.babacoin.network/` in the default
browser only when requested. Local and web keys, balances and recovery procedures
are separate; there is no automatic import or wallet/RPC bridge. The mainnet
website launch is disabled in local test-network previews. Embedded browsing is
future work requiring a supported browser stack and review of the site's framing
policy; it is not claimed as included in V2.

## Rollback and future improvements

Keep old binaries and private backups. A rollback must consider wallet feature
versions, database state, chain state and network rules, not only an executable
filename. Never downgrade automatically or test rollback against the only data
copy. Full history replay, broader migration fixtures, physical Pi validation,
publisher signing/notarization and independent review remain explicit release
confidence goals. Production consensus changes require separate proposals and
activation plans.
