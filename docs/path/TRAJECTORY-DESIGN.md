# Path trajectory evaluation: current-state audit and design (WIP)

Status: IN PROGRESS, branch `feat/path-trajectory`. Audit done against main 40d1ea3;
Pareto kernel landed; evaluator, assigner, tests and benchmark not yet written.

## Current-state audit (from code, 2026-10-02)

1. Semantic paths: `RxPath` (src/path/rx_path.h): typed steps in an append-only arena,
   anchors, sorted attributes/constraints. SEMANTIC_PATH_ID = SHA-256 over the canonical
   encoding under tag `omega.path.v1`. Forks share a prefix by reference (spec 7.1).
2. Identity vs realization: `rx_path_realization_id` hashes (semantic id, machine id,
   engine profile id, code digest, schedule kind, ARGUS observation digest) under
   `omega.path.realization.v1`. No telemetry enters either hash.
3. Per-realization predictions: `RxCostModel` (src/runtime/rx_costmodel.h): Bayesian
   linear regression per (cell, arm) on log2 ps/call; `RxCmPrediction` carries mean,
   sd, dof, n, known, borrowed, energy_pj, fail_p. Also per-arm verify_ns, synth_ns,
   code_bytes, power_mw. Matvec only (RX_CM_OPS = 1).
4. Measured vs modeled: latency fitted from measurements; energy = predicted ps x
   measured power (0 = unknown); fail_p from trial counts; verify/synth/code bytes stored
   as given by the caller. Transition costs: nothing exists.
5. `cq_rank` (src/runtime/rx_capq.c): strict Pareto front over the named dimensions,
   then per-priority band narrowing (within best x (1 + tol_ppm)), ties on the id tuple.
   Now a thin binding over `src/rx_pareto.{h,c}`; the oracle test (951 checks) passes.
6. Hard admission (`cq_query_admit`): liveness, locality, types, effects, authority
   ceiling, budgets, reliability, evidence, max_cost, min_confidence, require_held,
   caller admit callback. All before ranking; none is a penalty.
7. Planner (`pla_plan`): A*, g = step count (ng = cur.g + 1), h = misplaced goal objects
   + outsiders on protected units, f = g + h.
8. Constant costs: one move = PLA_MOVE_ENERGY (5); step cost uniform 1.
9. Machine/locality: CqEntry.machine_id index + `local` flag + CQ_LOC_LOCAL/FABRIC;
   AienMachineId canonical records. No cost of changing machine anywhere.
10. Extendable without duplication: rx_pareto kernel (shared), RxPath + RxPathRealization
    (sidecar metrics attach by reference), RxCmPrediction (consumed through a provider
    interface). The arrangement A* is left untouched (approach 1 of the brief).

## Selected architecture

- `src/rx_pareto.{h,c}`: the one Pareto front + deterministic selection rule, heap free.
  `cq_rank` rebuilt on it (behavior preserved by the oracle test).
- `src/path/rx_path_eval.{h,c}` (next): PathCost vector sidecar, deterministic evaluator
  over (RxPath, per-step realization refs, cost provider), known/unknown bit per
  dimension, saturating integer aggregation (SUM / MAX / probability composition /
  COUNT), frontier over whole trajectories via rx_pareto, interval-aware compare
  (improved / dominated / tied / incomparable / unresolved), local stability verdict,
  bounded radius-1/2 realization neighborhood, prefix reuse for forked children.
- Bounded label-setting assigner over a fixed semantic path (steps x alternative
  realizations, Pareto labels per node) alongside, not inside, the arrangement A*.
- `src/runtime/rx_path_costmodel.{h,c}` (next): adapter from RxCmPrediction to step
  costs with intervals; transition cost reported UNKNOWN (the model has none).
