# LEARNING-SEARCH-584 r2: interleaved arms and a longer horizon (ledger L167)

Append-only. Protocol: `research/learning-search-584/FREEZE.md` plus `AMENDMENTS.md` A4
(design, verdict rule, predictions Q1 to Q6, committed 6d81ab9d before any run of record)
and A5 (review fixes, committed eb13ee97, also before the run of record). This is the
"Next discriminating experiment" of `../README.md`. The sealed seeds 0x5861..0x5865 were
not run and their evidence was not touched.

## Run of record
- date: 2026-10-20, batch 14:15:34Z to 14:16:40Z. host: Spark GB10 (spark-b87b), Linux
  aarch64, CPU only, no GPU used.
- head: source 18790827 (A5 harness), binary sha256 in `binary.sha256`, `commit.txt`.
  Build of record: `run-r2.sh build` (quiet-build, nice 10, -j4).
- command: `sh research/learning-search-584/run-r2.sh record`, which ran
  `quiet-measure flock /tmp/aien-gb10.lock spark-receipt ls584-r2 -- sh run-r2.sh record-inner`:
  15 processes `ls584 run --arms A,A2,B,C0 --seed 588x --reuse R` (R = 1, 3, 6; seeds
  0x5881..0x5885), all inside one hold of both locks.
- load and memory: the run waited for 1-minute load under 15, polling every 60 s; after
  the 90-minute cap (12:22:26Z to 13:52:28Z, 90 polls) load1 was 141.6 and the run went
  ahead as the brief requires. It then waited 23 minutes in the quiet-measure and gb10 lock
  queue (13:52:28Z to 14:15:34Z), during which load1 fell to 99.1. Batch start load1 99.08, MemAvailable 59,426,152 kB; end
  load1 97.24, 68,927,160 kB (`record/batch.txt`, `record/wait.txt`, per run
  `record/R*/load.txt`, and the header of `totals.csv`). The load target was NOT met.
- instrument: thread CPU time (CLOCK_THREAD_CPUTIME_ID) per objective inside each lane's
  own turn; stream totals are sums of those. Joules: UNMEASURED for the arms. The UPS
  receipt (`record/spark-receipt.json`) covers the whole box over 66 s: mean 242.6 W,
  15,968 J, incremental -488 J against a 250 W idle reading taken at load about 100, so
  it cannot separate this run from the other work on the box.
- controls (unmeasured, correctness only): `run-r2.sh controls` (stale for B and C0,
  nolearn for C0, every seed and R), `controls/`.
- CALC-EXEMPT for the tables: min / median / max are order statistics of the per-stream
  totals and differences in `summary.txt`, each difference receipted there; ms = ns / 1e6.
- result tables: `summary.txt` (`aggregate-r2.sh`), `totals.csv`; aien-calc receipts for
  every difference, noise value and comparison in `calc/` (`*.json` and `*.txt` with ids).

## Design in one paragraph
One process per stream runs four lanes on the same objectives, taking turns per
objective: A (no learning), A2 (a second copy of A: the same-code noise floor), B
(existing verified reuse) and C0 (B plus learned search order). The lane order follows a
balanced 4 x 4 Latin square, so every lane runs in every position equally often. A
throwaway lane solves the first objective once before the stream, so first-call costs
land on no lane. R multiplies each key's reuse opportunities: R = 1 is the sealed design
(about 1.1 per key), R = 3 and R = 6 repeat blocks 2 and 5 with fresh inputs (stream
lengths 240, 384, 600 objectives).

## Harness checks (`checks/checks.txt`: pass 67, fail 0; the record phase refuses to start otherwise)
- Same decisions as the sealed harness: the new binary on calibration seed 0x5851 makes
  the same decisions as the 2026-10-19 evidence for A, B, C0, C1 (0 differing rows each).
