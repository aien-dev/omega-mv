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
| `heldout_results.md` | gate verdicts, errors, coverage, controls | `tests/dagsim/heldout_memcpy.sh compare` |
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

See `heldout_results.md` for the measured boxes and the handoff
`~/handoffs/2026-10-19-research-583.md` for the full checklist.
