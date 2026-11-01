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
