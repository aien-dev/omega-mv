# REUSE-1 v1.0 clarifications

Rulings that resolve ambiguities in `REUSE-1.md` v1.0 without changing any rule there. All of
them were made before any experience collection or measurement; each names who raised it.

K1 (CandLib, EvalJudge; 2026-10-25). Calibration location. `drive build` copies CALIB into the
library as `gen-G.cal` (written before `gen-G.man`, same ADR 0015 order, counted in LIBDIR bytes);
manifest words 52..55 hold its SHA-256 (0, and no file, when CALIB is `-`); `lbload` verifies it
before exec; a loaded router reads its calibration from `LIBDIR/gen-G.cal`. The evaluator passes no
extra argument. An arm started with LIBDIR `-` has no calibration (the fixed rule of 5.2).

K2 (CandCore; relayed to the evaluator). Framing order and statuses of section 2:
BATCH item DEL with b != 0 is 3; BATCH with p = 1 + 3m and m outside 1..1024 is 3, p != 1 + 3m is
4. BEGIN: p < 65 or p != 65 + n is 4; n > 4096 is 5; invalid PROG 2; a value outside the domain 3;
a refused BEGIN keeps the previous session. Ops 2..8 with no session are 6 (after the framing check
4, before 2 and 3). STATS and END without a session are 0. EOF on stdin without END: the arm exits
3 without a response. The echo arm validates and applies every request like full; only its QUERY
answer is 0, 0, 0. PROGRAM: invalid PROG is 2 (checked first), then out of domain 3.

K3 (CandLib). Template admission and manifest order (5.3, 3.4): the FSM template is decided first;
AGG shapes are admitted in descending TRAIN QUERY count (ties lower shape id) while count >= 8 and
K < 16; library order is AGG in admission order, then FSM. Lineage digests are sorted ascending.
A duplicate experience file or more than 8 is a usage error (exit 2). The self-check is FNV-1a 64
(offset basis 14695981039346656037, prime 1099511628211) over the 2040 little-endian bytes of
words 0..254.

K4 (CandSelect). Bind rule cells (5.2): bind, full and method are all read at change class 0; any
of the three missing gives the fixed rule. A calibration file whose words 3..7 are nonzero, or whose
size is not exactly 1600 bytes, is malformed and refused whole.

K5 (EvalJudge). The evaluator vendors a byte-identical copy of omega-mv main `acb3f15c`
`om/sha256/sha256.om` as spark-rsi-mv `om/reuse-eval/sha256.om` with a pin file.

KAT (integrator). `tests/reuse/kat.txt` (626 records, sha256
`31c153e1881e3a842cb838448ac53b5331c8e99300023752ac0ae3522b16b7cb`) was produced outside Omega:
inputs and answers by a Python script, then every answer recomputed independently in Wolfram
Language (Wolfram MCP kernel, 2026-10-25): `{626, 0, {}}` = 626 records, 0 disagreements. Record
format: REUSE-1 integrator decision 4 (kinds 1 lane step, 2 fold, 3 AGG program, 4 FSM program).
The generating script is integrator tooling, not part of the delivered closure; the file is data.

K6 (EvalStats). FINAL seed use: bytes 0..7 (LE) seed the bootstrap (section 10 as written);
bytes 8..15 seed the FINAL generator; bytes 16..23 seed the arm-order permutation.

K7 (EvalStats, EvalJudge). Control outcomes are recorded one per control and sub-case as detected,
undetected, reported (C12 ablations only) or not run, with a flag when a tampered or revoked
artifact answered a QUERY as a method. Undetected or flagged: STOP. Not run: GO is impossible
(INVESTIGATE at best); a control is never silently passed.

