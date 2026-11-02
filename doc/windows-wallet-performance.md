# Windows wallet responsiveness

The node's blockchain synchronization and the wallet's transaction scan are
separate operations. Replacing `wallet.dat` can require a scan of blocks already
stored locally, even when the node was fully synchronized. The scan finds
transactions belonging to the replacement wallet's keys and starts from that
wallet's saved checkpoint when possible. It is not necessarily a new network
download. A completed scan saves its checkpoint; interrupted scans must not
silently skip unprocessed transactions on the next launch.

Use **Copy diagnostics** during startup to record the version, network, phase
and elapsed time. During a wallet rescan, the checked-block count and current
height show whether the scan is advancing. An early loading phase without a
percentage is not by itself evidence of a hang.

## Periodic balance calculations

Nonblocking lock acquisition alone does not make a large wallet's balance
calculation inexpensive. The Qt wallet model now schedules periodic balance
calculations on its own worker thread and applies completed snapshots on the
GUI thread. At most one request is outstanding per wallet. Notifications and
synchronous send-balance updates invalidate older results, which are discarded
and retried. An unavailable validation/wallet lock leaves the last complete
snapshot visible until a retry succeeds. Wallet unload joins the worker before
destroying its wallet interface.

Hidden CoinJoin progress statistics are skipped when CoinJoin is disabled.
This changes periodic presentation work, not balance rules, transaction
validation, key storage, network identity or consensus.

These changes address identified sources of GUI stalls, not every possible
cause of slow operation. Disk latency, antivirus activity, a large transaction
history, enabled mixing statistics and explicit wallet operations can still
require investigation. Do not delete wallet or blockchain files to diagnose a
slow window. Back up a wallet before replacing it, and replace wallet files only
after a normal, completed application shutdown.
