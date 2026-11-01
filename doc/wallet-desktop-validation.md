# Desktop wallet validation

The wallet has a vertical navigation panel, a balance summary, the existing
balance details and recent transactions, and persistent day/night themes.
The appearance button and Ctrl+Shift+L change the theme. The existing font
family and size settings still apply. The dashboard scrolls when its contents
cannot fit on a small screen.

The activity bars represent only the bounded recent list, with credits above
and debits below the baseline. They do not represent prices, a complete
transaction history, staking yield or a return forecast. Exact amounts remain
in the transaction list. The balance summary uses the existing total field;
watch-only and pending balances remain separate. Network labels identify the
selected network and do not claim synchronization or connectivity.

Startup paints feedback before application font setup and no longer enumerates
all installed font families for diagnostic logging. Font setup duration is
logged. Block verification, wallet loading, rescans and synchronization are
unchanged. A valid pixel-sized native font is converted before the point-size
cache is populated; a Qt regression test covers the previously observed abort.
These changes do not establish the cause of slow startup with a user's data.
The published v1 version-dialog measurement and fresh-wallet RPC readiness
are different measurements and must not be presented as a speed comparison.

For a disposable GUI test, create separate existing absolute directories for
data and GUI settings, and an empty configuration file in the test data folder:

```text
babacoin-qt -regtest=1 -datadir=<test-data> -conf=<empty-test-conf> -uiconfigdir=<test-gui-settings> -choosedatadir=0 -connect=0 -listen=0 -dnsseed=0 -discover=0 -upnp=0 -listenonion=0 -maxconnections=0
```

Do not reuse production directories. `-uiconfigdir` isolates GUI preferences,
not wallet data or network selection. A normal launch retains existing GUI
settings. No automatic wallet migration, key export or production data copy
is part of the visual test. Regtest test coins have no mainnet value.

Windows uses the current UCRT64 dependency packages with BDB 4.8.30, explicit
native pkg-config dependency information and recursive DLL packaging. The
legacy all-static application flag is disabled for these dynamic Qt packages.
The BLS link adds the external Sodium dependency omitted by the upstream
Windows combined archive; production cryptographic code is unchanged.

macOS builds run natively on Intel and Apple Silicon. The GUI is bundled with
macdeployqt and ad hoc signed. It is not Apple notarized. The CLI binaries
retain the Homebrew dependencies recorded in the artifact. Native Windows
ARM64, 32-bit Windows, old macOS versions and physical Raspberry Pi hardware
are not validated by this matrix.

The scheduler ordering test records all 200 callback ordering observations and
checks them on the main test thread after joining workers. Calling Boost.Test
assertions concurrently is unsupported; an ARM64 Ubuntu 26 run exposed an
intermittent abort in the old test. Production scheduler code is unchanged.
See the [Boost.Test FAQ](https://live.boost.org/doc/libs/master/libs/test/doc/html/boost_test/section_faq.html).

Test outcomes apply to their recorded source commit and platform. The draft
PR and workflow artifacts are review/test deliverables, not a production
release or a guarantee that arbitrary historical consensus versions agree.
