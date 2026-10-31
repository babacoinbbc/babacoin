BABACOIN CORE V2 - ISOLATED WALLET PREVIEW

Windows: run TEST-WALLET.cmd.
Linux/macOS: run bash TEST-WALLET.sh from the extracted package.

Each launch creates NEW temporary regtest data and separate GUI preferences.
External P2P connections are disabled. Regtest coins have no mainnet value.
The launcher does not copy, open or migrate an existing wallet.
Use the Day / Night button or Ctrl+Shift+L to switch appearance.

Directly launching babacoin-qt or the Mac application uses normal settings and
may open an existing production wallet. Use the preview launcher for visual QA.
Never copy a real wallet.dat into this disposable test directory.

This release candidate is for validation, not a claim of zero financial risk.
Read the V2 migration guide before considering a production upgrade. Back up
through the running wallet's backup facility, or copy only a cleanly stopped
wallet. Keep backups private. Do not publish seed words, keys or RPC secrets.

Existing mainnet genesis, ports, prefixes and monetary schedules are retained.
The current main branch already differs from V1 in activation and spork rules.
An OS build passing does not prove agreement among every historical version.

Mac applications are ad hoc signed, not Apple notarized. No Microsoft signing
certificate is claimed. Linux CLI/GUI binaries require the recorded runtime
libraries for the matching distribution. Physical Pi hardware validation is
separate from native ARM64 userspace testing.

Source, settings, platform status and release notes:
https://github.com/babacoinbbc/babacoin
