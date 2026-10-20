# AMENDMENTS to FREEZE.md (recorded, never edited into the freeze)

## A1 (2026-10-19, after calibration, before the sealed run): shuffled-feedback control wording
FREEZE.md section 8 said the shuffled-feedback control must make learning arms "not commit
wrong programs (success must drop to abstention, not to wrong answers)". That clause cannot
be met by any arm and was mis-specified: under shuffled feedback the arm is handed
examples of another objective, solves them correctly (0 inconsistent commits), and the
sealed check against the hidden rule of the stated key scores the answer wrong. What the
control can show, and what is now the criterion, is: (a) learning arms do not beat A under
shuffled feedback (equal success and wrong counts), and (b) no arm commits a program
inconsistent with the feedback it was given. Calibration: A/B/C0/C1 all 1, 1, 2 successes
and 208, 217, 212 wrong on seeds 0x5851..0x5853, bit-identical across arms.

## A2 (2026-10-19, after the restart check, before the sealed run): C's ordering counts are not persisted
FREEZE.md section 9 said a cut-and-restart run must reproduce the counts of an uncut run.
B does (0 differing rows). C1 differs on 53 row cells of 248 after the cut because its
shape and constant frequency counts (part of z) live only in memory; only the table and
the realizations go through Cortex, and those reload bound (32 entries, 16 realizations
with recomputed triple ids). The sealed verdict does not depend on this (C0 is selected
by the same table mechanism); the gap is reported under recovery, and persisting the
counts as a Cortex record is listed as follow-up work, not done here.

## A3 (2026-10-19, before the sealed run): C1 is also run on the sealed seeds
The freeze runs A, B and the selected C on the sealed seeds. C1 is run as well, labelled
unselected and reported for completeness (SELECTION.md explains why the frozen rule
selected on noise). The verdict uses C0 only.

## A4 (2026-10-20, after the sealed run, before any r2 run of record): r2 re-measurement, interleaved arms and a longer horizon (ledger L167)
Reason: the sealed run left B INCONCLUSIVE because B always ran right after A in a separate
process at load 50 (same-code spread 43%), and C0 FAIL partly because the stream offers
about 1.1 reuse opportunities per key ((48 + 24) / 64) while payback needs 2. This entry is
the "Next discriminating experiment" of `evidence/LEARNING-SEARCH-584/README.md`. Sections 1
to 10 of FREEZE.md stay as they are for everything not named here. The sealed seeds
0x5861..0x5865 are not run again and their evidence is not touched.

What changes:
1. Interleaved execution. One process runs the lanes A, A2, B, C0 on one stream, one
   objective at a time: for objective i every lane takes its turn before objective i+1.
   A2 is a second, independent copy of A (same code, same objectives, same minute): the
   same-code noise floor. Lane order per objective follows a balanced (Williams) Latin
   square over the 4 lanes (rows 0 1 3 2 / 1 2 0 3 / 2 3 1 0 / 3 0 2 1 by objective index
   mod 4), so every lane runs in every position and right after every other lane equally
   often. Each lane has its own table, its own Cortex journal and its own counters; lanes
   share nothing but the read-only stream. Timing per objective is CLOCK_THREAD_CPUTIME_ID
   inside the lane's own turn, as before.
2. Stream parameter R, "reuse opportunities per key", values 1, 3, 6. Blocks 2 and 5 are
   run R times: passes 2..R repeat pass 1's key sequence with fresh example and check
   inputs, drawn from their own generator, so every key gets exactly R times its
   sealed-design reuse opportunities (on average 72 R / 64 per pool key: 1.125, 3.375,
   6.75), and blocks 1, 3, 4, 6, 7 are the same objectives for every R. R = 1 is the sealed
   design (control): with one lane it reproduces the sealed harness decision for decision.
   Stream length 240, 384, 600 objectives (+ 8 preface commands).
3. Seeds: new calibration seeds 0x5871, 0x5872, 0x5873 (harness checks only: decision
   identity, mutants, controls; nothing is selected or fitted on them); new held-out seeds
   0x5881..0x5885 (run of record). Never 0x586x.
