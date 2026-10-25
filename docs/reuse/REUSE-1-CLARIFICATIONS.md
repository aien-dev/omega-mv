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
