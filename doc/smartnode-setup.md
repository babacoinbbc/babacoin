# Smartnode setup wizard

Before mainnet block 1,250,000, the collateral amount selector offers both
1,800,000 and 2,000,000 BBC. The latter can be funded and registered early, with
no opening-height restriction. Only 2,000,000 BBC remains reward-eligible from
block 1,250,000. The wizard checks the selected amount against both the current
and next block, so it will not prepare a registration that is already expiring.
No existing collateral or registration is converted automatically.

Early 2,000,000 BBC registration requires network-wide validating-node adoption:
V1 can reject the first block containing such a registration. Read the
[mandatory upgrade notice](release/mandatory-network-upgrade.md); do not assume
old wallets remain compatible until the later reward cutoff.

Open **Tools → Smartnode setup wizard**, or the setup button on the Smartnodes
page. This is a self-operated registration flow with **0% operator reward**.
Existing-node updates, external signers, delegated operators and automatic VPS
administration remain separate workflows.

## Before starting

Use a synchronized local wallet with a verified private backup. The wizard reads
the current network's collateral schedule and confirmation requirement. On the
audited main source, collateral is 1,800,000 BBC through height 1,249,999 and
2,000,000 BBC from 1,250,000. It pauses at a changing next-block boundary rather
than silently preparing the old amount. This does not introduce a new activation.

The current registration protocol requires an IPv4 service address. Mainnet uses
P2P port 6678. A running VPS with matching Babacoin software, the correct network,
reliable storage and an appropriately configured P2P firewall is required.
The wizard does not install an OS, use SSH, open a firewall or prove server uptime.

## Guided registration

1. **Collateral:** choose an owned, unspent, sufficiently confirmed P2PK/P2PKH
   output of the exact current amount. Already registered collateral is excluded.
   If needed, choose **Prepare a collateral transfer in Send**. This creates a
   new receiving address and opens a separate normal Send dialog; review its
   recipients, inputs and fee before sending, then wait for confirmations and
   reopen/refresh the wizard. It does not automatically spend funds.
2. **Public identities:** enter the server's IPv4/P2P endpoint, a valid public BLS
   key, owner/voting address, separate payout address and a fee source controlled
   by this wallet. The wizard can create owner and payout addresses locally;
   update the wallet backup after generating new keys. Owner/voting must be
   separate from collateral and payout. The simple wizard requires a local
   spendable wallet; it does not import watch-only or hardware-wallet secrets.
3. **Server key:** on the server, run `babacoin-cli bls generate`, preserve its
   secret privately there and paste only the public result into the wizard.
   The optional public configuration template contains placeholders, no private
   key or RPC password. Add the matching secret locally on the server and keep
   RPC on loopback. Main/test/regtest exports retain the selected network;
   named devnet templates require manual network parameters. No operator private
   key is requested by the desktop wizard.
4. **Review:** the wallet prepares and signs locally without inserting or relaying
   the transaction. The wizard checks the exact funding inputs and outputs,
   references collateral without spending it and shows the exact registration
   fee. Every change output must return to the chosen fee source. The existing
   node's dry-run path checks policy and ProReg rules; final submission still
   performs full script and consensus validation.
5. **Submit:** check the review consent and confirm the fee/broadcast question.
   The exact reviewed signed transaction is submitted once. Wait for confirmation
   and check the Smartnodes list and server status. A local submission is not a
   guarantee of mining, network-wide acceptance, payment eligibility or rewards.

Keep fee funds separate from collateral. During preparation, all known collateral
denominations are temporarily protected from fee selection. The funding guard
also rejects collateral inputs, unexpected change destinations, duplicate inputs,
overspending, negative outputs and fees above the wallet limit.

## Cancel, retry and recovery

Before a submission attempt, cancelling restores only locks newly added by the
wizard; pre-existing locks remain untouched. After submission or an ambiguous
submission error, the selected collateral stays locked. Check the transaction
status before retrying. Keep collateral protected while a registration is pending;
normal wallet coin locks are in-memory state, not a new wallet database format.
Do not assume that restarting before confirmation preserves a new temporary lock.

The wizard never replaces an existing registration automatically, converts old
collateral or tops up an existing UTXO. A software update cannot enlarge a
collateral output by sending more coins to its address.

The older `protx quick_setup` RPC remains an advanced automatic-broadcast interface,
not the wizard backend. Its credentials now use the core cryptographic RNG; its
new recovery file is checked and committed before broadcast, with no overwriting
of an existing filename. Use a private writable working directory: the file
contains an operator secret and inherits the host's file access policy. Retain
the reported configuration path if registration fails or its status is unclear.
