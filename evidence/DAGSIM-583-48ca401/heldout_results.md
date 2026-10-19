# Held-out results: shared-bandwidth model vs fresh memcpy measurements

Gates were precommitted in PRECOMMIT.md before any of these rows were measured.

| k | measured median GB/s (n, min..max) | predicted GB/s [hi..lo] | rel. error | covered | lookup ctrl GB/s (err) | spec roofline GB/s (err) | envelope |
|---|---|---|---|---|---|---|---|
| 2 | 85.7 (10, 81.4..89.9) | 35.8 [34.3..37.1] | -58.2% | no | 17.9 (-79.1%) | 35.8 (-58.2%) | in |
| 3 | 107.0 (10, 89.4..117.1) | 53.7 [51.4..55.6] | -49.8% | no | 55.9 (-47.8%) | 53.7 (-49.8%) | in |
| 5 | 123.9 (10, 102.6..134.4) | 65.4 [60.8..69.7] | -47.2% | no | 55.9 (-54.9%) | 89.5 (-27.8%) | in |
| 6 | 113.5 (10, 93.5..128.5) | 65.4 [60.8..69.7] | -42.4% | no | 65.9 (-41.9%) | 107.4 (-5.4%) | in |
| 4 | 101.2 (10, 78.0..123.1) | 65.4 [60.8..69.7] | -35.4% | no | 55.9 (-44.8%) | 71.6 (-29.2%) | in (lookup home ground) |
| 16 | 129.2 (10, 103.8..136.5) | 65.4 [60.8..69.7] | -49.4% | no | 43.0 (-66.7%) | 273.0 (+111.3%) | OUT (16 threads + load above 20 cores) |

## Gates (precommitted)

- G-err: nominal prediction within 25% of the measured median on each of k=2,3,5,6: 0 of 4 passed. FAIL
- G-cov: [lo..hi] interval covers the measured median on at least 3 of 4 held-out k: 0 of 4 covered. FAIL
- Energy: UNMEASURED (no wall instrument integrated over these runs).

Load during measurement is in the header lines of measured/*.csv; every number is a loaded-box number.
