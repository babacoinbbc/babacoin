# Synchronization responsiveness and import observer review

## Reproduced GUI wait

The pre-fix client model queried the header tip from the GUI thread when its
cache was empty. That core query waits for `cs_main`. A regression test holds
the validation lock in a separate thread, with a five-second escape deadline,
while attaching the overview/console and requesting status updates. The old
model reaches the deadline; the corrected model services timer heartbeats and
the overlay's Hide action before the lock is released.

The client model now publishes coherent chain snapshots from a worker that
uses `TRY_LOCK`. GUI readers keep the last complete snapshot. Mempool polling
and InstantSend database scans also run on that worker, with initial-sync and
frequency limits. Destruction joins the worker before node databases shut down.
Balance rendering uses the first successful cached snapshot rather than a
blocking startup query. Normal transaction creation checks remain unchanged.

Further review identified the same chain-lock wait inside bounded transaction
history batches: a CPU time budget does not prevent a single blocking query.
Initial snapshots, later history batches, live update replay and passive status
painting now use nonblocking chain-lock attempts before wallet queries. Busy
work is retried; live notifications are coalesced without dropping the latest
update. Cached records remain available for painting. A second regression holds
the chain lock before the first history batch runs and checks GUI heartbeats,
deferred history completion and successful replay after the lock is released.
Another check defers a live display update and confirms it is applied after
release. These changes affect display scheduling, not wallet records or spending.

The Smartnode table's owner-only filter also queried collateral through the
chain lock from its GUI timer. It now takes one nonblocking read-only collateral
snapshot, releases the chain lock before building table rows, and retains the
refresh request if the snapshot was busy. The same held-lock fixture exercises
the owner-only filter and verifies that its deferred refresh completes. Optional
CoinJoin progress statistics likewise keep their last display while the chain
lock is busy. Mixing, registration and transaction submission rules are unchanged.
Filtered-out Smartnode rows free their temporary items before returning.

## Observer-only changes to protected validation files

`src/validation.h` adds an optional read-only progress callback to
`LoadExternalBlockFile`. `src/validation.cpp` records the deserialized input
offset and the existing loaded-block count, reports at a bounded monotonic-clock
interval outside the validation lock, and reports once at the end. A standard
observer exception disables reporting; thread interruptions still propagate.

Review of the complete diff shows no changes to block decoding, accepted file
ordering, block hashes, `AcceptBlock`, `ActivateBestChain`, script or PoW checks,
return conditions, chain selection, collateral, rewards or wallet serialization.
Callers that do not request progress retain the empty observer default.
`init.cpp` connects this observer for bootstrap and explicit load files and
announces the subsequent activation phase. Existing import/rename ordering is
preserved, including the fact that a `.old` name is not a validation receipt.

Only these two newly changed protected files receive explicit manifest
exceptions; their previous normalized hashes are retained as `original_sha256`.
The integrity guard continues to check every other protected entry unchanged.
This source tripwire is not a proof of consensus equivalence or a security audit.

## Progress semantics and tests

- Header synchronization uses a labelled time-based estimate.
- Block verification cannot reach completion while known headers remain ahead
  of the validated tip, even if an old transaction-throughput estimate says 1.
- File reading uses processed bytes divided by the actual input file size.
  Unknown sizes remain indeterminate; validation is a separate phase.
- A retained, sequenced import snapshot supports late GUI attachment and rejects
  delayed events older than the latest snapshot.
- Qt tests cover lock contention, Hide/heartbeat response, stale and non-finite
  estimates, file-read versus header updates, late attachment and event ordering.

The fixture cleanup fix preserves the Berkeley DB duplicate-handle assertion.
Windows test assertions write to stderr so unattended failures do not leave a
desktop dialog. The production application's assertion behavior is unchanged.
