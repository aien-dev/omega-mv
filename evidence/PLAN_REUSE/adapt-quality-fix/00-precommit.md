# PRECOMMIT: E03 payback rerun against the L168 adaptation fix (written before any payback run)

- date: 2026-10-20. ledger: L168. fix head: 4060eca5 (runtime change 9562508d).
- harness: tests/runtime/rx_plan_payback.c and the Makefile target from research/e03-payback-246 @66e092ac,
  COPIED (git archive of 66e092ac into ~/workspace/targets/omega-rxplan-adapt/payback-{before,after}), not cherry-picked,
  and not committed to this branch (it belongs to the research branch's own PR).
  - before: the archive as it is (runtime identical to base a8ffd2b9: `git diff --stat a8ffd2b9 66e092ac -- src/` is empty).
  - after: the same archive with this branch's src/runtime/rx_plan_arrange.{c,h} and payback-harness-L168.patch (the
    harness's inline repair loop replaced by: search first, then pla_adapt with that search plan). rx_plan.{c,h} identical.
- runs: `rx_plan_payback_test dev` and `rx_plan_payback_test heldout`, adaptation ON (PAYBACK_NO_ADAPT unset), mutant unset,
  PAYBACK_OUT set to a per-run folder. One run per mode per arm (before, after). Seeds and streams as frozen in
  research/e03-payback-246 evidence/PLAN_REUSE/e03-payback/00-frozen-protocol.md (unchanged).
- what counts: F2 floor rows (plan length arm A vs arm B per task) and the merge / swapped-reverse plan lengths. Correctness only:
  no timing claim; load and MemAvailable recorded at start and end; no quiet lock.
- predictions:
  - before: F2 FAIL rows > 0 on the mixed streams (campaign: S2, S3, S4), merge repaired to 12 where search finds 9.
  - after: 0 F2 FAIL rows on every stream in dev and heldout; every arm B plan length equals arm A's.
  - after: the only failing checks, if any, are the wall-clock sign checks the campaign already records as load-dependent
    (P+/P0 on the frozen wall sum); no floor, reconciliation, control or mutant check newly fails.
- mutant: the "before" run is the mutant for the F2 check (unfixed policy): it must show F2 bad rows > 0, or the check is broken.
