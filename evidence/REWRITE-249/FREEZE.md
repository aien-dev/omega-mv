# REWRITE-249 FREEZE (written and committed before the run of record)

Campaign: aien-dev/aien-architecture-mv#249 E06, Wolfram-method extension. Ledger L059.
Frozen: 2026-10-19. Any later change goes in AMENDMENTS.md, never here.
Source: branch `research/rewrite-elim-249`; the commit that adds this file is the source of record.
Build of record: `quiet-build nice -n 10 make -j4 ... rw249 test-rw249`, binary digests in
`build-record/binaries.sha256`, test output in `build-record/test-of-record.txt`.

## Expression suite

| set | generator | n | sha256 |
|---|---|---|---|
| development (used while building and in tests) | `rwsearch gen --seed 1 --n 100` | 100 | 58fc24dfc1cd2bca80636717f9e6430c41f221c6e03fc5666e2ad94da935d90c |
| held-out (run of record only) | `rwsearch gen --seed 249 --n 240` | 240 | 4042740770fe15cc0be7812ddb63daf7a12a171656980fb72d766ab4224a6067 |

Held-out shape: lengths 1..6 steps (41, 50, 26, 47, 42, 34), widths u8 66, u16 63, u32 62, u64 49.
No expression appears in both sets. The generator's constant distribution was written after the rule
set by the same author (bias stated in the design note).

## Cost model blob

Calibration: `rwsearch calibrate --obs 16` (48-step single-class chains, 200000 calls per timing,
16 observations per arm, pinned to one CPU) under `spark-receipt rw-249-calibration -- quiet-measure
flock /tmp/aien-gb10.lock ...`, before this file was committed.
Blob: `calibration/model.blob`, rx_cm digest `bdba7882b7835aa73d2167c0f0f3c00bbbbbc7fbc569294176e9fd493c9cd581 (receipt calibration/2026-10-19-rw-249-calibration.json; MOVK observations hit the 1 ps floor: constant loading is off the dependent path)`.
Arm 7 is never observed on purpose. Every arm of the run of record uses this one blob.

## Budgets (same for every arm, per expression)

- distinct terms (nodes) at most 512; candidate evaluations at most 4096; thread CPU time at most 20 ms.
- Hitting any limit labels the result `best_found_under_budget`, never optimal.
- Arms: M (manual rules R01 R02 R05..R12, first applicable), G (all contracts, strict emitted-instruction
  decrease only), Bi (bounded exploration ranked by emitted instructions), B (bounded exploration ranked
  by the blob; unknown cost ineligible).
- Secondary, not of record: arm B at node budgets 16, 64, 256, 512, 2048, one repetition each.

## Repetitions and order

5 repetitions per arm, one receipt per arm: `spark-receipt rw-249-<arm> -- quiet-measure flock
/tmp/aien-gb10.lock tools/rw249/run_arm.sh <arm> 5 ...`, arm order M, G, Bi, B. The contract cache is
filled once before the arms (cold check cost recorded separately) and is warm in every repetition.

## Metrics (per arm, per repetition; `runs.csv`)

search CPU ns and wall ns; check CPU ns; peak resident bytes of search, check and execution; accepted
rewrites; failed candidates (rewrites the checker rejected); unchanged; removed operations (with their
derivation in `derivs.rwd` and an OEV1 `removed` record); best_found_under_budget; unranked; for every
accepted rewrite the per-call time of the original and of the rewrite and a null control (the original
timed twice), as minimum of three 100000-call timings each; a digest of the outputs, derivations and
verdicts.

## Frozen execution count and net saving

Each held-out expression is one objective executed E = 1000000 times. Net saving of an arm in one
repetition = E x (sum over accepted rewrites of measured per-call saving) minus (search CPU ns + check
CPU ns). Break-even E* = (search + check) / (sum of per-call savings). Arithmetic through aien-calc.
Execution savings use measured times only, never model predictions.

## Predeclared effects and verdict rules

- H1 (the hypothesis: bounded search finds reductions the manual rules missed). Count held-out
  expressions where arm B's accepted output has strictly fewer emitted instructions than arm M's
  accepted output and than arm G's. PASS if at least 5, INCONCLUSIVE if 1 to 4, FAIL if 0. Every counted
  case must be accepted by rwcheck in all 5 repetitions.
- Execution saving detectable (G3) for an arm if in every one of the 5 repetitions the sum of per-call
  savings exceeds the sum of null-control differences; otherwise INCONCLUSIVE.
- Net saving including search: PASS if positive at E in all 5 repetitions, FAIL if negative in all 5,
  otherwise INCONCLUSIVE.
- Determinism: PASS if the outputs digest is identical across the 5 repetitions of each arm.
- Mutants (in `build-record/test-of-record.txt`): M1 corrupted derivation step, M2 dropped assumption,
  M3 cache key without semantics version, M4 checker bypassed (end to end), M5 unsound axiom in the
  base, M6 semantic guard off, M7 derivation step skipped, M8 unknown cost counted as zero. Each must
  be RED with bad rows above zero.

## Instruments and units

CPU ns: `CLOCK_THREAD_CPUTIME_ID` (search, per expression) and `CLOCK_PROCESS_CPUTIME_ID` (check,
execution). Wall and execution ns: `CLOCK_MONOTONIC`. Peak bytes: `getrusage` `ru_maxrss`. Joules:
UNMEASURED per objective; the spark-receipt UPS figure covers the whole box at 10 W steps and is read
only for an arm whose run lasts 30 s or more, labelled whole-box.
