# E03 extension: does retained experience repay its full cost? (aien-architecture#246, 2026-10-19)

Append-only. Protocol frozen first (00-frozen-protocol.md @293b49a7), amendments appended (AMENDMENTS.md), every
run's receipt kept whether it passed or failed. All numbers below are copied from the receipts by summarize.py
(SUMMARY.md); none is typed by hand except the aien-calc receipt ids.

## Plan section 10 fields

- experiment_id: E03-PAYBACK-20261019. parent_id: PLAN_REUSE (receipt d8c8246c, candidate 7d562020, 2026-09-28).
- owner: sa-2-533a4d94 (ledger L049). reviewer: none yet (independent review is open, see acceptance box 5). date: 2026-10-19.
- objective_family: plan applicability (arrangement domain, blocks-world lifted onto World objects). contract_track: C
  (objective-level efficiency: seconds per accepted objective). exact_semantics_version: spec/plan-reuse.md, OMEGA_PLAN_TEMPLATE_V1.
- source_pins: omega base a8ffd2b9; branch research/e03-payback-246 commits 293b49a7, a45c8c99, 503f990f, 23472463, 8cb6351b,
  0012d413, c47be3b8, 64be1932, 9f65e306; aienos lock b84c0a67 (build/aienos-cap); physics pin 6d7cf0d4 (~/workspace/physics-pf-6d7cf0d4).
  executable_hash: test_binary_sha256 in each receipt (run 4/5: 4849084542ca...). model_hash: none (no model; A* planner).
  checker_identity: rx_plan_execute verification (success predicates, invariants, evidence words, crumb verify) plus goal_holds.
- hypothesis: retained plan templates on the existing rx_plan cache repay acquisition plus maintenance on a repeated stream within
  H = 40 tasks; they do not on a stream where every retained template is refused (P0). competing_explanations: wall-clock noise on a
  shared box; the identity hash the harness computes in both arms; execution (rx_graph_compile) dominating e2e so the effect is
  small; arm order within a task; adaptation producing valid but longer plans.
- predicted_measurements: 01-prediction.json (wall, @23472463) and 01-prediction-cpu.json (cognition CPU, @64be1932), both committed
  before the held-out runs they apply to.
- bound_id: none claimed. proof_assumptions: net(H) model assumes stationary p and Delta, geometric retention r, maintenance m per
  opportunity; r = 1 observed (the cache never evicts; forgetting only through STALE marks). invalidation_conditions: measured
  net outside the predicted band; floors violated; controls with the wrong sign.