4. New checks, each with a mutant that must go red: M4 (lane isolation: lane A2 runs on
   lane B's state; A2 rows must then differ from A rows), M5 (horizon: passes 2..R draw keys
   afresh; the per-key opportunity count must then fail to be exactly R times).
   M1, M2, M3 unchanged and must still be red; disabled learning (C0 nolearn) must still be
   decision-identical to A at R = 1, 3, 6.
5. Arms in the run of record: A, A2, B, C0 interleaved, R in {1, 3, 6}, 5 held-out seeds:
   15 runs, all in one hold of quiet-measure + /tmp/aien-gb10.lock, after waiting for
   1-minute load under 15 (polled every 60 s, at most 90 minutes, then run anyway with the
   load recorded). Correctness-only controls after it, unmeasured: stale (B, C0) and nolearn
   (C0) on every held-out seed and R.

Verdict rule (frozen here, per arm L in {B, C0}, per R, over the 5 held-out streams s):
- d_L(s) = A(s) - L(s), stream CPU totals in ns from the interleaved run;
  n(s) = |A(s) - A2(s)|, the same-code noise floor of that stream.
- Quality floors as FREEZE.md section 5: F1 (0 inconsistent commits, 0 unverified
  realizations), F2 (success / objectives >= 0.90 and wrong(L) <= wrong(A) + 1 per stream),
  F3 (stale control: 0 wrong rows with path 2).
- FAIL if any floor fails on any stream, or d_L(s) < -n(s) on all 5 streams (detectably slower).
- PASS if every floor holds and d_L(s) > n(s) on all 5 streams (saving larger than the
  same-code noise on every stream; under no effect, 5 of 5 positive has chance 1/32).
- INCONCLUSIVE otherwise.
Each d_L(s), n(s) and comparison is computed with aien-calc (receipts in `r2/calc/`).

Predictions (#262 rule), written before the run of record, from the earlier calibration
only (`calib/A-585x`, `calib/B-585x`, measured 2026-10-19, `netfit.sh` inputs). Per
calibration seed: H1 = hits in blocks 2 and 5, c1 = first (CANDIDATE) reuses there, o =
VERIFIED reuses elsewhere, Delta = search path on repeats minus VERIFIED reuse (medians),
P = persistence CPU per stream: 0x5851: H1 36, c1 21, o 1, Delta 2,835,552, P 1,030,448;
0x5852: 41, 22, 0, 2,459,824, 772,640; 0x5853: 38, 23, 0, 2,370,688, 825,056.
At R every key hit at R = 1 is hit R times; only its first hit re-realizes, so VERIFIED
reuses are R H1 - c1 + o and the predicted per-stream saving of B over A is
(R H1 - c1 + o) Delta - P:
- Q1 (R = 1): 34.7 to 46.0 ms (calc:1fa5e098, calc:b60dbde4, calc:3fad38b0).
- Q2 (R = 3): 214.9 to 248.5 ms (calc:07855642, calc:9dba6127, calc:1361445c).
- Q3 (R = 6): 485.2 to 554.7 ms (calc:71339bb1, calc:f4794c6a, calc:961c6b03).
  Scored: streams inside the range, and whether the median d_B is inside.
- Q4 net(H) per key for B (#246 closed form, calibration fit of C, p, Delta, r):
  net(3) 2.48 to 2.75 ms (calc:5ca96f4f, calc:9e821d29, calc:5a9ec051); net(6) 6.19 to
  6.92 ms (calc:85bc325c, calc:05b69b47, calc:8d4345f7). Compared against the same fit on
  the held-out B lanes at R = 3 and R = 6.
- Q5 sign: d_B(s) > n(s) on 5 of 5 streams at R = 3 and R = 6 (B PASS); at R = 1 the
  predicted saving is of the order of the noise floor, no sign prediction.
- Q6 quality: B and C0 meet F1 and F2 on every held-out stream at every R.
- No numeric prediction is made for C0's cost: it differs from B only in search order on
  non-hit objectives, whose cost effect calibration did not resolve.
Disclosure: the harness-check runs on 0x5871 (R = 1, 3, 6; their stream totals were
printed, at load 60, unlocked) ran before this entry was committed. The prediction formula
and every input above come from the 0x5851..0x5853 evidence of 2026-10-19 and were not
changed after those check runs.
