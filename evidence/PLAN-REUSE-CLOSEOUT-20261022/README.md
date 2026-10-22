# L462: reuse the completed L168 plan quality fix

L072's requested correctness work already exists. Reuse
[fix/rx-plan-adaptation-quality at 4c9fae6c2cbf1295d78841142d73e234b10e3cfe](https://github.com/aien-spark/omega/tree/4c9fae6c2cbf1295d78841142d73e234b10e3cfe).
This branch descends from that exact commit and adds only this evidence folder.
No new implementation, planner, seed, training input, threshold, skip, service,
worker, or ownership change. This is a verification packet, not a new experiment.
L072 remains open for the parent to reconcile with completed L168.

## Verified result

The repair loop formerly selected a valid repaired plan without comparing it
with search for the goal. L168 moves repair into `pla_adapt`, searches for the
goal first at the caller, and rejects a repair longer than that search plan
before executing/storing it. The parent template remains usable for its own
goals. Search exhaustion may still admit a repair with a NULL search plan;
ties are admitted. Those are documented limits, not newly qualified cases.

| Evidence | Published result | L462 verification |
|---|---|---|
| Regression RED, cc3261eb6b10cd45e491d610a85128ed3d23bbc6 | exit 1; 1326 checks, 8 failures; run/retained 12 vs 9 and 10 vs 7 | exact published log checked |
| Regression GREEN, 9562508d72f9e3258876a49a3ae8a84524ee70de | exit 0; 1326 checks, 0 failures; 9 vs 9 and 7 vs 7 including subsequent reuse | exact published log checked |
| Surrounding host `make test`, 4060eca53d947f514f05196345bfc1796282b536 | exit 0 | exact published log checked; does not link rx_plan |
| E03 heldout before | exit 1; 3060 checks, 52 failures; 51 F2 mismatches plus P0 wall sign | recomputed all 344 raw task pairs; 33 cases 9 vs 12, 18 cases 7 vs 10 |
| E03 heldout after | exit 0; 3060 checks, 0 failures | recomputed all 344 raw task pairs; F2 mismatches 0; seeds/order/reference lengths unchanged |
| Other heldout floors and controls | every stream floors_ok, negative_transfer 0; stale refused/rederived/revalidated, corrupt 3/3 refused, 0 stored | checked receipt fields and raw goal/success results |
| Development before/after | final console: 290 checks, 1 failure, bad_rows 1 in each arm | 30 raw task pairs each, F2 mismatches 0; P+ wall-sign failure preserved |
| New read-only artifact audit | exit 0 | `verification.json`; detects deliberately altered length and missing arm |
| Harness integration patch | dry-run exit 0 on exact E03 harness | `harness-patch-dry-run.log` |

The existing [L168 evidence](https://github.com/aien-spark/omega/tree/4c9fae6c2cbf1295d78841142d73e234b10e3cfe/evidence/PLAN_REUSE/adapt-quality-fix)
contains every original log, before/after trace, receipt and patch. This packet
hashes all 19 audited evidence files rather than copying or editing them.
`verification.json` records both binary SHA-256 values from the receipts.

## Report limitations found during verification

- All four payback receipts contain `run_commit: unknown`. The earlier
  `00-precommit.md` pins the copied E03 harness to
  `66e092ac50c8fea66960f38e1a704aa39cf07f69`, the runtime fix to 9562508d,
  and the integration patch. Binary hashes identify the actual recorded
  binaries; L462 cannot retroactively prove build provenance beyond this.
- The development JSON reports 286 checks, 0 failures, bad_rows 0, but its
  final console reports 290 checks, 1 failure, bad_rows 1. This is explained
  by `json_head` at E03 source line 1199, before four sign checks beginning
  at line 1215. Heldout writes its header after those checks (line 1316).
  The audit initially failed on this discrepancy (exit 1, preserved in
  `initial-audit-failure.log`), then was corrected to verify both documented
  snapshots explicitly. No source or old artifact was changed.
- `bad_rows` is not the F2 count: the before heldout receipt says 1 despite
  51 F2 failures. F2 counts here are recomputed from paired CSV plan lengths
  and independently matched to F2 console failures. They are never inferred
  from that aggregate field.
- The research branch still contains its old inline adaptation loop. L168
  supplies a patch for that separately owned harness; the patch applies
  cleanly. Renda should keep that integration requirement with E03 landing.

## Unchanged gates and NOT_RUN work

The overall `OMEGA_PLAN_REUSE_PASS` remains FAIL. G1 historically fails on
reverse-tower operation count. G2 is inconclusive under load; the archived
green correctness run records G2 FAIL. No full qualification PASS is claimed.
The F2 quality result is specific to this arrangement domain and fixed streams.

| Item | L462 status | Prerequisite / next owner |
|---|---|---|
| Fresh compile/regression/surrounding suite/E03 execution | NOT_RUN | Explicit brief directs reuse/verification of an existing completed solution rather than rebuilding it; archived executions verified above |
| New wall/CPU timing qualification | NOT_RUN | Separately authorized quiet-measure window and frozen protocol; E03 owner |
| Energy or GPU | NOT_RUN | Outside this host-only assignment; energy also needs a suitable package counter |
| GitHub PR creation/merge | NOT_RUN | Renda owns writes; use existing fix, preserve E03 harness patch requirement |
| L072 closure or reassignment | NOT_RUN | Parent coordinator recommendation: reconcile with L168 and this receipt |

## Discovery / non-overlap check

Fetched origin; `origin-refs.tsv` records remotely published main, fix and E03
refs. Main is a8ffd2b912c84197d08f963b05f9c7d8b30a3f8a. Main lacks this
fix. All-ref history of `rx_plan_arrange.c` shows only 9562508d and its
original implementation. Research/plan-reuse branches and prior handoffs
were searched before writing. The live ledger has L072 open/unassigned,
L168 done, and L462 assigned to this worker. Existing Renda desk line 450
already advertises the exact L168 SHA (short form).

Current process command lines and latest Codex session metadata showed L460
inventory, L461 delivery and this L462; no other active adaptation editor
was identified. The app-server process was included in the check. The shared
main checkout is dirty. E03's existing worktree has untracked build logs.
Both were left alone; the former fix worktree no longer exists.

Own worktree: `/home/drakestapleton/workspace/omega-plan-reuse-closeout-20261022`.
Own target: `/home/drakestapleton/workspace/targets/omega-plan-reuse-closeout-20261022`
(only an exact harness copy for patch dry-run; no build or binary).
Early handoff: `/home/drakestapleton/handoffs/unfinished-20261022/L462/handoff.md`.

## Commands and exits

Run from this worktree. No Python, agents, model loads or host builds used.

```sh
node evidence/PLAN-REUSE-CLOSEOUT-20261022/verify.mjs > evidence/PLAN-REUSE-CLOSEOUT-20261022/verification.json 2> evidence/PLAN-REUSE-CLOSEOUT-20261022/verification.stderr
# exit 0; initial stricter log/receipt equality attempt exited 1 as explained above

git show 66e092ac50c8fea66960f38e1a704aa39cf07f69:tests/runtime/rx_plan_payback.c > /home/drakestapleton/workspace/targets/omega-plan-reuse-closeout-20261022/tests/runtime/rx_plan_payback.c
# exit 0
patch --dry-run -d /home/drakestapleton/workspace/targets/omega-plan-reuse-closeout-20261022 -p1 < evidence/PLAN_REUSE/adapt-quality-fix/payback-harness-L168.patch
# exit 0

git diff --check
# exit 0
```

The audit reads immutable git blobs at the exact fix SHA, checks paired
before/after tasks, records artifact digests, verifies preserved historical
failures, and asserts no implementation change between the reused fix and
this branch. A PASS certifies these checks on archived executions only.