- baseline: arm A, no reuse (A* every task, execute, verify; no identity hash, store or index). intervention: arm B, existing reuse
  (retrieve, bind, applicability, execute with CANDIDATE reference-first, record, store on miss, publish index) with the adaptation
  path; ablation row: arm B without adaptation (PAYBACK_NO_ADAPT, runs 3 and 5). Arm C (#584 learning arm): not available
  (omega-research-584 had no runnable arm at run time); two arms only.
  controls: P+ (S1 merge), P0 (world generation bumps every task), STALE template, three corrupt records, mutants M1 (ledger drops a
  phase), M2 (reload skips the canonical-bytes check), M3 (sign detector swaps arms). workload_partition: development seeds 200..211
  (12 per shape), held-out seeds 300.. (+100 per stream), H = 40 per stream, frozen before any run.
- hardware_state: DGX Spark GB10 host, process pinned to the 10 Cortex-X925 cores; load average and runnable count at start and end
  in every receipt (run 1: 16.6 / 6 runnable; runs 2, 3: 15.8 to 16.5 / 25 to 28; dev run 2: 50 / 63; runs 4, 5: 69 / 66 to 76).
  cache_state: fresh per stream except S2 (warmed on development seeds; warm-up rows kept as S2_warmup_dev_seeds).
  instrumentation_state: CLOCK_THREAD_CPUTIME_ID for cognition phases, CLOCK_MONOTONIC for execution phases and e2e; joules
  UNMEASURED (no aien_spbm package counter on this kernel; the UPS reads 10 W x 2 s, a sub-millijoule per-task delta is below it).
- budget: 5 held-out runs x 688 trace rows + 2 dev runs x 60 rows, each run under 2 s of CPU; search bounded at MAX_EXPAND 2e6
  (never reached); no GPU; storage 34,056 bytes per template (sizeof PlanTemplate).
- measurement_protocol: 00-frozen-protocol.md. uncertainty_method: per-hit IQR for Delta, Wilson 95% for p, per-task IQR of the
  net, bands from those. stopping_rule: one run per stream of record; a code fix is a new receipt beside the old (A1, A4).
- raw_receipts: dev/dev-receipt.json, dev/dev-run2-receipt.json, heldout/heldout-run{1,2,4}-receipt.json,
  heldout/heldout-run{3,5}-noadapt-receipt.json, traces *.csv, run logs *.log, dev/calc-receipts/ (13 aien-calc receipts),
  dev/dev-mutant-M{1,2,3}.log. commands: handoff and the run logs (quiet-measure flock /tmp/aien-gb10.lock ...).
  outputs: SUMMARY.md. negative_controls: P0, STALE, corrupt x3, mutants x3.
- discovery_cost (acquisition, cognition CPU, per template): 74 to 100 us (identify hash 62 to 90 us of it, store 0.6 us, decision 2 us,
  index 2 to 4 us). verification_cost: reference run on first reuse of a CANDIDATE 31 us wall; every execution verifies success
  predicates, invariants, evidence and crumbs (136 to 180 us wall, both arms). execution_cost: compile 2.4 to 3.2 ms + run 0.3 to 0.5 ms
  wall per task, both arms (80% of e2e). reuse_count: 39 hits per 40-task S1 stream; 280 hits and 64 misses over the nine held-out streams per run.
- result: see the acceptance table. claim_strength: see the verdict.
- remaining_gap / next_discriminating_experiment / revisit_trigger: see the end.

## What the lifecycle costs (cognition CPU, run 5, arm B over 344 tasks; run 4 within 3% of these)
goal read 538 us, lookup 93 us, applicability 570 us, search on misses 284 us, generalize 130 us, identity hash 4,081 us,
store 118 us, decision records 965 us, index publish 891 us, serialize 98 us, reload 79 us. Arm A over the same 344 tasks:
goal read 569 us, search 4,129 us, generalize 1,086 us (hash excluded). Execution (compile, run, verify) is 0.9 to 1.4 s wall
per arm per run and is the same work in both arms (floors F2, F3 hold on every S1 task).
The identity hash is the acquisition cost: 62 to 90 us per template, the same order as one A* search on merge (42 to 52 us) and
twenty times a search on reverse (3 to 4 us).

## Acceptance (the five boxes of #246 "Additional acceptance")

| box | result | evidence |
|---|---|---|
| 1. Full lifecycle ledger reconciles with traces, including failed acquisition/reuse | PASS for reconciliation (runs 2 to 5: 688 rows, 0 bad; run 1: 1 row off by 2 us, cause fixed in A1, receipt kept). Mutant M1 goes red (2 to 3 bad rows). Failed reuse is exercised only by the STALE control (no natural failed reuse: false applicability 0). Failed acquisition NOT_OBSERVED (A* never exhausted its budget). | heldout/*-receipt.json "reconciliation"; dev/dev-mutant-M1.log |
| 2. Positive-payoff and no-payoff controls detect the expected sign; stale/corrupt plans refuse or revalidate through existing rules | PASS on cognition CPU in every run (P+ positive, P0 negative; M3 goes red). FAIL on the frozen wall sum in runs 2, 3, 4, 5 and dev run 2 (the sign flips under 25 to 76 runnable processes); PASS in run 1 only. Stale: refused on the stale axis, re-derived into the same record as CANDIDATE, next reuse runs the reference first, promotes (every run). Corrupt: 3 of 3 refused (decode error; canonical bytes differ from the carried body, twice); M2 goes red (2 stored). | receipts "controls"; dev/dev-mutant-M{2,3}.log; AMENDMENTS.md A4 |
| 3. Predicted and measured break-even horizons plus uncertainty on held-out streams (#262) | DONE. Cognition CPU: merge predicted net(40) +1,726 us [+1,130, +1,835], break-even 4 tasks; measured +1,487 us (run 4) and +1,441 us (run 5), break-even 3, both in band. Reverse predicted -39 us [-100, +41], break-even 81 (beyond H); measured -88 us and -54 us, no break-even within 40, in band. P0 predicted -2,738 us (band from the m IQR: [-2,745, -2,727], aien-calc calc:5e040624..., calc:172dc0cc...); measured -2,762 us (0.6% outside) and -2,723 us (inside). Wall: prediction (01-prediction.json) vs run 1: merge +1,565 us [+326, +2,299] measured +2,323 (1% above the band); reverse -666 us [-1,273, -219] measured -7 (outside: the dev wall Delta was noise); P0 -2,535 us measured -5,769 (one +2.3 ms outlier); runs 2 to 5 wall figures carry outliers of 1 to 100 ms and are not interpretable. | SUMMARY.md; 01-prediction*.json; dev/calc-check.md |
| 4. Retention/transfer/restart outcomes and quality floors beside cost; no general-agent claim | DONE. Retention: r = 1 (no forgetting without a stream change; 0 forget events, 0 natural STALE marks). Transfer to held-out bindings: 39 of 40 hits on new object identities (S1, S2, S6); to a new composition (OTHER shape): miss, acquisition, then reuse. Negative transfer: with the adaptation path, merge goals retrieve the reverse template (same goal shape, one chain of six), get refused on required_state, and are repaired into a valid 12-move plan where search finds 9 (swapped reverse: 10 vs 7); the repaired plan is stored and reused: F2 floor FAIL on S2, S3, S4 (runs 1, 2, 4: 9 to 14 longer plans per stream). Without adaptation (runs 3, 5) every floor holds on every stream (3058/3060 checks, only the wall-sign checks fail). Restart: 1 record serialised (34 KB + 1 KB canonical) and reloaded as CANDIDATE, next reuse revalidates with the reference run; serialize + reload cost 177 to 228 us CPU, more than two acquisitions. Stale generations (S5): 9 refusals, 9 re-derivations into the same record, net -713 us CPU. Interference (S3): 5 templates (170 KB), applicability cost per lookup grows from 0.2 us to 3.0 to 3.6 us as same-shape templates accumulate; lookup itself stays 0.2 to 0.4 us. Claim is for this domain only. | SUMMARY.md stream tables; receipts "memory", "robust", "cognition_cpu" |
| 5. Preserve prior gates and historical failures; append immutable evidence and independent review | Gates untouched: tests/runtime/rx_plan_reuse.c and tests/gates/ unchanged; the 2026-09-28 receipt and its G1 FAIL stand; the Makefile change is one additive target. Evidence appended (this folder). Independent review: NOT_DONE (open; a different model should re-derive the verdict from the receipts and re-run `make build/rx_plan_payback_test && ./build/rx_plan_payback_test heldout`). #276 Symposium crossover: recorded by the #583 agent (docs/research/276-crossover-matrix.md @1cd04c8e on research/cost-sim-583), no overlap. | git log; this folder |

## Reinvestment ledger (cognition CPU, run 5, per stream; saved_gross uses wall deltas on hits as frozen, so it is noisy; the CPU columns are the usable ones)
Over the nine held-out streams (344 tasks: 280 hits, 64 misses) arm B spent 4,744 us of learning CPU (acquisitions 4,362 us,
maintenance 206 us, repair 0 us, restart 177 us). Savings were banked only where search is expensive: merge hits save 38 to 40 us each
(39 hits per stream). On S6 the restart (177 us) was fully funded by earlier savings (reinvested 177 us, unfunded 75 us); on every other
stream nothing was reinvested because nothing had been saved first (unfunded = spent). Run 4 (with adaptation): 4,942 us spent, of
which repair 41 us and restart 228 us; reinvested 350 us. Nothing is counted twice: saved_gross counts only hits, spent_learning only
misses, repairs, restarts and maintenance.

## Verdict: does retained experience repay its full cost on this stream?

- Merge two towers (search 42 to 52 us): YES. Cognition CPU net +1.44 to +1.49 ms over 40 tasks, break-even at task 3 (predicted 4),
  acquisition 96 to 100 us repaid by 39 hits of 38 to 40 us. claim_strength: measured (cognition CPU, two held-out runs, prediction
  in band). On wall e2e the same stream is INCONCLUSIVE: the effect (1% of a 3.9 ms task) is below the noise of this shared box.
- Reverse a tower (search 3 to 4 us): NO within H = 40. Each hit saves at most 1 us of CPU because the lookup, applicability,
  decision and index work equals the search it replaces; the 77 us acquisition is never repaid (predicted break-even 81, measured
  none). claim_strength: measured. This is the same shape the 2026-09-28 receipt failed G1 on (1.2x).
- No-payoff stream (every template refused): NO, by -2.7 ms over 40 tasks, 68 us per task of pure overhead (hash, store, decision,
  index, lookup, refusal), within 1% of the prediction. claim_strength: measured.
- Changed preconditions, interference, stale generations, restart: NO on cognition CPU in every run (net -159 to -714 us): the
  re-derivations, repairs and restart cost more than the hits save when the hits are reverse-shaped. claim_strength: measured.
- With the existing adaptation path on, the arm fails its quality floor (longer plans retained and reused) on every mixed stream,
  so its cost figures do not count regardless of sign. claim_strength: proven_in_scope (deterministic plan lengths, repeated in runs
  1, 2 and 4; absent in runs 3 and 5).
- Wall-clock seconds per task: INCONCLUSIVE for every stream at this effect size on this box (sign controls fail in 4 of 5 held-out
  runs). Joules: UNMEASURED.
- Nothing here is a general-agent claim: one domain, one planner, one host.

## remaining_gap / next_discriminating_experiment / revisit_trigger
- Gap: the adaptation path has no "compare with a fresh search" guard, so a repaired plan can be longer than the plan search would
  find, and once stored it is reused. Next: a bounded always-search control (the #246 minimum-work policy) that refuses to store an
  adapted plan longer than the search result; measure the extra search cost against the length regression. Trigger: any stream that
  mixes goal shapes with equal chain structure.
- Gap: the identity hash (62 to 90 us) is the acquisition cost and is paid in both arms by pla_generalize. Next: hash only on store
  (arm A never needs it) and measure the acquisition cost drop; trigger: any change to rx_plan_identify.
- Gap: wall seconds at this task size cannot be resolved on a box with 25+ runnable processes. Next: repeat runs 4 and 5 on a quiet
  box (runnable < 10) or on a 10x longer task (deeper towers) so the effect exceeds the noise; trigger: a quiet window.
- Gap: joules. Next: the aien_spbm package counter on a kernel that exposes it, over a 30 s steady loop of each arm.
- Gap: independent review (acceptance box 5) and arm C (#584) on the same streams once that arm exists.
