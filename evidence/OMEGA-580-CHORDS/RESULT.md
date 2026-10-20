# omega-mv#580 result (rolls, chords, dependency graphs on the GB10)

Branch feat/580-chords. Chip run r4 (06-chip-r4): 40 checks, 0 failed. r3 had 3 failing rope checks; cause was the test's host reference being FMA-fused, fixed by -ffp-contract=off in the test build (87599cf0). Host tests 21/21.

All numbers measured with the machine loaded by other jobs (load average 50 to 70), so they are not clean-machine figures. No comparison to the hardware ceiling has been made.

| what | jobs/s | note |
|---|---|---|
| one note at a time | 1040 (chip r4), 906 (joules run) | |
| roll depth 1 / 16 / 64 | 146k / 264k / 267k | |
| chord K=1 / 4 / 16 / 64 | 258k / 817k / 1.70M / 2.27M | ORDER_NONE |
| voices 1 / 2 / 4 (depth 16 each) | 263k / 167k / 108k | more voices measured SLOWER |
| harmony DAG (SERIAL, WAVE, SEM) | 30 to 43 graphs/s | every node exact; ordering-removed control showed 0 wrong words (informational) |

Joules (07-joules, 60 s each, receipts in that folder): wall-power increment is NOT usable. The UPS reads the whole outlet in 1 percent steps while 50 to 70 other jobs ran, and the increments came out negative or noise-sized (note -2380 J, chord -721 J, roll +955 J). Only the GPU-reported power is usable, GPU watts x seconds / jobs:
- note: 14.49 W mean, 54,393 jobs: 0.0160 J/job (calc:39b2a4cdfe33)
- roll depth16: 21.97 W, 14,943,283 jobs: 8.82e-5 J/job (calc:b0be524a)
- chord K64: 23.25 W, 125,850,240 jobs: 1.11e-5 J/job (calc:a94bf083)
GPU power includes whatever else was using the GPU (lock_holders_max up to 7 in receipts), so treat as a mixed upper bound, not a clean per-job cost.

Open: clean-machine rerun, ceiling fraction, why 2 and 4 voices are slower than 1.
