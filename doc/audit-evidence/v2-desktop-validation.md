# V2 desktop validation

Local validation on Windows 11 (x64, Qt 5.15.19) covers:

- All 345 core test cases and all six Qt test groups completed successfully.
- Turkish and US English amount grouping, signs, fractional precision, and
  unchanged canonical parsing/URI/export amount formatting.
- Asynchronous transaction-history preparation, event-loop heartbeat, queued
  notifications without duplicate rows, deferred export, and cancellation on
  model destruction. A complete snapshot is published once so Windows
  accessibility does not repeatedly scan a growing table.
- Splash artwork at 125% and 150% process scaling, including long development
  version text; no logo/title overlap in the observed Windows configuration.
- A backed-up existing-wallet copy with a large history, readable transaction
  date/amount columns, navigation, and day/night display. At an identical chain
  tip with the same mempool, V1 and V2 wallet balances, transaction count,
  wallet version and HD identity match. This is one private migration trial,
  not certification of every historical wallet.

The local executable is a development build. CI release packages have their own
source hashes, manifests and completion gates and must be checked separately.
Private wallets, keys, account records, screenshots and logs are not published.
Full-node verification and required rescans remain enabled; these GUI changes
are not a promise of instant blockchain loading or synchronization.
