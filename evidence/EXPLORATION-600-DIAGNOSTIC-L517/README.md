# L517 diagnostic correction evidence

This packet records the bounded correction to L512 finding E1 on the exact
L492 source head `4960b1a07a5713c2eed9df53ad6c90103d1c4ea2`. It is host-only
diagnostic evidence. The sealed scientific verdict remains **FAIL**; hardware
and model gates remain **NOT_RUN**, and energy remains **UNMEASURED**.

## Finding and RED/GREEN

The frozen L492 v1 rows have SHA256
`eae930c030069e3f35ee755a7132ffb843522b5f588d00748b31e1b67b2846a1` before
and after this work. A failing assertion on the sealed H32 B-arm rows found 10
budget-exhausted cases with a positive value in the old `false_pruned` column;
the raw RED output and exit 1 are in `red.log` and `red.log.exit`. This is 10
of the 20 budget-exhausted B cases at H32, represented by 20 B cases total.

Version 2 calls the value `unexplored_better` and records `stop_reason`.
Re-rendering the same rows gives 20 B cases with `budget_exhausted`, of which
10 retain a beneficial unexplored node. The focused GREEN check has exit 0.
The independent 300-row accounting reports zero failures. Its summary matches
the original arithmetic file exactly apart from the corrected metric label
(`arithmetic-compatibility.diff`, exit 0). C-positive H32 remains 20/25;
C total net remains 38,510; the predeclared scientific verdict remains FAIL.

The derived table `rows-v2.csv` is generated from the immutable frozen rows by
`tests/research/ex600/derive_diagnostics.awk`; it does not rerun the search.
The converter refuses an unexpected input header (negative control exit 2).
The exact schema and limitation are documented in
`research/exploration-600/DIAGNOSTIC-SCHEMA.md`.

## Host checks

Pinned host: `spark-b87b`. The exact source inventory and binary digest are in
`host-checks/sources.sha256` and `host-checks/binary.sha256`. The fresh run
metadata binds source head, timestamp and command in `host-checks/run.json`.
Build and both executions used the existing `quiet-build` path through
`research/exploration-600/run.sh`, with its 60-second bound.

Command:

```sh
EX600_OUT=/home/drakestapleton/workspace/targets/L517-exploration \
  bash research/exploration-600/run.sh checks \
  /home/drakestapleton/workspace/targets/L517-exploration/packet-final
```

Build exit 0; original suite **2,337 checks, 0 failures**, exit 0; focused
diagnostics **40 checks, 0 failures**, exit 0. All six existing behavioral
mutants were caught (`greedy`, `invalid`, `free-failure`, `budget`, `seed`,
`context`); each mutant execution exited 1 on a failed assertion, rather than
failing to build. The run's PASS is host qualification only.

The focused diagnostic covers a budget stop with beneficial frontier work, a
no-beneficial-state control, and separately observable capacity abandonment.
No explicit per-node policy-prune reason is stored in the source state, so a
true policy-prune total remains unavailable and is not inferred. Original
campaign files and their manifest were not edited; no held-out campaign was
rerun.
