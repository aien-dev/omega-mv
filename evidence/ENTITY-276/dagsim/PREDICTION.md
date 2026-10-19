# ENTITY-276 prediction before measuring (D3)

Written and committed before the first run of record. Evaluator: dagsim from
aien-dev/omega-mv branch research/cost-sim-583 at 9f8a9cea, built unchanged from a detached
worktree into ~/workspace/targets/omega-entity-276/dagsim (dagsim_unit: 171 checks, 0
failed; their run_cli.sh: all ok). Schema: OEV1 (spec/offline-evidence-v1.md). The spec
file at 9f8a9cea differs from the freeze point 5d864920 by one additive clarification
(a barrier with a measured cost is written as an op plus dep lines); no field this
fixture uses changed.

## Fixtures and results (`dagsim eval FILE --asof 2026-10-19`)

| Fixture | Inputs | Status | Makespan = critical path = CPU work bound |
|---|---|---|---|
| entity276_restart_unknown.oev | every op cost `unknown` (no receipted source) | REFUSED_UNKNOWN (exit 1): "op replay has prov=unknown" | none (refused, as it must be) |
| entity276_restart_warm_dev.oev | one development run, labelled `synthetic` | OK, synthetic=1 | 936,429,578 ns |
| entity276_restart_cold_dev.oev | same, no retained knowledge | OK, synthetic=1 | 969,313,876 ns |

`dagsim diff cold warm`: 1 op removed as `reused` (search2, 100,880 ns), 2 added (rebuild,
lookup2), 7 kept; it flags "trace conservation broken" because the kept exec ops come from
two different development runs with different times. That flag is correct.

The DAG is one serial chain on the harness main thread, so critical path, resource bound
and makespan coincide; the simulator adds no contention insight for this pilot.

## What is predicted (provenance: synthetic, from unreceipted development run devB)

1. Wall time per repeated objective is dominated by execution plus synchronous journal
   appends (CX_OPEN_SYNC), not by cognition: lookup 19,648 to 27,344 ns and search
   100,880 ns, against about 250,000,000 ns of wall time per objective.
2. Retained knowledge saves one search on the three repeated objectives (searches: warm 0,
   cold 1), but rebuilding the template table from the journal costs more than it saves:
   cognition warm (rebuild + 3 lookups) minus cold (search + 2 lookups) = +411,440 ns
   (calc:f1796e97). Predicted cognition verdict: retained knowledge does not pay back
   within three objectives.
3. Predicted makespan difference warm minus cold = -32,884,298 ns (calc:20599050), well
   inside the run-to-run spread of the exec steps; predicted G5 (cumulative process CPU,
   FREEZE section 5) = INCONCLUSIVE or FAIL. In devB the cumulative process CPU was warm
   minus cold = +7,139,056 ns (calc:be2872e2).

Comparison with the measured run: README.md, per op class, ratio measured over predicted,
with this provenance.

## Coverage gaps for the #583 owner (not patched here)

- No receipted source exists for these op classes: Cortex journal replay (cx_open), template
  rebuild (decode + rx_plan_identify), World restore through outside publications, plan
  lookup, action-graph execution on resident reactions. The honest fixture is refused.
- Synchronous journal persistence (fdatasync per append) dominates wall time, and OEV1 has
  no calibrated storage resource for it; `kind=persist` needs a storage bandwidth resource
  with a source.
- Makefile: `test-dagsim` runs `./$(DS_UNIT)`, which fails when OUT_DIR is absolute
  ("No such file or directory"); the binaries build, and the tests pass when run by path.
