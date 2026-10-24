# DAGSIM-583 evidence: offline resource-DAG cost simulator (omega-mv#583)

Branch `research/cost-sim-583`, evaluator `tools/dagsim`, schema `spec/offline-evidence-v1.md`.
Host spark-b87b (GB10, 20 cores, 128 GiB unified). Everything here is offline: the evaluator never
executed anything, and the only new measurement is a CPU memcpy probe (no GPU). Every number below
is a loaded-box number; load averages are in the file headers.

## Contents

| Path | What | Produced by |
|---|---|---|
| `PRECOMMIT.md` | held-out gates and what FAIL means, committed before measuring | hand-written |
| `fixtures/memcpy_k{2,3,5,6,4,16}.oev` | held-out DAGs, fitted from M1 block4 k=1 and k=8 only | `tests/dagsim/heldout_memcpy.sh predict` |
| `predictions/memcpy_k*.json` | evaluator output per DAG (bounds, attribution, sensitivities) | `build/dagsim eval FILE --asof 2026-10-20` |
| `predictions.csv` | predicted seconds, lo/hi, GB/s, lookup control, spec-roofline control | same script |
| `measured/memcpy_k*.csv` | fresh measurements, 10 reps each, block4 CSV shape, load in header | `tests/dagsim/measure.sh` under `quiet-measure` |
| `measured/LOCK.txt` | whether the GB10 lock was held during the runs | same |
| `heldout_results.md` | round 1 gate verdicts (FAIL, FAIL): block4 morning calibration vs afternoon box | `tests/dagsim/heldout_memcpy.sh compare` |
| `heldout_round1_interpretation.md` | why round 1 failed (calibration taken under other tenants' DRAM traffic) | hand-written |
| `round2_fit/`, `fixtures/round2_*.oev`, `round2_predictions/`, `round2_predictions.csv` | same-state refit (k=1, k=8) and predictions committed before the held-out rows | `FIT_DIR=round2_fit ... predict` |
| `round2_heldout/`, `round2_heldout_results.md` | round 2 held-out rows and verdicts (G-err FAIL 3/4, G-cov PASS 3/4) | `measure.sh`, `... compare` |
| `heldout_round2_interpretation.md` | pipe regime retained, low-k independence falsified, next experiment | hand-written |
| `PRECOMMIT_round3.md`, `round3_x925/`, `round3_a725/`, `heldout_round3_interpretation.md` | core-class pinned k=1,2,3; INCONCLUSIVE at load 56 | `measure.sh PIN=`, hand-written |
| `cli-example/` | `run_cli.sh` outputs on the pinned M1 fixtures (JSON, Pareto table) | `tests/dagsim/run_cli.sh` |
| `test-dagsim.log` | `make test-dagsim` output (unit + CLI checks) through `quiet-build` | Makefile |

## Reproduce

```
cd <omega worktree at this branch>
make PHYSICS_DIR=~/workspace/physics-pf-6d7cf0d4 OUT_DIR=build test-dagsim      # 171 unit checks + 26 CLI checks, no GPU
build/dagsim eval tests/dagsim/fixtures/m1_spark_subset.oev --asof 2026-10-20
build/dagsim pareto tests/dagsim/fixtures/*.oev --asof 2026-10-20
sh tests/dagsim/heldout_memcpy.sh build/dagsim evidence/DAGSIM-583-48ca401 predict   # byte-identical to predictions.csv
```

The `--asof` date is required so a rerun on a later day reproduces the same validity check
(the M1 calibration is 30 days; after 2026-11-18 the fixtures refuse as STALE, by design).

## Status per #583 acceptance box

Held-out verdict in one line: the shared-pipe rule is retained for k >= 3 on the same box state
(within 1% at k=5,6, intervals cover); low-k per-thread independence is falsified (k=2 +36%);
a calibration from a different box state does not transfer at all (round 1, 1.7x to 2.4x).
Energy: UNMEASURED throughout. Selection regret: NOT_RUN. Independent reproduction: not done.
Full checklist in `~/handoffs/2026-10-19-research-583.md`.
