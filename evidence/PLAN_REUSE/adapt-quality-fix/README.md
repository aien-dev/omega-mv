# PLAN_REUSE adaptation quality fix (ledger L168, 2026-10-20)

Append-only. Defect from the E03 payback campaign (aien-dev/omega-mv research/e03-payback-246 @66e092ac,
evidence/PLAN_REUSE/e03-payback/README.md acceptance box 4 and Verdict): with adaptation on, a merge goal retrieves the
reverse-tower template (same goal shape), is refused on required state, is repaired into 12 moves where search finds 9
(swapped reverse: 10 where search finds 7), and the repaired plan is stored and reused.

Where the defect lived: the repair loop was inline in each caller (tests/runtime/rx_plan_reuse.c solve() and its copy in
tests/runtime/rx_plan_payback.c). It kept the shortest repair but never compared it with a search for the goal itself.
What changed: the loop is now the runtime function pla_adapt() (src/runtime/rx_plan_arrange.c, declared in
rx_plan_arrange.h). It takes the caller's search plan for the goal and admits a repair only when it is no longer; otherwise
the search plan is run and stored. The parent template is not marked STALE (it was refused, not reused and failed; STALE
is per template, not per goal shape, and would refuse its own valid reverse goals). spec/plan-reuse.md says so.

| run | date | host | head | command | load (1/5/15) and MemAvailable, start / end | instrument | result | claim_strength | verdict |
|---|---|---|---|---|---|---|---|---|---|
| 01 red | 2026-10-20 | DGX Spark (GB10) | cc3261eb (base a8ffd2b9 runtime + new check) | PLAN_REUSE_NO_ENERGY=1 rx_plan_reuse_test | see 01-red-base.log header | plan lengths from the planner (deterministic) | 8 failures: merge 12 vs 9 run and retained (twice), swapped reverse 10 vs 7 run and retained (twice) | proven_in_scope | FAIL (expected: red before green) |
| 02 green | 2026-10-20 | DGX Spark | 9562508d | quiet-build nice -n 10 make -j4 ... OUT_DIR=../targets/omega-rxplan-adapt test-plan-reuse | 81.86 / 80.99, 59.1 / 59.4 GB | same | 1326 checks, 0 failures; merge 9 = 9, swapped reverse 7 = 7, next reuse 9 and 7 | proven_in_scope | PASS |
| 03 G2 A/B | 2026-10-20 | DGX Spark | base cc3261eb vs fix 9562508d, alternating x3 | PLAN_REUSE_NO_ENERGY=1 rx_plan_reuse_test | about 70 throughout | thread CPU ratios (not a timing of record) | G2 (reverse CPU ratio >= 10x) failed once on base and once on the fix's run 02 under load; passed 3 of 3 fix runs here. G1 FAIL (1.2x) on every run, both arms: the 2026-09-28 history | inferred (load noise) | G2 INCONCLUSIVE under load, not caused by this change |
| 04 make test | 2026-10-20 | DGX Spark | 4060eca5 | quiet-build nice -n 10 make -j4 ... test | see 04-make-test.log | exit code | exit 0 (omegatool does not link rx_plan) | measured | PASS |
| 05 payback | 2026-10-20 | DGX Spark | 42589225; harness copied from 66e092ac (00-precommit.md) | rx_plan_payback_test dev / heldout, adaptation on | 61.26 / 61.80, 63.9 / 62.8 GB (heldout); see each log | F2 floor = plan length arm A vs arm B per task | see below | proven_in_scope | PASS (prediction held) |

Run 05 detail (binaries: before sha256 4849084542ca... equals the campaign's runs 4 and 5; after 8d305677a8e1...):

- heldout before: 3060 checks, 52 failures: 51 F2 rows (merge 9 vs 12: 33 rows, calc:9c6c5289; swapped reverse 7 vs 10:
  18 rows, calc:00510347; total calc:6faf6e74) on S2 warm-up, S2, S3, S4 (floors VIOLATED, negative transfer 11, 13, 14, 9),
  plus the P0 wall-sign check (load-dependent, as recorded in the campaign).
- heldout after: 3060 checks, 0 failures, 0 bad rows; floors ok on every stream; negative transfer 0 on every stream.
  Merge arm B 9 moves on every task (arm A 9); swapped reverse 7 (arm A 7). Repairs still admitted where they tie
  search (outsider case, 7 = 7: S3 1, S4 1).
- dev before and after: one failure each, the P+ dev wall-sign check (load-dependent); 0 F2 rows in both (dev streams
  do not mix goal shapes, so dev does not discriminate this defect).
- Mutant: the before run is the mutant for F2 and goes red (51 bad rows). The before run of rx_plan_reuse (01) is the
  mutant for the new check (8 bad rows).

Cost: not a timing of record. By construction every miss with adaptation on now pays one search plus the repair
searches (it skipped the goal search before when a repair was found). Because pla_plan is optimal (A*, admissible
heuristic), an admitted repair can only tie search, never beat it; that is a question for the E03 owner's
minimum-work policy, not a correctness issue. Joules: UNMEASURED.

Not claimed: anything beyond the arrangement domain and this planner.
