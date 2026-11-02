# Windows and macOS wallet responsiveness

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

## Small screens and large fonts

The Windows/macOS shell uses scrollable navigation so bottom actions remain
reachable on short displays. Its compact mode reduces branding and header
spacing while retaining labeled navigation and network information. Dashboard
cards stack when the content area is narrow. Hidden tabs no longer impose their
minimum width on the current page; other forms can scroll when necessary.
These layout changes are enabled for Windows and macOS. Linux/ARM daemon
packaging and network rules are outside this UI change.

The Smartnode wizard keeps its headings and descriptions inside themed,
scrollable pages. The native white ModernStyle header is not used by these
desktop builds. Controls remain accessible on shorter windows; long collateral
identifiers do not enlarge the dialog. A disabled Next button still means that
the required confirmed, eligible collateral or synchronization is unavailable.
Registration signing, fee checks, explicit broadcast consent and collateral
rules remain in effect.

The About dialog links to the official
[Babacoin repository](https://github.com/babacoinbbc/babacoin), shows repeated
inherited attribution lines once, and permits scrolling/resizing. The source
link is corrected in the shared license information too.

## Qt version

The current Windows build uses open-source Qt 5.15.19, listed in the official
[Qt 5.15 archive](https://download.qt.io/archive/qt/5.15/). Qt 6.12.0 is available
in the [Qt 6.12 releases](https://download.qt.io/official_releases/qt/6.12/).
Open-source Qt is available under its applicable LGPL/GPL licenses. This UI
repair retains Qt 5 compatibility; it does not claim to complete a Qt 6 port.
Moving to Qt 6 requires a separate port of removed APIs and build dependencies,
followed by platform and upgrade validation.
