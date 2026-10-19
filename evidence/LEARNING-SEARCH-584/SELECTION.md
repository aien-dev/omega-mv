# SELECTION (written before the sealed run, 2026-10-19)

selected_c: C0
extra_reported: C1

## Rule applied (FREEZE.md section 7, unchanged)
Lowest median per-objective CPU ns among C variants meeting F1..F3 on all three calibration
streams (seeds 0x5851, 0x5852, 0x5853), medians pooled over the 720 scored objectives.

| arm | pooled median cpu_ns | success (of 720) | wrong (of 720) | F1 (inconsistent commits) | F2 per stream (succ >= 216, wrong <= A+1) | F3 stale control wrong-from-memory |
|---|---|---|---|---|---|---|
| A  | 2,608,896 | 681 | 10 | 0 | n/a (incumbent) | n/a |
| B  | 2,671,216 | 681 | 10 | 0 | pass (224/231/226; 5/2/3 vs A 5/2/3) | 0 (all wrong rows are path 1 = search) |
| C0 | 2,589,136 | 678 | 13 | 0 | pass (223/230/225; 6/3/4 vs A 5/2/3) | 0 |
| C1 | 2,630,128 | 684 | 13 | 0 | pass (225/232/227; 6/3/4 vs A 5/2/3) | 0 |

Selected: C0 (2,589,136 < 2,630,128). Source: evidence/LEARNING-SEARCH-584/calib/*/rows.csv,
column cpu_ns, rows with block > 0; controls/*-stale.

Weakness of the rule, recorded, not acted on: C0 and C1 run identical code on every
objective except COMPOSE (block 4), where C1 adds one concatenation try. The 1.6% pooled
median difference is within the run-to-run spread of identical code paths on this loaded
host (the same arm's block-1 median varied 2.37 to 2.80 ms across calibration seeds). The
rule therefore selected on noise. C1 is run on the sealed seeds as well and reported,
labelled unselected; the verdict uses C0.

## Predictions for the sealed streams (#262 out-of-sample rule), per stream, C0 vs A
P1. VERIFIED reuse rows per stream: 15 to 19; median cpu_ns of those rows 0.22 to 0.26 ms.
P2. Search-path cost on repeated objectives (block 2 and 5 misses) 2.3 to 3.1 ms; Delta
    (search path minus verified reuse) 2.1 to 2.6 ms.
P3. Applicability p (hits over the 72 reuse opportunities in blocks 2 and 5): 0.46 to 0.60.
    The limit is table capacity (32 of 64 keys retained, refused_full about 160 per stream),
    not forgetting: fitted r is 0.995 to 0.9985 per opportunity.
P4. Per-key payback: net(1) within +-0.05 ms of zero (the first applicable use re-realizes
    and saves nothing), net(2) = +1.0 to +1.5 ms, break-even H* = 2 applicable uses.
    No-payoff region: keys with fewer than 2 applicable uses, and every key the full table
    refused.
P5. Stream-level cumulative CPU: predicted saving of C0 over A = n_ver * Delta - persistence
    = about 30 to 45 ms of about 650 ms (5 to 7%). Cross-process run-to-run spread of
    identical code is larger than this (up to 15%), so the sign of (A total - C0 total) is
    predicted to be positive in at most 4 of 5 seeds and not in all 5. If all 5 come out
    positive by more than 10% the noise model, not the mechanism, is wrong.
P6. Quality: every arm success >= 216 of 240 on every sealed stream; C0 wrong <= A wrong + 1
    on every sealed stream; inconsistent commits = 0 everywhere.
P7. Controls on sealed seeds: shuffle gives success <= 3 and equal counts for A, B, C0 (no
    arm extracts anything from spurious feedback); nolearn C0 is decision-identical to A;
    stale gives 0 wrong rows with path 2; unseen gives 0 compose rows.
P8. Compositions (block 4): C0 = A exactly; C1 (unselected) 2 to 4 more successes than A
    and 2 to 4 fewer failed searches per stream.

A prediction that fails is reported as such; none is adjusted after the sealed run.
