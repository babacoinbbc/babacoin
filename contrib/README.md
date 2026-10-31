# Babacoin operator helpers

V2 separates package installation, wallet migration, node configuration and
network administration. No helper is authorized to combine those operations
silently.

## Verified downloads for fresh Linux installations

Review the tagged source first. Then run as a regular user:

```sh
BBC_VERSION=v2.0.0-rc.1 bash contrib/install.sh
```

The helper requires an explicit published tag, selects the exact Ubuntu or
Debian version and native architecture, verifies SHA256, rejects unsafe archive
members and installs into a new user-owned directory. `BBC_INSTALL_DIR` may
select a different **new absolute directory**. It does not start applications,
create wallets, change configuration, require sudo, install dependencies,
open firewall ports or enable a service. Existing nodes/data are refused.
Read [migration and settings](../doc/v2-migration.md) before production use.

Ubuntu packages are never substituted for Debian packages. Raspberry Pi OS
64-bit follows its Debian base, but physical device testing remains separate.
Inspect the packaged runtime-library and build-package lists on your exact OS.

## Existing installations

Use the running wallet's `backupwallet` operation or copy a cleanly stopped
wallet privately. A failed RPC backup does not fall back to copying live data.
`prepare-wallet.sh backup` implements this fail-closed behavior. Do not run a
new binary against your only wallet copy as a compatibility experiment.

Legacy Oracle/seed setup and automatic OS-upgrade scripts now exit without
changes. Their previous binary selection, forced package-manager interruption
and security-update modifications are not part of the V2 installation path.
Systemd and public P2P configuration are explicit operator decisions; RPC must
remain private. No private-key export is part of V2 setup.

## Offline GUI preview

Release packages contain `TEST-WALLET.cmd` or `TEST-WALLET.sh`. Every run creates
new regtest data and GUI settings with external P2P disabled. Never put a real
wallet in that test directory.