K8 (EvalGen). Workload details of 7.1 and 7.2: FSM type C sessions are 4 new machines (FSM has one
shape); of the 10 TRAIN machines 5 are general and 5 are permutation machines, FSM type A recall
uses the permutation ones and type A new uses new permutation machines; AGG type A programs of all
three classes have f in {MIN, MAX} (the generator fails loudly if the TRAIN pool has no such
shape); type I starts with DEL and alternates; type P puts a PROGRAM before QUERY 4, 8, 12 and 16
(AGG: same shape, new constants; FSM: same S and A, new s0, λ and rules); TRAIN reuse: AGG
sessions j with j mod 4 = 3 (50 of 200) repeat a uniformly chosen earlier TRAIN AGG program;
TRAIN session s is FSM iff s mod 6 = 5; CALIB: 2 families x 3 n x 4 change classes x 4 sessions =
96 sessions of BEGIN + 16 x (change, QUERY), change = none, one SET, a BATCH of 2..64 SETs, or a
BATCH of 1024 SETs; AGG type A deletes the first index whose mapped value equals the current fold
result, then inserts a random value at a random index.

K9 (CandCore, EvalJudge). Experience tap (6.1): the evaluator runs one baseline router B process
over every group of a split, in order, as `BASE tap - - - TAPFILE SPLITARG`. TAPFILE must not exist
(a fresh file per tap); SPLITARG is the record word 9 value: 1 TRAIN and TRAINP, 2 CALIB, 3 DEV. The
operator admits a tap by appending `<sha256> AUTHORIZED <label>` to ALLOW.

K10 (CandLib, EvalJudge). Control mutants C1, C1b, C1c are codegen-only (`drive build ...
mutant:c1|c1b|c1c`): c1 compares AGG shape ids with the wl byte cleared; c1b ends every wl-16 lane
map with `v = and v 255`; c1c reads c_j from R1P word 5 + 2j. Each also runs with
`unit:check=NOG3` (G3 removed). C1 is detected by G3 (route 5 on every QUERY) or by wrong answers;
C1b and C1c with G3 must give route 5 and no wrong answer; every NOG3 variant must give wrong answers.

K11 (EvalJudge). C1 detection evidence: the controls log prints both the route-5 count and the
wrong count, so the record shows which mechanism caught the mutant.

K12 (CandDelta, CandSelect). Churn (5.2): an invalidation is a MIN/MAX extreme rescan or a drop of
live delta state (at PROGRAM, or when a change or restore finds the state unusable), counted at most
once per drop; a cutoff miss is an FSM recompute unit that ran to n without a cutoff. From 64
invalidations or 64 cutoff misses since the session's BEGIN snapshot, delta is invalid for the rest
of the session.

K13 (EvalJudge). C3 equality scope: with diagnostic cost words rotated across records, gen-G.om and
gen-G.elf must be byte-identical and manifest word 4 and words 56..183 equal; the lineage words
differ by construction.

K14 (EvalJudge). STATS capture: after the last request the judge sends STATS and stores the status,
q and the q counters zero-padded to 32 words in `stats-A-R-gNN.ctr`. Diagnostic only and excluded
from every latency sum.

K15 (CandLib, EvalJudge). C8 uses `crashafter:K` for K = 1..9, one per publish step, over a copy of
the honest generation; drive exits 97, and the library must stay self-consistent and loadable with
its own MANSHA.

K16 (CandLib, from the independent candidate review). Calibration handoff: lbload reads gen-G.cal
once, checks the SHA-256 of exactly those bytes against manifest words 52..55, and passes the same
bytes to the generation on a memfd at fd 7; the router reads `/proc/self/fd/7`, never LIBDIR. This
closes a check-then-read race (P0).

K17 (EvalJudge, from the independent evaluator review). END handling: proc.log word 15 is the END
round trip (part of the process cost); word 10 is the exit status plus 1024 when END failed
(status not 0, q not 0, end of file or timeout). Any END failure or nonzero exit is one wrong refusal
(STOP).

Independence note. The lanes were written by separate agents under one operator account
(UNISOLATED). One slip is recorded and not mitigated: helper names of `r1.om` were broadcast to the
evaluator agents during integration. The evaluator's reference `ref.om` was written from the contract.
