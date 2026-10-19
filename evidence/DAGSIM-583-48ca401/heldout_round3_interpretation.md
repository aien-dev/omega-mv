# Round 3 interpretation: INCONCLUSIVE (load 56 during the pinned runs)

`round3_x925/` and `round3_a725/` ran at 20:14Z under both locks, but the host load average
was 56 (20 cores), so the pinned cpus were shared with other tenants' threads the whole time.
Medians (10 reps, GB/s read+write):

| class | k=1 | k=2 | k=3 | k=2 over k=1 | rep spread at k=3 |
|---|---|---|---|---|---|
| X925 (cpus 5-9,15-19) | 51.9 | 62.5 | 56.1 | 1.20x | 20.2 to 104.6 |
| A725 (cpus 0-4,10-14) | 49.1 | 85.3 | 69.5 | 1.74x | 18.4 to 110.2 |

Against PRECOMMIT_round3.md: X925 k=2 is 1.20x of k=1, below the 1.6x line, which would read
as "falsified inside one class", but the k=3 rows are slower than k=2 in both classes and the
rep spreads span 5x, which is not a property of the memory path; it is other work on the same
cores. The precommitted reading assumed the pinned cores were ours. They were not. Verdict:
INCONCLUSIVE, not FALSIFIED and not RETAINED. The same six runs on a box with load under 10
(each is 20 s) would settle it; that is the next experiment, unchanged.

Two things this round did show, both consistent with round 2 and not depending on scaling:
single-thread memcpy is about the same on either class (51.9 vs 49.1 GB/s), so the per-thread
rate is not a core-frequency effect at k=1; and the sensor log confirms every held-out row of
the day was taken with a GPU job streaming the same DRAM, so no row here is a quiet-box number.
