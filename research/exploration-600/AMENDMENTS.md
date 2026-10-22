# Pre-held-out clarifications

A1, 2026-10-22, after development GREEN and before any held-out execution:
A trial's checkpoint charge includes recording its terminal budget decision. The
policy marks exhaustion in that same paid transition if the next tranche cannot
fit. For a budget too small for one trial, initialization records the exhausted
state. There is no uncharged extra terminal append. The original16-record storage
ceiling remains conservative. This clarifies full charging, not a policy change.

A2, same stage: the host driver writes real Cortex records for every campaign arm,
replays each completed history, records actual journal bytes and total host elapsed
and CPU seconds separately. Pure work-unit policy results remain SYNTHETIC. Native
CPU execution correctness is host qualification, not hardware timing qualification.

A3, before held-out: development review found that writing only the result leaves
an interruption window where spent work could disappear. Use two existing Cortex
candidate records per attempt: a prepaid reservation before evaluating, then the
result. The same synthetic checkpoint2 charge covers both appends and one recovery
lookup/recheck. A restart with a pending reservation abandons that paid attempt;
its search/check/storage/checkpoint cost remains spent, interrupted_count grows,
and a retry must pay anew. Completed-boundary restarts still reproduce exactly.
Attempt limit14 includes interrupted attempts. Conservative journal ceiling becomes
29 records =24392 bytes including header; payload size unchanged. Record count and
bytes count all reservations, results and abandoned-reservation records. No new
journal format, authority or effect. This correction applies before held-out and
has no measured-calibration implication. Add a deterministic process-interruption
hook after reservation and after evaluation to test this exact window.

A4, before held-out: bind OSC source, emitted native code and host executable into
one artifact digest. The executable includes the linked compiler/Cortex source;
a changed adapter/build cannot resume an old campaign. Validate record length and
all identity header fields before reading context words. Candidate frontier entries
are unverified proposals; the correctness floor applies to accepted best/expanded
states. An invalid proposal may enter the checking queue but never pass its gate.
The known Cortex whole-record rollback limitation is unchanged: deliberate rollback
of an entire valid journal prefix requires an existing J-Space/external anchor to
detect. This isolated no-promotion experiment claims crash recovery with synchronous
append, not protection against coordinated rollback. No new anchor/store is added.
