# REWRITE-249: checked elimination ranked by cost (E06 Wolfram-method extension)

Issue aien-dev/aien-architecture-mv#249 (E06), parent #276, feeds omega-mv#583's removed-work ledger.
Ledger L059. Design: `docs/research/249-checked-elimination.md`. Frozen plan: `FREEZE.md` (commit
de779dff, 2026-10-19T21:07:59Z, before the first measured run at 21:14:33Z). Later changes: `AMENDMENTS.md`.

## Answer in one paragraph

Bounded cost-guided search (arm B) found reductions the manual rules missed: on 18 of 240 held-out
expressions its checked output emits fewer instructions than both the manual rules (M) and greedy
instruction-count rewriting (G), against a predeclared threshold of 5. Every accepted rewrite of every
arm was proof-checked and none failed. But at the frozen 1 000 000 executions per expression, no arm
pays for itself. The cost of checking (an exhaustive or sampled comparison of old and new program through
the omega evaluator, about 1.2 to 1.5 s per arm per repetition) is far larger than the execution time
saved, so net saving including search and check is negative for every arm in every repetition.

## Runs (all on host spark-b87b, GB10, single pinned CPU thread)

| run | date (UTC) | head | command | load 1/5/15 start, end | MemAvailable start, end (bytes) | instrument | result | claim_strength | verdict |
|---|---|---|---|---|---|---|---|---|---|
| build and test of record | 2026-10-19 | 09083bd4 | `quiet-build nice -n 10 make -j4 rw249 test-rw249` | in `build-record/` | in `build-record/` | test script exit code | all PASS, M1..M8 RED | measured | PASS |
| calibration | 2026-10-19T21:04:44Z | 0969fb83 | `spark-receipt rw-249-calibration -- quiet-measure flock /tmp/aien-gb10.lock rwsearch calibrate --obs 16` | 10.52/14.62/24.20, 5.16/10.43/20.90 | 25864179712, 27086602240 | CLOCK_MONOTONIC per 200000 calls | blob digest bdba7882...581 | measured | n/a |
| cold contract check | 21:14:33Z | de779dff | `spark-receipt rw-249-coldcheck -- quiet-measure flock ... run_record.sh cold` | 13.25/15.51/20.10, 11.98/15.16/19.94 | 24731017216, 25871003648 | CLOCK_PROCESS_CPUTIME_ID | 25 proven_in_scope, 0 probe_agreement, 0 rejected, 11 243 839 344 ns CPU | proven_in_scope | PASS |
| arm M x5 | 21:16:44Z | de779dff | `spark-receipt rw-249-M -- quiet-measure flock ... run_arm.sh M 5 ...` | 20.50/15.58/19.39, 18.91/15.39/19.29 | 8936169472, 16522342400 | see FREEZE | table below | measured | see verdicts |
| arm G x5 | 21:20:50Z | de779dff | same, arm G | 15.18/15.28/18.43, 13.14/14.82/18.23 | 9690288128, 9316634624 | see FREEZE | table below | measured | see verdicts |
| arm Bi x5 | 21:23:42Z | de779dff | same, arm Bi | 10.81/12.56/16.80, 9.55/12.15/16.57 | 20720414720, 19729973248 | see FREEZE | table below | measured | see verdicts |
| arm B x5 | 21:26:30Z | de779dff | same, arm B | 16.80/13.11/16.18, 16.43/13.15/16.16 | 18156785664, 18281381888 | see FREEZE | table below | measured | see verdicts |
| secondary (not of record) | 21:31:01Z | de779dff | `spark-receipt rw-249-secondary -- quiet-measure flock ... run_secondary.sh` | 20.11/14.48/15.76, same | 12452139008, 15335653376 | thread CPU | node sweep, matching profile | measured | n/a |

Receipts: `receipts/*.json`. Driver: `runs/run_record.sh`. Per-arm data: `runs/<arm>/runs.csv`
(rep1 kept in full, rep2..rep5 summaries); full record dirs were in `~/workspace/targets/omega-rw-249/record/`.

