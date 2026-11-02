# Desktop data-directory recovery validation

Base: main `d97e06b270fa80786e4bc0a19fb41f950a7669aa` (the same source tree as
the original v2.0.1 release).

An unavailable explicit `-datadir` previously skipped the directory chooser and
failed the subsequent startup directory check. The new Qt regression failed
against the original `intro.cpp`, then passed with the recovery implementation.

Validation on native Windows with Qt 5.15.19:

- Both the desktop executable and Qt test executable compiled successfully.
- All six Qt test groups passed: 34 cases including fixture setup/cleanup,
  zero failures and zero skipped cases.
- The new regression exercises unavailable shortcut paths, cancellation,
  rejection of empty folders, selection of an existing wallet, remembered
  relocation, confirmation when the old drive returns, unavailable saved GUI
  settings, reconnect-and-recheck, and the `wallets/wallet.dat` layout.
- A root wallet marker is not sufficient when an existing `wallets/` directory
  changes the backend's wallet base. Unrelated files there are rejected.
- Tests use a temporary directory and synthetic wallet markers; no wallet is
  opened by the backend and no actual user data is moved or modified.
- The network compatibility guard passed for all 284 protected source files.

This is a local functional regression result, not validation of a new installer
or malware-classification clearance. Cached build version metadata is not a
release identity; new release downloads require the separate CI packaging and
release checks. The original v2.0.1 downloads do not contain this fix.
