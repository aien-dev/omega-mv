# Held-out results: shared-bandwidth model vs fresh memcpy measurements

Gates were precommitted in PRECOMMIT.md before any of these rows were measured. Calibration: round2-same-state (evidence/DAGSIM-583-48ca401/round2_fit).

| k | measured median GB/s (n, min..max) | predicted GB/s [hi..lo] | rel. error | covered | lookup ctrl GB/s (err) | spec roofline GB/s (err) | envelope |
|---|---|---|---|---|---|---|---|
| 2 | 55.7 (10, 54.1..57.4) | 75.7 [60.6..87.7] | +35.9% | no | 37.9 (-32.0%) | 75.7 (+35.9%) | in |
| 3 | 83.9 (10, 74.9..112.1) | 95.0 [60.6..113.0] | +13.2% | yes | 95.0 (+13.2%) | 113.5 (+35.3%) | in |
| 5 | 95.4 (10, 65.0..100.1) | 95.0 [60.6..113.0] | -0.4% | yes | 95.0 (-0.4%) | 189.2 (+98.3%) | in |
| 6 | 94.0 (10, 90.9..115.7) | 95.0 [60.6..113.0] | +1.1% | yes | 95.0 (+1.1%) | 227.1 (+141.6%) | in |
| 4 | 83.8 (10, 77.1..119.5) | 95.0 [60.6..113.0] | +13.4% | yes | 95.0 (+13.4%) | 151.4 (+80.7%) | in (lookup home ground) |
| 16 | 99.9 (10, 98.9..120.1) | 95.0 [60.6..113.0] | -4.9% | yes | 95.0 (-4.9%) | 273.0 (+173.3%) | OUT (16 threads + load above 20 cores) |

## Gates (precommitted)

- G-err: nominal prediction within 25% of the measured median on each of k=2,3,5,6: 3 of 4 passed. FAIL
- G-cov: [lo..hi] interval covers the measured median on at least 3 of 4 held-out k: 3 of 4 covered. PASS
- Energy: UNMEASURED (no wall instrument integrated over these runs).

Load during measurement is in the header lines of measured/*.csv; every number is a loaded-box number.