Deviations, said out loud:
- The box was busy throughout (load average 10 to 20 from other work; the calibration receipt shows the
  GPU about 94 % busy and up to 4 lock holders). These are single-thread CPU measurements on a pinned
  core, but other work shares memory bandwidth and caches.
- MemAvailable was below the brief's 12 GiB floor at the start of arms M (8.9 GB) and G (9.7 GB, and
  9.3 GB at its end). This run used about 10 MB at peak; the memory was held by other processes, which
  this worker may not stop. Recorded, not hidden.
- Execution timings are bimodal: execution CPU per repetition is either about 0.35 to 0.45 s or about
  1.9 to 2.8 s, and the per-call saving moves with it (example: Bi rep1 sum of savings 738400.4 ps
  against 103847.8 to 146581.7 ps in the other reps). The likely cause is which core type the pinned
  CPU landed on, or contention. UNVERIFIED.

## Per arm (held-out set, 240 expressions, 5 repetitions)

| arm | changed (accepted) | failed candidates | removed ops | best_found_under_budget | search CPU ns per rep | check CPU ns per rep | search, check, exec peak bytes | outputs digest (all 5 reps) |
|---|---|---|---|---|---|---|---|---|
| M manual | 135 | 0 | 196 | 0 | 148672 to 277040 | 1176463936 to 1278392560 | 1.81e6 to 1.89e6, 9.82e6 to 1.00e7, 1.63e6 to 1.70e6 | c3cd9da6bb950535 |
| G greedy | 171 | 0 | 351 | 0 | 387808 to 703568 | 1260168160 to 1490342848 | 1.83e6 to 1.90e6, 9.82e6 to 1.00e7, 1.63e6 to 1.70e6 | 7ca62a89887c25a6 |
| Bi bounded, instruction count | 175 | 0 | 374 | 2 | 3578352 to 6450080 | 1193444880 to 1274501472 | 1.83e6 to 1.90e6, 9.89e6 to 1.01e7, 1.63e6 to 1.70e6 | a34ac6f8ecda2eba |
| B bounded, rx_costmodel | 186 | 0 | 374 | 2 | 11357696 to 19830816 | 1315824256 to 1438202640 | 1.83e6 to 1.89e6, 9.82e6 to 1.01e7, 1.63e6 to 1.70e6 | c63551f1d2f2f5c8 |

Unranked (input cost unknown) was 0 in every arm. Wall ns per rep is in `runs.csv`.
Cold check of the 25 contracts (once, cache then warm): 11 243 839 344 ns CPU.

Cost per accepted objective, search plus check, in the median-break-even repetition:
M 9067923.4 ns (calc:3c2120eb32675c44b9123996cd3401c6592e401941244035674ff18826d0e72e),
G 7486354.0 ns (calc:4789cb4e573b15c61a293f64f2ef290b6a5b4cb55eba170a62ea12fb76ea6d4b),
Bi 6840132.8 ns (calc:00a920bfdf4c78e6c53bd712e4a1af5176fa1bae6a6556e64a125dc780480771),
B 7135386.8 ns (calc:867f024524b83d5d4aa17ddce3c46aa5f804361c8e02c74bf17ff1b7fe740ad8).
Previous run of the same figure: none (first run). Physical bound: the fastest dependent step measured in
calibration is 252.6 ps (ADD, AND), which is one step per cycle at 3.95883 GHz
(calc:167111bb4012b99b488f5da19c5c405eaef2b1c7390235df0f16c313cfd51250); that the core clocks at that
rate is inferred, not read from a manual (UNVERIFIED). The checker cost per objective is over a million
times that step, so the checker, not the machine, is what limits the method here.
Joules: UNMEASURED. Each arm ran 10 to 16 s between its start and end stamps, under the 30 s the
FREEZE requires before the whole-box UPS figure may be read.