- Lanes are independent: on 0x5871..0x5873 at R = 1, 3, 6, every interleaved lane makes the
  same decisions as the same arm run alone (0 rows differ), and A2 the same as A.
- Disabled learning: C0 nolearn decision-identical to A at every seed and R (checks), and
  on every held-out stream (`controls/nolearn.txt`, 15 of 15 at 0).
- Horizon: every key's reuse opportunities at R = 3 and 6 are exactly R times those at R = 1
  (0 bad keys); the preface and blocks 1, 3, 4, 6, 7 are the same objectives (block, kind,
  key) at R = 3 and 6 as at R = 1 on all 3 calibration and 5 held-out seeds
  (`checks/blocks-same.txt`, 16 of 16 at 0; recorded after the run of record, from the
  stream generator only, no timing).
- Mutants, each watched red: M1 code-byte flip 238 bad rows (382 per lane interleaved);
  M2 lying table entry 55 (B) and 53 (C1); M3 journal tamper 1; M4 lane leak (A2 runs on
  B's state) 384 rows differ from A; M5 horizon generator draws fresh keys: 51 to 61 bad keys.
- The same checks under the pre-review A4 binary are kept in `checks-a4/`.
- claim_strength: proven_in_scope (decision identity on these seeds; timings not involved).

## Results (CPU ms per stream; min / median / max over the 5 held-out streams)

| R | A | A2 | B | C0 | A - B | A - C0 | noise abs(A - A2) |
|---|---|---|---|---|---|---|---|
| 1 | 725.9 / 733.5 / 773.0 | 730.2 / 735.0 / 780.0 | 675.6 / 684.3 / 747.5 | 671.5 / 688.8 / 729.2 | 25.5 / 41.6 / 55.5 | 37.1 / 43.8 / 62.0 | 1.4 to 7.0 |
| 3 | 971.6 / 1203.0 / 1251.9 | 972.1 / 1207.2 / 1260.3 | 762.5 / 958.1 / 1012.9 | 760.8 / 945.2 / 994.1 | 209.1 / 239.0 / 301.8 | 210.8 / 257.8 / 296.3 | 0.5 to 8.5 |
| 6 | 1435.9 / 1457.0 / 1813.3 | 1437.9 / 1457.6 / 1820.3 | 927.4 / 1025.6 / 1313.0 | 931.2 / 1000.0 / 1269.4 | 406.4 / 444.8 / 529.6 | 435.9 / 474.4 / 543.9 | 0.6 to 7.0 |

The per-stream values, with an aien-calc receipt for each, are in `summary.txt`. On every
stream at every R, both B and C0 beat A by more than that stream's A vs A2 difference.
Interleaving brought the same-code difference down to 0.042% (calc:46ad5bba, R = 6
seed 0x5885) to 0.91% (calc:2df97ec6, R = 1 seed 0x5881) of A's stream total; receipts in
`calc/noise_pct_*`, even at load 100. In the sealed run, separate
processes of the same code differed by 43%. Each n(s) is a
single same-code difference per stream, not a noise distribution. The protection against
chance is the 5-of-5 requirement.

Carry-over diagnostic (A5, not part of the verdict): the per-objective median of A - A2
by Latin-square row is between -11.3 and +5.3 microseconds at every R, against A's mean cost per objective
of 2.39 ms (calc:36b1d97b, R = 6 seed 0x5884) to 3.26 ms (calc:7654df7a, R = 3 seed 0x5884). Which lane ran just before has no visible effect at this
resolution. claim_strength: measured.

## Quality floors
- F1 (no realization committed unverified, no commit that disagrees with its examples):
  0 inconsistent commits, every lane, every stream. PASS.
- F2 (success at least 0.90 of objectives, wrong at most A + 1): holds for B and C0 on
  every stream at every R (`summary.txt`). PASS.
- F3 (stale control: 0 wrong rows from memory): C0 0 everywhere. B: 1 on seed 0x5884 at
  R = 1, 3 and 6 (block 5 recall, seq 203 / 299 / 443, 2 of 16 sealed inputs disagree);
  0 on the other 4 seeds. On that objective A and B (without the stale control) commit a
  wrong answer by search too, with the same 2 of 16 disagreement, and C0 succeeds only
  because its learned order finds a different consistent program. So the 12 examples do
  not pin the rule there (an identification failure the incumbent shares), and memory
  added no wrong answer that A did not also give. The frozen F3 counts it anyway, and
  the verdict follows the frozen rule.

## Verdicts (A4 rule; separate from "task done")

| arm | R = 1 | R = 3 | R = 6 |
|---|---|---|---|
| B | FAIL (F3, seed 0x5884) | FAIL (F3, seed 0x5884) | FAIL (F3, seed 0x5884) |
| C0 | PASS | PASS | PASS |

On cost alone, B clears the noise on 5 of 5 streams at every R; it fails only on F3. This
replaces the sealed run's B INCONCLUSIVE and C0 FAIL with a measurement that controls run
order. The sealed verdicts stand for the sealed protocol: different seeds, separate
processes, and C0 there had a different F3 event. claim_strength: measured, at load
about 100, one host, one task family. A PASS means only that the bounded hypothesis
passed on this suite, and it carries no general-transfer claim.

## Predictions (A4, written before the run; nothing adjusted)
- Q1, saving of B over A at R = 1, 34.7 to 46.0 ms: 1 of 5 streams inside, median 41.6
  inside. Partial.
- Q2, R = 3, 214.9 to 248.5 ms: 2 of 5 inside, median 239.0 inside. Partial.
- Q3, R = 6, 485.2 to 554.7 ms: 2 of 5 inside, median 444.8 below the range. FAIL. The
  count model holds roughly (VERIFIED reuses 193 to 234 at R = 6; `totals.csv`), but
  Delta fitted on the R = 6 lanes is 2.05 to 2.75 ms against 2.37 to 2.84 in calibration,
  so each reuse saved less than predicted.
- Q4, net(H) per key for B: predicted net(3) 2.48 to 2.75 ms, net(6) 6.19 to 6.92 ms.
  Fitted on the held-out lanes: R = 3 fits give net(3) 2.36 to 3.13 (0 of 5 inside) and
  net(6) 5.89 to 7.93 (0 of 5); R = 6 fits give net(3) 2.11 to 2.80 (0 of 5) and net(6)
  5.25 to 6.91 (1 of 5). FAIL: the ranges were too narrow (three calibration seeds, no
  spread term). Break-even: net(1) lies between -0.10 and +0.15 ms on every fit, so H*
  is 1 on 5 fits and 2 on 5 (`netfit-B.txt`, `ls584 neth` via netfit.sh); net(2) is +1.06 to +1.60 ms.
  The first applicable use still saves about nothing (it re-realizes), as in the sealed run.
- Q5, d_B > n on 5 of 5 at R = 3 and 6: held on cost, 5 of 5 at both. The "B PASS" it
  implied did not happen, because of F3.
- Q6, B and C0 meet F1 and F2 everywhere: PASS.

## Limits and UNVERIFIED
- The load target (under 15) was not met. The run went ahead at load1 97.2 to 101.6 (`totals.csv`) after the
  90-minute cap. Interleaving makes the comparison fair inside each process, but the
  absolute milliseconds would be different on a quiet machine.
- UNVERIFIED: that the Latin square removes all order effects; the carry-over diagnostic
  shows none at microsecond resolution.
- UNVERIFIED: that repeating blocks 2 and 5 right after their first pass is the right
  reading of "longer horizon" (A4 item 2).
- After a restart, B's cut and uncut runs differ only in the state_bytes column (the
  earlier binary behaves the same): CANDIDATE realizations are kept in memory only, by
  design. Restart is not part of r2.
- C's ordering counts are still not persisted (A2 of the sealed run; open).
