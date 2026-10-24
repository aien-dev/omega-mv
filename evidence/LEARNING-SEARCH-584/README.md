# LEARNING-SEARCH-584: bounded learning-mechanism search (omega-mv#584, ledger L048)

Append-only campaign folder. Protocol: `research/learning-search-584/FREEZE.md` (frozen
before implementation, refined before the first calibration run; later changes only in
`research/learning-search-584/AMENDMENTS.md`). Selection and predictions written before the
sealed run: `SELECTION.md`. Harness: `tests/research/ls584/ls584.c` (`make ls584`,
`mk/ls584.mk`), scripts in `research/learning-search-584/`.

- date: 2026-10-19. host: Spark GB10 (spark-b87b), Linux aarch64, CPU only, no GPU used.
- head: branch research/learning-search-584 (commit in `commit.txt`; binary sha256 in
  `binary.sha256`; build note in `build-note.txt`).
- commands: `sh research/learning-search-584/run.sh build|dev|calib|controls|mutants|restart|sealed`.
  Each run dir holds `rows.csv` (one row per objective), `summary.json`, `stdout.txt`,
  `load.txt` (load1 and MemAvailable at start and end, and whether the lock was held).
- instrument: CPU thread time (CLOCK_THREAD_CPUTIME_ID) and wall time per objective.
  Joules: UNMEASURED (no per-process instrument; the UPS wall meter sees the whole box
  at 10 W resolution and these runs last about 1 s each). Load during the measured
  batches: calibration load1 10.6 to 11.1; sealed load1 50.6 to 53.7 (the box was much
  busier; see "noise" below).
- claim_strength per section is given inline. Verdicts: PASS / FAIL / INCONCLUSIVE /
  NOT_RUN, separately from "task done".

## What was compared (FREEZE.md sections 1 to 4)
Task: bounded arithmetic transformations in the Omega V0 realizable subset (ADD SUB MUL
AND OR with constants, width 32), the Phase 0 family 1 of architecture #264. Every
committed answer is a program realized through the existing `omega_synthesize_for_dgx_spark`
path (synthesis, triple identity, 84-input native differential against the semantic
evaluator) and executed natively on the objective's examples. 240 objectives per stream
in seven blocks (first exposure, repeated, novel bindings, compositions, delayed recall
after interference, changed rules, correction), plus 8 malformed commands for the adapter.
Arms: A no learning (full canonical enumeration every time), B existing verified reuse
(table of 32, CANDIDATE/VERIFIED/STALE lifecycle as in `rx_plan.h`, VERIFIED hits reuse
the identity-bound realization), C0 = B + learned enumeration order, C1 = C0 + composition
of retained rules. Persistence through the canonical Cortex store (`rx_cortex.h`):
candidates as CX_K_CANDIDATE, promotions with evidence records carrying the realization.

## Results of record (sealed seeds 0x5861..0x5865; `sealed/`, `sealed-controls/`)

Per stream totals (CPU ms, success and wrong of 240), from `aggregate.sh sealed/*`:

| seed | A cpu / succ / wrong | B | C0 (selected) | C1 (unselected, reported) |
|---|---|---|---|---|
| 5861 | 572.0 / 226 / 5 | 541.6 / 226 / 5 | 551.9 / 228 / 3 | 611.5 / 230 / 3 |
| 5862 | 673.1 / 231 / 8 | 643.5 / 231 / 8 | 742.1 / 231 / 8 | 810.4 / 231 / 8 |
| 5863 | 617.7 / 230 / 0 | 591.8 / 230 / 0 | 711.3 / 230 / 0 | 631.9 / 232 / 0 |
| 5864 | 690.5 / 232 / 3 | 546.0 / 232 / 3 | 715.0 / 232 / 3 | 683.3 / 232 / 3 |
| 5865 | 755.0 / 231 / 1 | 693.8 / 231 / 1 | 674.8 / 232 / 0 | 615.8 / 235 / 0 |

Within-arm mechanism numbers for C0 (`netfit.sh sealed/A-s sealed/C0-s`), per seed:

| seed | verified reuse rows (median cpu_ns) | search path on repeats (median cpu_ns) | Delta ns | p | r | C ns | net(1) ns | net(2) ns | H* |
|---|---|---|---|---|---|---|---|---|---|
| 5861 | 21 (219,024) | 2,394,992 | 2,175,968 | 0.528 | 0.9956 | 1,177,629 | -29,153 | +1,114,232 | 2 |
| 5862 | 11 (266,176) | 3,155,248 | 2,889,072 | 0.472 | 0.9937 | 1,388,732 | -24,512 | +1,331,160 | 2 |
| 5863 | 11 (251,488) | 2,754,576 | 2,503,088 | 0.403 | 0.9930 | 1,033,697 | -25,453 | +975,715 | 2 |
| 5864 | 17 (264,192) | 2,975,088 | 2,710,896 | 0.569 | 0.9974 | 1,570,356 | -26,772 | +1,512,810 | 2 |
| 5865 | 20 (235,120) | 2,717,440 | 2,482,320 | 0.514 | 0.9974 | 1,287,935 | -12,271 | +1,260,080 | 2 |

