# Round 2 interpretation: same box state, same probe

Fit: `round2_fit/` (k=1: T1 = 52.85 ms per GB, 45.7 to 58.3; k=8: 95.05 GB/s, 60.6 to 113.0),
measured 19:48:44Z under both locks, load1 25.1, MemAvailable 36 GB. Held-out: `round2_heldout/`,
19:56:45Z, both locks, load1 10.0, MemAvailable 28 GB (headroom rule kept). Predictions were
committed at 9f8a9cea before the held-out rows existed.

Verdict (`round2_heldout_results.md`):

- G-err FAIL, 3 of 4: k=3 +13.2%, k=5 -0.4%, k=6 +1.1% pass; k=2 +35.9% fails
  (predicted 75.7 GB/s, measured 55.7, spread 54.1 to 57.4, so it is not noise).
- G-cov PASS, 3 of 4 (k=3,5,6 covered; k=2 not).
- Controls: nearest-k lookup equals the model wherever the pipe binds (both use the k=8 figure)
  and is worse at k=2 (-32%); the spec roofline (273 GB/s) is +35% to +173% everywhere.

## What this says

Retained for this box state: the shared-pipe regime. From k=5 on, k concurrent copies run at
the k=8 pipe figure within 1%, with the interval covering. "No sum of concurrent full-bandwidth
claims" is the right rule and the processor-sharing pipe reproduces it.

Falsified: independence of per-thread busy time at low k. One thread copies at 37.9 GB/s, two
threads reach 55.7 together (1.47x, not 2x), three reach 83.9 (2.2x). The v1 op model has no
resource between "one core" and "the DRAM pipe", so it cannot express this.

Candidate explanation, not yet tested: the GB10 has two core classes (10 Cortex-X925, 10
Cortex-A725; `rx_costmodel` already separates `RX_CM_CORE_X925` and `RX_CM_CORE_A725`), and an
unpinned second thread can land on the slower class or on the other cluster's path to memory.

## Next small experiment (bounded)

Pin threads with `taskset` to X925 cores only and re-measure k=1,2,3 (three 20 s runs, CPU
only). If k=2 then reaches about 2x of k=1, the fix is a per-cluster bandwidth resource in the
fixture (expressible in OEV1 today: one `bandwidth` resource per cluster, no schema change).
If not, the per-op busy-time model needs a sublinear thread term, which is a v2 schema item.

## Residual

Unexplained after this round: the k=2 shortfall (20 GB/s of 75.7 predicted) and the wide k=8
repeat spread (60.6 to 113.0 GB/s at load 25). Both are reported as residual, not attributed.
