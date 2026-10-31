# Babacoin BBC — mandatory V2 network upgrade

**Coordination notice:** distribute with the verified release link and checksums.
Draft artifacts and a passing build alone do not mean network adoption is complete.

## Action required from all validating services

Exchanges, mining pools, miners running their own node, explorers, seed nodes,
Smartnode servers and desktop full-node wallet users must upgrade to the
designated V2 release promptly when it becomes available. This requirement is
not limited to Smartnode operators.

**Do not wait until block 1,250,000 to upgrade.** V2 permits 2,000,000 BBC
Smartnode registrations immediately, without an opening block. V1 does not
recognize that collateral denomination and may reject the first block containing
such a registration, even before block 1,250,000. There is no guaranteed grace
period for unmodified V1 validators. A matching genesis or successful peer
connection does not establish agreement on block validity.

The release does not remotely install itself, update a pool or exchange,
submit registrations or move users' coins. Operators must complete their own
upgrade, synchronization and service checks.

## Collateral and reward eligibility

| Collateral output | Registration in V2 | Reward eligibility |
|---|---|---|
| Exactly 1,800,000 BBC | Existing rules apply before expiry | Eligible through block 1,249,999; ineligible from block 1,250,000 |
| Exactly 2,000,000 BBC | Available immediately; no opening-height restriction | Eligible before and after block 1,250,000, subject to normal registration, validity and payment-selection rules |

Eligibility does not promise a payment in every block or a particular return.
Normal confirmations, service status, PoSe, network state and payment selection
still apply. No extra reward percentage is granted for registering early.

At the expiry boundary, candidate-block payment selection excludes the old tier
starting with block 1,250,000, even while the active tip is 1,249,999. Previously
recorded historical denominations remain recognized for chain replay. Losing
reward eligibility does not automatically spend, confiscate or convert collateral.

A collateral is **one exact-value transaction output**. Sending another 200,000
BBC to an address holding a 1,800,000 BBC output does not turn it into a
2,000,000 BBC output. The owner must deliberately prepare an exact 2,000,000 BBC
output and register it, with separate transaction fees. The V2 wizard offers
both currently payable tiers before expiry and never converts an existing
registration automatically. Do not send collateral to a third party claiming
to perform a compulsory migration.

## Operator checklist

1. Identify the designated release and match the archive against its checksums
   and source manifest. Retain the working installation and private recovery
   backups. Follow the [migration guide](../v2-migration.md).
2. Stop the previous process cleanly. Never open one data directory with two
   wallet/node processes. Install into a separate application directory.
3. Start the upgraded node, finish synchronization and compare recent block
   hashes with independently operated upgraded nodes. A version string or a
   TCP connection is not sufficient.
4. Exchanges: verify deposit crediting, withdrawals and hot/cold-wallet recovery
   procedures under your maintenance policy. Pools: update the block-template
   backend, verify templates and submitted blocks, and monitor rejection logs.
5. Smartnode operators: update both the server daemon and any local registration
   wallet; verify the selected collateral, owner and operator identities. Keep
   the operator secret on the server. Check the registration and service state
   after confirmation; retain a recovery backup before and after setup.
6. Monitor chain height, best-block agreement and rejection/synchronization
   warnings during the transition. Do not resolve a chain disagreement by
   disabling validation, forcing invalid blocks or automatically downgrading.

Block 1,250,000 is a height, not a fixed calendar date. Network readiness must be
checked before distribution; do not substitute an estimated date for the height.

## Other rules already present in the audited main branch

The founder destination schedule changes at height 1,250,000. The spork-key
rotation takes effect on software upgrade. These are existing main-branch
differences from V1, not new promises of historical-version compatibility.
They must be included in operator review; no spork is broadcast by publishing
this release. PoW, the mainnet genesis, address formats and ports are retained.

Historical founder payments retain the published V1 destination
`BLbWwABzjJGsSeibe6oJvTK5aEq8Sx7rU9`; the existing future destination is
`B6uz2zqVRvL6RWw9w1vYmahiuQPRm9G8gT` from block 1,250,000. An earlier
candidate's incorrect historical address was caught by an existing-data trial
and is covered by public historical-coinbase regressions. Never approve a
database rebuild merely to work around that incompatible candidate.

Official source and releases: <https://github.com/babacoinbbc/babacoin>.
Never disclose a wallet seed, private key, RPC password or spork private key in
a support ticket or upgrade announcement.