net(H) = p*Delta*(1-r^H)/(1-r) - C (architecture #246 extension); C = persistence per key
+ p*(candidate first-reuse cost - verified reuse cost), because the first applicable use
of a CANDIDATE re-realizes (reference-first) and saves nothing. Closed form checked against
the direct finite sum by the harness (`ls584 neth`, max abs diff < 1e-7 ns) and by
aien-calc: `calc/net2_closed_5861.json` and `calc/net2_direct_5861.json` both
1,114,231.627 ns; `calc/net1_5861.json` -29,153 ns; no-payoff bound p*Delta/(1-r) =
259,074,196 ns (`calc/nopayoff_bound_5861.json`), far above C, so the no-payoff region
on this suite is not "C too large" but "fewer than 2 applicable uses per key" and "key
never retained" (table full: refused_full 138 to 152 per sealed stream).
Mean Delta over sealed seeds 2,552,269 ns (`calc/delta_median_sealed.json`); mean p 0.497
(`calc/p_mean_sealed.json`). claim_strength: measured.

Stream-level difference A minus C0: +20.0, -69.1, -93.7, -24.5, +80.2 ms; mean -17.4 ms
(`calc/sealed_diff_mean_A_minus_C0_ns.json`). A minus B: +30.4, +29.6, +25.9, +144.5,
+61.2 ms; mean +58.3 ms (`calc/sealed_diff_mean_A_minus_B_ns.json`). Predicted saving
from the mechanism (16 verified reuses * 2.35 ms - persistence) was 36.9 ms
(`calc/predicted_saving_per_stream_ns.json`).

Noise: the same code path (block-1 first solves, identical in every arm) had medians
from 2.25 to 3.21 ms across sealed runs, a 43% spread, with load1 50 to 54. Arms ran in
the order A, B, C0, C1 per seed, so order and load drift confound cross-process totals.
The within-arm numbers (same process, same minute) do not have this problem.
claim_strength for stream-level totals: unresolved.

## Prediction scoring (SELECTION.md, #262 rule; nothing adjusted after the fact)
- P1 verified reuse rows 15 to 19: observed 21, 11, 11, 17, 20: 2 of 5 inside. FAIL (range
  too narrow; p varies more across streams than calibration showed). Median cost of those
  rows 0.22 to 0.26 ms: 0.219, 0.266, 0.251, 0.264, 0.235: 4 of 5.
- P2 search path 2.3 to 3.1 ms: 2.39, 3.16, 2.75, 2.98, 2.72: 4 of 5. Delta 2.1 to 2.6 ms:
  2.18, 2.89, 2.50, 2.71, 2.48: 3 of 5 (the sealed batch ran at 5x the calibration load).
- P3 p 0.46 to 0.60: 0.528, 0.472, 0.403, 0.569, 0.514: 4 of 5. r 0.995 to 0.9985:
  0.9956, 0.9937, 0.9930, 0.9974, 0.9974: 3 of 5. Capacity, not forgetting, remains the
  limit (refused_full 138 to 152 per stream).
- P4 net(1) within +-0.05 ms of 0: 5 of 5. net(2) 1.0 to 1.5 ms: 3 of 5 (0.98, 1.51 just
  outside). H* = 2: 5 of 5. PASS.
- P5 sign of (A - C0) positive in at most 4 of 5: positive in 2 of 5. PASS (the
  structural claim: stream totals cannot separate the arms at this effect size and noise).
- P6 success >= 216 everywhere: PASS. C0 wrong <= A wrong + 1 on every stream: 3/6, 8/9,
  0/1, 3/4, 0/2: PASS. Inconsistent commits 0 everywhere: PASS.
- P7 shuffle success <= 3: C0 gave 3, 2, 0, 3, 5: 4 of 5 (seed 0x5865 gave 5). Equal
  counts across arms under shuffle were verified on calibration only (A, B, C0, C1
  identical on all three seeds); A and B shuffle were not run on the sealed seeds.
  nolearn decision-identical to A: 5 of 5 PASS. stale: wrong rows from memory 0, 1, 0, 0, 0:
  one violation (seed 0x5862, block 7, a retained program consistent with the 5 evidence
  points and wrong on 1 of 16 sealed inputs; A solved that objective by search in two
  attempts). unseen: compose rows 0 everywhere PASS.
- P8 C1 compositions 2 to 4 more successes than A: +2, 0, +2, 0, +3; failed searches
  -2, 0, -2, 0, -3: 3 of 5 (two seeds had no composable pair retained).

## Acceptance boxes (#584)
- Candidate grammar and lowering coverage published; unknown ops rejected before
  execution: PASS (FREEZE.md sections 2 and 3; every action goes through
  `omega_program_realize_check`; the harness grammar emits only the five V0 ops).
- Baselines with matched exposure and budgets; all failed searches counted; shuffled,
  disabled-learning, stale/wrong-memory and unseen-composition controls: PASS as run
  (`controls/`, `sealed-controls/`; failed searches in every summary).
- Semantic correctness separate from usefulness, with mutants; sampled tests labelled:
  PASS. Mutants (`mutants/`): M1 code-byte flip caught 229 of 229 realizations (bad rows
  229); M2 lying table entry refused 54 (B) and 56 (C1) times (bad rows > 0); M3 journal
  word tamper: cx_open refused, fallback to empty state (bad rows 1). The sealed check is
  labelled SAMPLED in every row.
- Stored candidate reloads with bound identities, recovery/refusal, safe fallback:
  PASS for the table and realizations (`restart/`: 32 entries reloaded, 16 realizations
  rebound by recomputed triple id, 0 unbound; B cut-vs-uncut rows identical). GAP (A2):
  C's ordering counts are not persisted, so a cut C1 run differs from the uncut run on
  53 row cells.
- Quality floors and retention intervals precommitted; all arms and failures published
  with #262 results: PASS (this file).
- Break-even horizon and no-payoff regions via the E03 extension: PASS (table above):
  H* = 2 applicable uses per key on every sealed stream; no payoff for keys with fewer
  than 2 applicable uses or never retained.
- Independent reviewer reproduction and #263 dossier: NOT_RUN (not this agent's role;
  pinned commands and artifacts are in this folder).

