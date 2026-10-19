# PRECOMMIT: held-out gates for the shared-bandwidth model (omega-mv#583)

Written and committed BEFORE any held-out row was measured. Branch
research/cost-sim-583. Evaluator: tools/dagsim at the commit that contains this file.

## Hypothesis under test

k threads copying one 1 GB buffer on the Spark CPU behave like k independent copy
ops whose own busy time is `T1/k` (per-thread rate from M1 block4, k=1) and whose
traffic `2 GB / k` each shares one pipe of capacity `CAP` (M1 block4, k=8). The DAG
evaluator predicts time = `max(T1/k, 2 GB / CAP)` with the interval from the receipts'
repeat spread. The structural claim being tested is the processor-sharing pipe:
concurrent full-bandwidth claims do not add.

Fitting set (used, never re-measured here): `~/yardsticks/aien-model/M1/block4.csv`,
rows gb=1 threads=1 (T1 = 0.1117 s median, 0.1079 to 0.1166) and gb=1 threads=8
(CAP = 65.4 GB/s read+write median, 60.8 to 69.8). Load during that receipt: 14.8 to 29.

Held-out set (measured after this file is committed): k = 2, 3, 5, 6 at 1 GB, fresh,
10 reps each, via `quiet-measure flock /tmp/aien-gb10.lock nice -n 10 memcpy_probe 1 k 10`.
Also measured: k = 4 (block4 has it: the lookup control's home ground, excluded from the
gates) and k = 16 (expected OUT of envelope: 16 threads plus the current load exceed
20 cores; excluded from the gates, reported as a refusal case).

## Development noise (what the gates are set from)

block4 repeat spread at 1 GB: k=1 is 17.1 to 18.5 GB/s (plus or minus 4% of median),
k=8 is 60.8 to 69.8 (plus or minus 7%), k=4 is 48.6 to 60.9 (plus or minus 11%),
k=16 is 34.6 to 47.1 (plus or minus 15%). Load was 15 to 29 and is 29 now.

## Gates (numbers fixed now)

- G-err (numerical error): for each of k = 2, 3, 5, 6, |predicted GB/s minus measured
  median GB/s| / measured median <= 25%. PASS needs 4 of 4. 25% is about twice the worst
  development spread at the held-out thread counts.
- G-cov (interval coverage): the evaluator's [lo, hi] interval (optimistic to pessimistic
  point from the receipts' lo/hi) contains the measured median for at least 3 of the 4
  held-out k. The interval is narrow (about plus or minus 7%), so under load 29 this gate
  may fail; a FAIL here means the receipts' repeat spread understates day-to-day variance
  and `sd_scale`-style widening is needed, which is a real finding, not a tuning knob.
- Controls reported beside the model, not gated: nearest-k lookup from block4 (log2
  distance), and a spec roofline `max(T1/k, 2 GB / 273 GB/s)`.
- Selection regret: not applicable to this DAG family (one candidate per k); reported as
  NOT_RUN for this held-out set.
- Energy: UNMEASURED. No instrument is integrated over these runs.

## What a FAIL means

The model is retained only for what passes. A G-err FAIL on k=2,3 (busy-time bound)
falsifies the per-thread rate carrying over from k=1; a FAIL on k=5,6 (pipe bound)
falsifies the k=8 pipe figure as a capacity. Either is recorded in the dossier as a
falsified cost hypothesis with the numbers.