## Verdicts, per #249 acceptance item

| item | verdict | numbers |
|---|---|---|
| Freeze budgets and held-out cases | PASS | FREEZE committed 21:07:59Z, first measured run 21:14:33Z; held-out set sha 40427407...6067 used only in the run of record |
| Invalid assumptions and mutants rejected | PASS | negatives N01..N07 all rejected (N01 isqrt without sign, N02 isqrt in u32, N03 (x*2)/2 in u32, N04 f32 reassociation, N05 probe-agreeing masks, N06 dropped assumption, N07 wrong axiom); mutants M1..M8 RED; A1 replay mutant RED |
| Independent derivation checker | PASS in scope | `rwcheck` is a separate program with its own parser and evaluators; it checks every step against the axiom base and compares old and new program directly, cross-checked against omega's own evaluator. Same author as the search, so G6 (independent reproduction) is still owed: ledger L061 |
| Same accepted outputs and contracts | PASS | outputs digest identical across 5 reps in every arm; checker accepted every candidate in every rep (M 135, G 171, Bi 175, B 186; rejected 0, bad rows 0) |
| Total search, check and execution cost and memory measured, not expression size alone | PASS (measured) | table above; savings from measured execution only, never from the model |
| Net saving including search (G4) | FAIL | even the best case (largest saving of any rep against the cheapest search plus check of any rep) is negative at E = 10^6: M -1100573408 ns (calc:e3f69ee3365b2fd2df1d0d8cbad88ddfa9ee493a324d103495b431d857117087), G -1084998444 ns (calc:803102f5b91400b96363faa6711ad0341138081c9506bc89746b155997675ce3), Bi -458622832 ns (calc:4a21387fd7eb0f835ee452d01b620549162f3897e34e2ee20107cb6cb932a2f4), B -1132357752 ns (calc:36486fccef034dd92898342dfe0a37876f678f5eb6cb45c4e4b8b59040df3b7e) |
| Break-even executions per expression set (median rep) | reported | M 16099191.8 (rep4, calc:2c3b8dee41359c0a65fa60cd2e923a247234479b961a61368a27c3bd71deaa30), G 9568012.1 (rep3, calc:91935f6e22abcf461312b165d019c59b5d90369808d5aa7864d4a36a33e21093), Bi 11083538.2 (rep3, calc:0e489b962080b5ddc9696c05a44341c36158fdf8760308af6c0c93045acd0d32), B 9237769.0 (rep5, calc:a76b9a723feda68f809d914ca063155bd6dc70b4ab700b5ec84adc193ac223f9) |
| Cache under assumptions, invalidated on changed semantics | PASS | key = SHA256 of id, assumption text, semantics version and claim; test shows a changed semantics version misses; M3 (key without version) RED |
| Timeout means best found under budget | PASS | 2 expressions in Bi and 2 in B hit the 512-node budget; labelled best_found_under_budget, never optimal |
| Preserve E04 dependency and existing gates | PASS with condition | E04 (#247) is OPEN and NOT_RUN, so every verdict here is conditional on it; no forbidden path or `src/` file was changed (diff against base a8ffd2b9 checked) |

Predeclared effects (FREEZE):

| effect | verdict | numbers |
|---|---|---|
| H1: B fewer emitted instructions than both M and G | PASS (threshold 5) | 18 expressions, each accepted in all 5 reps: S249-0011 (M 9, G 8, B 7), 0016 (13, 9, 7), 0049 (9, 9, 7), 0059 (11, 9, 7), 0071 (15, 11, 7), 0077 (14, 12, 10), 0080 (11, 11, 7), 0081 (16, 13, 8), 0137 (15, 14, 13), 0175 (12, 12, 8), 0194 (13, 13, 11), 0200 (16, 16, 15), 0204 (9, 9, 7), 0206 (13, 11, 6), 0212 (7, 7, 5), 0216 (11, 9, 7), 0230 (11, 11, 9), 0239 (9, 9, 7). M's output is its unchanged input where M found nothing. B never emitted more instructions than G |
| G3 execution saving detectable | M INCONCLUSIVE; G, Bi, B PASS | M rep3: sum of savings 9160.4 ps below the null control 19525.4 ps. G, Bi, B: sum of savings above the null control in all 5 reps |
| Determinism | PASS | digests above |
| Mutants M1..M8 | PASS (all RED) | `build-record/test-of-record.txt`; re-run with the A1 binary also all RED |

Gates (plan section 9): G0 PASS (pins, hashes, blob digest, sets); G1 measured but on a busy box (see
deviations); G2 PASS; G3 per arm above; G4 FAIL; G5 not tested beyond H1 on held-out cases (the model's
predicted cost was not regressed against measured per-call time), INCONCLUSIVE; G6 owed (L061).

## Other findings

- Example of a reduction the manual rules missed: S249-0206 at u32, `and 0x0; mul 0x0; and 0xfff;
  and 0x7fffffff; mul 0xffffffff`. B reached `and 0x0` (rules R16, R17, R17, R17, R12), checker accepted;
  measured 3577.8 ps per call before, 1450.2 ps after, null control 2.5 ps.
- Bi (bounded, instruction count) also beat G on 18 expressions. B's predicted cost was below Bi's, with a
  different output, on 26 expressions.
- Probe agreement is not proof (`probe/probe-analysis.txt`, counterexamples CE01..CE04 in
  `tests/research/rw249/counterexamples/`, for example u8 `add 0x0` against `and 0x2f`, which agree on all
  six probe inputs and differ at x = 16). In the u8 universe of 1- and 2-step programs: 6480 programs,
  2762 distinct functions, 2230 probe groups, 226 groups mixing different functions, 939 programs
  misgrouped, 0 true equivalences split. Completeness loss: omega's pruning dropped 3886 programs and with
  them 496 functions (its table also hit its 1024 capacity). Held-out: 240 programs, 176 functions,
  167 groups, 6 mixed, 11 misgrouped, 12 functions lost.
- Indexing (deliverable 5) not built. Doubling every match moved arm B's search from a median of about
  19.58e6 ns to about 22.24e6 ns over 5 noisy pairs, so matching is a small share of search, and search
  is itself under 2 % of search plus check. Rule: build only if matching dominates; it does not.
- Node budget sweep for B (secondary, one rep each): 16 nodes 296 removed ops (74 budget hits), 64: 364
  (26), 256: 373 (4), 512: 374 (2), 2048: 374 (2).

## OEV1 records

`oev/<arm>/<expression>.oev`: one file per accepted rewrite that removed operations (M 135, G 164,
Bi 169, B 169), each `removed op=... reason=eliminated proof=rwcheck:<derivs.rwd>#<expression>`.
Regenerated under amendment A1; dagsim (omega-research-583 @ d8c74201) accepts 637 of 637.

## MISSING PREREQUISITES

- rx_costmodel has no scalar operation class (RX_CM_OPS = 1, matvec only). Its 8 arms are used here as a
  labelled stand-in for ADD, SUB, MUL, AND, OR, MOVK, BASE and one deliberately unobserved class. A real
  scalar class needs a change under `src/runtime/`, which this campaign may not edit.
- Observation, not changed: omega's probe signature hashes outputs only (not the width), and its
  equivalence table is capped at 1024 entries and stops adding silently when full
  (`src/omega_synthesis.c` at a8ffd2b9).

## UNVERIFIED

- Cause of the bimodal execution timings (core type or contention).
- That 252.6 ps is one clock cycle of the core.
- That replay under a 20 ms CPU-time limit is deterministic in general; it matched in every case here
  and the account step refuses otherwise.
- The checker's sampled cases (u16 and wider, int and f32 domains) are sampled, not exhaustive; only w = 8
  with at most two parameters is exhaustive.
- The generator's constant distribution was written by the rule author after the rules (bias).