## Verdict of the bounded hypothesis
Does a finite search over {A, B, C0, C1} find a better lifecycle tradeoff than the
incumbent on this suite under the frozen floors?
- C0 (the variant the frozen rule selected): FAIL. Quality: F1 and F2 pass; F3 has one
  wrong-from-memory commit on 1 of 5 sealed streams. Cost: no detectable stream-level
  saving (mean -17 ms, sign inconsistent), as predicted (P5), because the realization
  dominates every objective (about 2.5 ms, mostly the 84-input native differential
  whose every call is an mmap/mprotect/munmap) and the stream offers about 1.1 reuse
  opportunities per key while payback needs 2.
- B (existing verified reuse, no learning beyond the cache): INCONCLUSIVE. Lower totals
  than A on 5 of 5 sealed streams (mean 58 ms, 4 to 21%), but B always ran right after A
  at load 50, and the same-code spread is 43%. The within-arm numbers say the mechanism
  is real (verified reuse 0.22 to 0.27 ms vs 2.4 to 3.2 ms) and the horizon is the limit.
- C1 (unselected): reported only. Compositions give +2 to +3 successes on 3 of 5 seeds.
No general-transfer or intelligence claim follows from any of this. A faster lookup on a
toy arithmetic family is not evidence of either.

## Cost screening through #583 (`oev/`)
OEV1 fixtures for A, B, C0, C1 from calibration seed 0x5852, evaluated by `dagsim` built
from research/cost-sim-583@eb35fe11 in a detached worktree (their tree unedited). All
four: status OK, peak live bytes 0 to 7,894 against 137 GB, critical path 13.5 ms for
one objective of each block. The screen rejects nothing at this scale; its value here was
the provenance rule: the first fixture declared the storage bandwidth `prov=unknown` and
was refused (REFUSED_UNKNOWN) until the claim was removed.

## Communication adapter
Bounded text commands (SOLVE/RECALL/COMPOSE/CORRECT with hex examples) are parsed into
the five PL_OT_GOAL fields of `rx_plan.h`; the ALLEN interface files are untouched.
8 malformed or ambiguous commands per stream: 8 of 8 answered with a clarification and
never committed, in every run. Adapter cost: 0.5 to 2.5 us CPU and 29 to 234 bytes per
command, reported in `rows.csv` (adapter_cpu_ns, adapter_bytes). No language model was
used: model cost 0.

## Next discriminating experiment
Interleave A and B per objective in one process (or alternate run order and repeat 5x)
under the quiet lock at load below 15, so the predicted 37 ms per-stream saving (5 to 7%)
can be separated from run-order and load drift. Then lengthen the horizon to 3 or more
reuse opportunities per key, where net(H) predicts +1.0 to +1.5 ms per retained key per
extra use. Persist C's ordering counts through Cortex before any further C variant.
