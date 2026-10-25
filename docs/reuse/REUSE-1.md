# REUSE-1: experience-selected guarded specialization, profile R1 (contract v1.0)

**Status:** FROZEN by the commit that adds this file, before any implementation, collection or
measurement. A change after that commit is a new version with its reason recorded; data gathered
under one version is never re-scored under another.
**NOT A MASTER PLAN.** Component-local experiment contract for omega-mv #742 (methods), #743
(incremental execution), #744 (lifecycle-cost selection) and spark-rsi-mv #73 (independent
qualification). Parents: omega-mv #584, #652; aien-architecture-mv #307. It changes no release
sequencing, no milestone status (M11, M25, M26, M38 stay PLANNED) and no subsystem ownership.

Host only: Linux arm64, DGX Spark CPU. GB10: NOT_RUN. Energy: NOT_MEASURED (a host power method
exists for the #657 gate but is not integrated or calibrated for this experiment; missing joules
are unavailable, never zero). The 27B and 35B services are not touched.

Reviewed before freezing by two independent read-only consultants (2026-10-25); their required
changes are applied. Recorded limits are in section 11.

## 0. Owners, successor slices, non-goals

| Need | Existing owner | REUSE-1 slice (R1 only, follows the owner's semantics) |
|---|---|---|
| Compiler | seeded omc: omega-mv `seed/omc/omc.om` sha256 `2e35db7d6c2066fb…`, binary sha256 `734f0a39319fbfa930bcc62cb30dbf06d47d78e126372b74f5baf478e6126b61` | none; every first-party program here is handwritten `.om` compiled by that binary |
| Synthesis, trace learning | #656, #658 (`src/omega_synthesis.c`) | `om/reuse/learn.om`: selects R1 templates from admitted experience |
| Guarded compilation | #665 | `om/reuse/codegen.om`, `om/reuse/check.om` |
| Incremental semantics | #657, `spec/omega-incremental-semantics.md`, `rx_semantic` | `om/reuse/delta.om` |
| Generations | ADR 0015, `rx_generation.h` | `om/reuse/lib.om` (commit order of ADR 0015 for the library files) |
| Cost selection | #522, #583, #584, #600, #698, #704, `rx_costmodel.c` | `om/reuse/select.om` |
| Experience | #699 (C, PR #707) | `om/reuse/exp.om`: R1 query records, see 11 |
| Durable memory | Cortex `rx_cortex` (ADR 0022) | none: BLOCKED, see 11 |
| Independent judge | spark-rsi-mv #67 (Rust BlindJudge, receipt v2), #68 | `spark-rsi-mv om/reuse-eval/`: the R1 evaluation slice of the #67 judge's `.om` successor |
| Turing scorer | omega-mv #700, existing turing-coder | `calibration/profiles/Turing-profile-REUSE-v1.0.toml` |

No second compiler, runtime, memory store, scheduler, trainer, judge, signer or promotion
authority. No method enters any production path. The library directory is a frozen,
reproducible build output (section 3.5), not memory. Nothing learns at run time (section 5).

## 1. Profile R1: supported semantics

Words are unsigned 64-bit little endian. Arithmetic is exact as written.

### 1.1 Program PROG (64 words, 512 bytes)

AGG (family 1):

| Word | Meaning |
|---|---|
| 0 | magic 3544411312749556050 ("R1PROG01") |
| 1 | 1 |
| 2 | lane width wl: 8 or 16 |
| 3 | fold f: 1 ADD, 2 XOR, 3 MIN, 4 MAX, 5 OR, 6 AND |
| 4 | steps k: 1..6 |
| 3+2j, 4+2j for j = 1..k (words 5..16) | op_j, c_j. op: 1 ADD, 2 SUB, 3 MUL, 4 AND, 5 OR, 6 XOR, 7 SHL, 8 SHR. c_j < 2^wl; for SHL and SHR c_j < wl |
| pair words with j > k, and 17..63 | 0 |

FSM (family 2):

| Word | Meaning |
|---|---|
| 0 | magic |
| 1 | 2 |
| 2 | states S: 2..16 |
| 3 | symbols A: 2..16 |
| 4 | start s0 < S |
| 5 | rule count R: 1..24 |
| 6+s, s < 16 | λ[s] < 256; 0 for s >= S |
| 22+r, r < 24 | rule r = s + 256 lo + 65536 hi + 16777216 t with s < S, t < S, lo <= hi < A; 0 for r >= R |
| 46..63 | 0 |

Any other value makes PROG invalid (status 2). Program digest = SHA-256 of the 512 bytes
(`om/sha256/sha256.om` of omega-mv main, concatenated unchanged). A digest is held as 4 words,
word j = bytes 8j..8j+7 read big endian.

Shape id (u64): AGG = wl + 256 f + 4096 k + 65536 * sum over j of op_j * 16^(j-1); FSM = 1.

### 1.2 State and result

AGG: X = (x_0..x_{n-1}), 0 <= n <= 4096, x_i < 2^wl. Lane map e(x): v = x; for j = 1..k:
ADD v = (v + c) mod 2^wl; SUB (v - c) mod 2^wl; MUL (v c) mod 2^wl; AND, OR, XOR bitwise;
SHL (v 2^c) mod 2^wl; SHR floor(v / 2^c). Fold: acc = id_f, then acc = acc f e(x_i) for
i = 0..n-1; ADD is mod 2^64 (it cannot wrap: 4096 (2^16 - 1) < 2^28), the others are the plain
u64 operations. id: ADD 0, XOR 0, MIN 2^64 - 1, MAX 0, OR 0, AND 2^64 - 1. Result r0 = acc,
r1 = n. All six folds are commutative monoids, so order never matters.

FSM: w = (w_1..w_n), 0 <= n <= 4096, w_t < A, stored at index t - 1. q_0 = s0,
q_t = δ(q_{t-1}, w_t); δ(s, a) = t of the lowest-numbered rule with rule.s = s and
lo <= a <= hi, else s. Result r0 = q_n, r1 = sum over t = 1..n of λ[q_t].

No trap is part of R1. Every operation is total on valid input; a trap on valid input FAILS.
Work is bounded by the profile (AGG n k lane steps, FSM n R rule tests); no fuel word is needed.
A session owns one state array; R1 has no references and no aliasing.

### 1.3 Changes

Valid only inside the current program's domain (AGG v < 2^wl, FSM v < A), with indices in range
and n <= 4096 after the change. Invalid: status 3, nothing changes.

| op | payload words | meaning (0-based index) |
|---|---|---|
| 3 SET | i, v | x_i = v |
| 4 INS | i, v | insert v before index i, 0 <= i <= n |
| 5 DEL | i | remove index i |
| 6 SWAP | i, j | exchange indices i and j (i = j allowed) |
| 7 BATCH | m, then m triples (op, a, b), m 1..1024, op 3..6 (b unused by DEL, 0) | applied in order, all or nothing; validity of each item is judged on the state left by the items before it |
| 8 PROGRAM | 64 PROG words | new program, same state; refused (3) if any value is outside the new domain, refused (2) if PROG is invalid |

Incremental obligation (#743): result(P, apply(s, d)) is what a delta path must return after any
valid change d. R1 is exact integers; no floating-point mode exists.

## 2. Arm protocol (evaluator <-> arm process)

The evaluator starts an arm with `execprog`, stdin and stdout pipes. Request: op, p, then p
words. Response: status, q, then q words.

| op | p | q |
|---|---|---|
| 1 BEGIN | 65 + n: PROG, n, n values | 0; replaces any session |
| 2 QUERY | 0 | 3: r0, r1, route |
| 3, 4, 6 | 2 | 0 |
| 5 | 1 | 0 |
| 7 BATCH | 1 + 3m | 0 |
| 8 PROGRAM | 64 | 0 |
| 9 STATS | 0 | up to 32 counters (diagnostic) |
| 10 END | 0 | 0, then exit 0 |

Status 0 ok, 2 invalid program, 3 invalid change, 4 unsupported request, 5 capacity, 6 no session.
An unknown op or p above 65 + 4096 + 1 + 3072 is answered 4 and the arm exits 4. A p that does not
match its op is 4 (the payload is read and discarded). route (never trusted): 1 FULL, 2 CACHE hit,
3 METHOD, 4 DELTA, 5 FALLBACK (a method or generation was refused; baseline used).

argv: `ARM LIBDIR MANSHA ALLOW TAPFILE SPLIT` (unused = `-`). MANSHA is the hex SHA-256 of the
manifest the evaluator authorizes (section 3.5). ARM:

| ARM | behaviour |
|---|---|
| echo | QUERY answers r0 = r1 = 0, route 0; pipe-overhead control, never scored for correctness |
| full | the qualified baseline: interprets PROG over the whole state at every QUERY |
| cache | exact answer cache: key = SHA-256(program digest, n, state digest); the state digest is recomputed only at the first QUERY after a change; at most 4096 entries, FIFO eviction; miss = full |
| method | methods recompute the whole state at every QUERY; no template or refused bind = full |
| delta | method plus delta maintenance (section 4); with no bound method the delta path uses the baseline lane map and δ |
| select | delta plus the #744 selector (section 5) |
| tap | full, plus one experience record per QUERY appended to TAPFILE with split SPLIT |

Every arm binary is a router built by `om/reuse/drive.om`. The **baseline router B** is the router
built with an empty library. B serves `full`, `cache`, `tap`, `echo` and loads generations.

## 3. Methods and the library (#742)

### 3.1 Experience record (128 words, 1024 bytes)

| Word | Meaning |
|---|---|
| 0 | magic 3544386028377026898 ("R1EXP001") |
| 1 | version 1 |
| 2 | sequence number, strictly increasing from 1 in a file |
| 3 | family |
| 4 | n |
| 5, 6 | r0, r1 of the baseline |
| 7 | ns of the baseline evaluation (diagnostic; never used for admission or selection) |
| 8 | steps (AGG n k, FSM rule tests) |
| 9 | split: 1 TRAIN, 2 CALIB, 3 DEV |
| 10, 11, 12 | session ordinal, query ordinal in the session, changes since the previous QUERY |
| 13 | provenance 1 = operator-owned local synthetic workload, consent yes, Apache-2.0, local only |
| 14 | sample count m, 0..16 |
| 16..47 | AGG: m pairs (x, e(x)) for the first m distinct lane values in index order. FSM: m words q + 256 a + 65536 q' for the first m distinct transitions taken. Rest 0 |
| 48..51 | state digest: SHA-256 of the n values as LE words |
| 52..55 | program digest |
| 15, 56..63 | 0 |
| 64..127 | PROG |

Admission (`exp.om`): SHA-256 of the whole file appears in ALLOW as `<64 hex> AUTHORIZED <label>`
or `<64 hex> SYNTHETIC <label>` and no `<64 hex> REVOKED <label>` line exists; file size a
positive multiple of 1024, at most 32 MiB; every record: magic, version, sequence, family, valid
PROG, program digest equal to the recomputed one, split 1..3, provenance 1, m <= 16, zero words
zero, and **every sample verified** against the baseline (AGG e(x) recomputed; FSM δ recomputed).
One bad record refuses the whole file. r0, r1 and word 7 cannot be re-derived (the state is not
stored) and are never used. Admitted experience grants no authority.

### 3.2 Templates and methods

A **template** is an AGG shape (wl, f, k, op_1..op_k) with c_1..c_k as parameters, or the one FSM
template. A **method** is a template's generated function in a router. It applies to a program iff
the program is valid (guard G1) and its shape id equals the template's (G2), and it is used only
after bind validation (G3, section 3.3). A method computes the result for any state; it is not a
cached answer.

AGG method code (generated): `mtl<t>` evaluates the template's ops in order with the constants
read from PROG words 4+2j; the run loop is the fixed text: acc = `r1ident f`; for i < n:
acc = `r1fold f acc (mtl<t> x_i)`. The fold and identity are the baseline's own functions.
FSM method: at bind, the table δ[s][a] for every s < S, a < A is filled by the baseline `r1delta`
(at most 256 cells); the run loop reads only the table and the baseline λ.

Learning (honest name: **experience-selected guarded specialization**): `learn.om` counts QUERY
records per shape in admitted TRAIN experience and admits templates by the rule of section 5.3.
Outcomes are not used to form a method; experience decides which shapes are compiled. FSM methods
are tabulation of the rule program (guarded compilation), not learned transition semantics. No
rewrite (op-sequence simplification) exists in v1.0.

### 3.3 Checking

G3 bind validation, inside the deployed router, before any result of that method is returned:
AGG: for every x < 2^wl, `mtl<t> x` = baseline `r1lane x`, all 64 bits compared (at most 65,536
pairs per program). Passed program digests are kept in a FIFO of 256 (process memory only; a
restart validates again). Failure = route 5 for that session. FSM: the table is built by the
baseline; nothing else is checked at bind.

What this establishes, and its trusted base: for a bound AGG program the method's lane map equals
the omc-compiled baseline lane map on the whole lane domain, so (the fold, identity and loop being
the fixed baseline-calling text) the method's result equals the omc-compiled baseline's result for
every valid state. For FSM, the table equals the baseline δ on every cell; the run loop and λ
accumulation are fixed text checked by unit tests and by the evaluator's per-QUERY comparison,
not proven. Trusted base: omc 734f0a39, the Linux arm64 host, and the fixed loop text. B, the
methods and the evaluator's reference are all compiled by the same omc, so an omc miscompile can
affect them in common; known-answer vectors computed outside Omega (Wolfram Language, recorded in
`tests/reuse/kat.txt`) cover every op, every fold and both widths at their boundaries.
Finite probes, observational signatures and the six-input synthesis signature are never used as
proof.

### 3.4 Library files and generations

LIBDIR (0700): `gen-G.om` (router source), `gen-G.elf` (router), `gen-G.man` (manifest),
`CURRENT` (40 bytes: G, then the SHA-256 of gen-G.man).

Manifest (256 words): 0 magic 3544386019401740626 ("R1MAN001"); 1 version 1; 2 G; 3 parent G;
4 template count K <= 16; 5 router source bytes; 6..9 router source SHA-256; 10..13 router ELF
SHA-256; 14..17 compiler SHA-256; 18 profile version 1; 19 lineage count L <= 8; 20..51 lineage:
L SHA-256 digests of admitted experience files (4 words each); 52..55 calibration file SHA-256
(0 if none); 56..183 per template 8 words (shape id, family, wl, f, k, TRAIN QUERY count, source
bytes of its functions, 0); 184..254 0; 255 FNV-1a 64 of words 0..254 (corruption check only).

Publish (ADR 0015 order): gen-G.om, gen-G.elf, gen-G.man are each written as `*.tmp`, fsync'd and
renamed with `renames` flag 1 (never replacing an existing generation); fsync LIBDIR; `CURRENT.tmp`
written, fsync'd, renamed onto CURRENT; fsync LIBDIR. A crash at any step leaves CURRENT naming
the previous complete generation.

Load (B started with ARM method, delta or select): read CURRENT; read gen-G.man; require
SHA-256(gen-G.man) = the CURRENT digest = MANSHA from the evaluator; check magic, version,
self-check, compiler SHA-256 = the pinned omc, profile version 1, every lineage digest AUTHORIZED or
SYNTHETIC and not REVOKED in ALLOW; read gen-G.elf into a `memfd`, require its SHA-256 = manifest
words 10..13, then `execprog /proc/self/fd/N` with the same argv plus `loaded`. Any failure:
B serves the request stream itself as `full` with route 5 on every QUERY. A generation never
checks itself; B, which the evaluator pins, does the checking before exec, and the evaluator pins
MANSHA from its own reproducible rebuild (section 7.4).

### 3.5 Identity and bounds

Restart identity: two loads of one generation give the same manifest, ELF SHA-256 and responses.
`drive.om build` is deterministic: the same inputs give byte-identical gen-G.om, gen-G.elf and
gen-G.man. Bounds: K <= 16, router source <= 512 KiB, LIBDIR <= 8 MiB including retained
generations, delta state <= 1 MiB per session, cache <= 4096 entries.

## 4. Incremental execution (#743)

Terms of `spec/omega-incremental-semantics.md`: sources are the lanes or symbols; derived values
are the mapped lanes and the fold (AGG) and the states q_t and the λ sum (FSM). derivation_id =
SHA-256("r1.method" 0x00, out_type 2, library generation G, SHA-256 of the method's source text),
or SHA-256("r1.baseline" 0x00, 2, 0, SHA-256 of B's source) for the baseline lane map. A session
binds one derivation_id at BEGIN or PROGRAM; a response never mixes two.

AGG delta state. ADD: acc' = acc - e(old) + e(new) mod 2^64; XOR: acc' = acc xor e(old) xor e(new).
OR and AND: count[b] = number of mapped lanes with bit b set, b < 16; result for n > 0: OR =
bits with count > 0, AND = bits with count = n (bits 16..63 are 0); n = 0 gives the identity.
MIN and MAX: a count per mapped value (2^wl counters) and the current extreme; when the last copy
of the extreme goes, scan to the next nonzero count (at most 2^wl steps, the adversarial case);
n = 0 gives the identity. SET = remove old + add new; INS = add; DEL = remove; SWAP = no work.

FSM delta state: q_0..q_n (stored trajectory) and the sum. A SET at index i changes w_{i+1};
recompute q_t for t = i+1, i+2, ... and stop at the first t > i with new q_t = stored q_t: from
there on every state is unchanged (early cutoff). INS at index i: the new symbol is w_{i+1}, old
w_t for t > i become w_{t+1}; recompute from t = i+1, compare new q_t with the stored state of the
same symbol suffix (old index t - 1), stop at the first equality, then shift the stored suffix.
DEL at index i: old w_{t+1} for t > i become w_t; recompute from t = i+1 comparing with old q_{t+1},
then shift. SWAP = two SETs. BATCH: items are applied to the stored trajectory in order, cutoff
judged per item. The sum changes by the λ difference over the recomputed range only. No cutoff
means recomputation to n.

Transactions: a change or BATCH is validated completely (section 1.3) before anything is
written; then the state and the delta state are updated; a refused request changes nothing. A
PROGRAM change or a revocation seen at QUERY drops the delta state and the binding and rebinds.
Revocation snapshot: ALLOW is read and hashed at BEGIN; at each QUERY its stat (inode, size,
mtime ns, ctime ns) is compared and on any difference ALLOW is re-read and the lineage re-checked;
a revoked lineage gives route 5 from that QUERY on. R1 graphs are a chain (FSM) or a one-level
DAG (AGG); no recursion occurs. Missing tracking or an unsupported case falls back explicitly
(route 3 or 1).

## 5. Selection by lifecycle cost (#744)

### 5.1 Order

Validity and authority first: G1, G2, G3, generation identity, lineage not revoked. Only valid
options are ranked by cost. Cost never waives a guard. An unsupported op, family or path id is
refused before any cost lookup and never counted in a supported cost bucket (the #704 rule).

### 5.2 The select arm

Per QUERY, among the valid options {delta, method, full}; per BEGIN or PROGRAM, whether to bind a
method at all (bind validation costs up to 65,536 lane pairs). Inputs: the frozen calibration file
and deterministic features only (family, wl, f, k, n size class, changes since the last QUERY,
binding state). The select arm learns nothing at run time and measures nothing about itself.

Calibration file (`R1CAL001`, magic 3544386010811150674), produced by the **evaluator** from its
own round-trip measurements on CALIB workloads (section 7): per family, per path (full, method,
delta, bind), per n class {64, 512, 4096}, per change class {0, 1, 2..64, >64}: p50 and p90 ns.
Missing file or a missing cell: the fixed rule of the delta arm (cold-start behaviour).
Rule: bind iff p50(bind) < E_q (p50(full) - p50(method)), E_q = mean QUERYs per session in CALIB;
per QUERY choose the lowest p50 among valid paths. Adversarial churn: after 64 invalidations or
64 cutoff misses in one session the delta path is disabled for that session.

Run-time discovery is **NOT_RUN** in v1.0: it would make LIBDIR learn from FINAL and hide its cost
in children. Bounded discovery is the offline build (5.3).

### 5.3 Offline template admission (the bounded discovery)

From admitted TRAIN experience: count QUERY records per AGG shape. Admit shapes in descending
count (ties: lower shape id) while count >= 8 and K < 16. Stated assumption: the FINAL shape
distribution is the TRAIN distribution plus unseen shapes; no probabilistic guarantee is claimed.
The FSM template is admitted iff TRAIN has at least 8 FSM QUERY records. Budget: the build stops
with no publish if omc takes more than 120 s; every failed compile is recorded.

### 5.4 Cost terms charged

Per request (evaluator round trip): routing, guards, bind validation, hashing, delta work, method
or baseline execution, invalidation and rebinding, and arm start-to-ready including generation
load and identity checks (charged to the first request of each process). Offline: acquisition =
learn + codegen + omc + publish, measured by the evaluator, charged once per repetition.
Residency: LIBDIR bytes and peak RSS. Elapsed ns, CPU time and bytes are reported separately.

Required tested behaviour: cold start (no calibration), high novelty (all-cold sessions), rule
drift, incorrect confidence (a calibration file 10x wrong: correctness unchanged, cost reported),
adversarial invalidation churn, exhausted budgets (K = 16 reached; omc over 120 s).

## 6. Measurements

By the evaluator: round-trip ns per request (monons), cold (first QUERY after BEGIN, PROGRAM or
process start) and warm, p50 / p95 / p99; each QUERY's r0, r1 against the evaluator's own reference
implementation; refusal statuses against the reference's; arm CPU time from `/proc/PID/stat`
(utime, stime, cutime, cstime) and memory from `/proc/PID/status` (VmHWM, VmRSS), read before
END; LIBDIR bytes; acquisition elapsed and CPU time (the build run as a timed child; omc child CPU
via cutime/cstime). By the arm, STATS (diagnostic only): route counts, binds, bind failures,
invalidations, cutoff lengths, delta disables.

Payback: QUERYs until cumulative (full - arm) round-trip ns exceeds acquisition ns.
#744 normalized cost: Cbar = r + (1 - q) e + q + K/N with r, q, e, K, N re-estimated from these
measurements; the Wolfram numbers in #744 are illustrations, not targets.

Placement: the evaluator runs on CPU 18 and every arm on CPU 19 (both X925 class, 3.9 GHz), set
by `taskset` at exec (an OS tool; `.om` has no affinity word); the CPU list and load average are
recorded per run. Every run is `nice -n 10`.

## 7. Workloads, splits and holdout (#73)

### 7.1 Splits

The generator is part of the evaluator. TRAIN (experience), TRAIN' (the shape-permuted control
experience: same sizes, shape pool disjoint from TRAIN's), CALIB (calibration), DEV (adaptive
development, any number of runs), FINAL (sealed, one run). TRAIN, TRAIN', CALIB and DEV seeds are
public constants of the evaluator. AGG shape pool: 24 seen shapes drawn from the TRAIN seed; TRAIN
sessions use them with a 1/(rank + 1) weight; TRAIN' uses 24 other shapes with the same weights.

### 7.2 FINAL composition (frozen counts)

Families AGG and FSM; n in {64, 512, 4096}; session types, 4 sessions per family x type x n:

| type | requests after BEGIN |
|---|---|
| Z zero change | 32 x QUERY |
| S sparse | 64 x (SET, QUERY) |
| D dense | 8 x (BATCH of 1024 SET, QUERY) |
| R reorder | 64 x (SWAP, QUERY) |
| I insert/delete | 64 x (INS or DEL alternating, QUERY), n kept within 1..4096 |
| A adversarial | AGG: f in {MIN, MAX}, 64 x (DEL of a current extreme, INS of a random value, QUERY); FSM: machines with no reconvergence, 64 x (SET at index 0, QUERY) |
| P rule drift | 16 x QUERY with a PROGRAM (same shape, new constants or new rules) before every 4th |
| C all-cold | as S, programs of unseen shapes only |

That is 2 x 8 x 3 x 4 = 192 sessions. Novelty classes, AGG types Z S D R I A P: per 4 sessions
1 recall (a TRAIN program), 2 transfer (a seen shape, new constants), 1 novel (an unseen shape);
type C: 4 novel. FSM: 2 recall (TRAIN machines), 2 new machines. Sessions are dealt into 24 groups
of 8 by a seeded permutation; one group is one arm process lifetime (a restart between groups).

Arms in FINAL: echo, full, cache, method, delta, idelta (delta with the empty library: B's delta
path), delta' (delta with the TRAIN' library), select. Each group runs on every arm, arm order per
group and repetition a seeded permutation; 3 repetitions; LIBDIR is copied fresh from the
evaluator's rebuild for every repetition.

### 7.3 Holdout

Before the evaluator freeze: a 32-byte secret from `random` is stored in the operator's TPM vault
(`atlas-vault add REUSE73_FINAL_SECRET`), and SHA-256(secret) is committed and pushed in
spark-rsi-mv `evidence/REUSE-73/final-commitment.txt`. FINAL seed = SHA-256(secret ||
candidate-freeze commit id || evaluator-freeze commit id) (ASCII hex ids). Order: evaluator freeze
(spark-rsi-mv commit pushed), candidate freeze (omega-mv commit pushed, with the TRAIN and CALIB
library and its manifest SHA-256), then the reveal and one FINAL run. A void run burns the seed;
a new run needs a new secret and a recorded reason. Independence status: **UNISOLATED**. One
operator account holds both sides; non-access is procedural (separate authoring agents that read
only this contract, vault read only by the evaluator run) and auditable by commit order, not
enforced by the machine.

### 7.4 Evaluator rebuild

The evaluator rebuilds the library from the admitted TRAIN experience with the frozen `drive.om`
and requires its manifest SHA-256 to equal the candidate's frozen value; that value is MANSHA.
A mismatch is BLOCKED (not reproducible).

### 7.5 Receipt for the #67 judge

The evaluator writes a receipt with the RECEIPT-V2 identity fields of spark-rsi-mv
`docs/RECEIPT-V2.md` (subject = candidate-freeze commit and MANSHA, holdout-set digest = SHA-256
of the generated FINAL request files, policy digest = SHA-256 of this contract, evaluator binary
digest, counts) and an evaluation-budget record (one FINAL run). Signing is BLOCKED: the #67 P-256
signer is Rust and no `.om` signer exists; the receipt is unsigned and says so.

## 8. Turing accounting (Definitions 1-3, omega-mv #700)

Profile `calibration/profiles/Turing-profile-REUSE-v1.0.toml` (frozen with this file). D = the low
4 bits of r0 of every FINAL QUERY, in request order, given the inputs. B = the baseline router;
M(arm) = B plus that arm's library, calibration parameters and arm code. Every model predicts the
exact value; the coded distribution puts 65,521 / 65,536 on it and 1 / 65,536 on each other symbol
(CODER_SPEC: K 16, every q >= 1). B and M give identical streams, so their coded data lengths are
equal and cancel; the existing turing-coder (a647b6a9, rANS and range) is run on both to show it.
Model charge P1 (primary, declared parameters): 64 bits per template word of the manifest entries
plus 64 bits per calibration value an arm reads. P2 (sensitivity): 8 bits per byte of generated
method source and of arm-specific unit source. T(arm) = -(L(M|G) - L(B|G)) < 0 for every arm with a
library; ΔT between generations is minus the change of the charge, and a positive ΔT can only come
from deleting templates and is never reported as a gain. Equivalent-execution savings are
reported separately (section 6). A positive T is impossible in R1 by construction; this is stated
before any data.

## 9. Controls (each must be detected; an undetected control is a STOP)

| id | control | expected |
|---|---|---|
| C1 | router mutant: G2 ignores wl | wrong answers on the control workload |
| C1b | router mutant: a method that drops a final `AND 255` at wl 16 | G3 refuses at x = 256, route 5, no wrong answer; with G3 also disabled: wrong answers |
| C1c | router mutant: `mtl` reads c_j from the wrong word | G3 refuses |
| C2 | one byte changed in an admitted experience file; a record with a wrong program digest | admission refuses the file |
| C3 | sample outputs permuted across records; then costs permuted | first: admission refuses (sample check); second: rebuilt library byte-identical (costs are not used) |
| C4 | delta mutant keeping delta state across PROGRAM | wrong answers |
| C5 | lanes 2^wl - 1 with MUL and SHL; values >= 2^wl | known-answer values; status 3 |
| C6 | one byte of gen-G.elf; one byte of gen-G.man; a coherent replacement (new ELF, manifest and CURRENT consistent with each other) | load refuses (route 5): the first by ELF digest, the second by MANSHA and self-check, the third by MANSHA |
| C7 | lineage REVOKED before start; REVOKED during a session | load refuses; route 5 from the next QUERY |
| C8 | publisher killed after each publish step | CURRENT names a complete generation that loads |
| C9 | manifest compiler SHA-256 or profile version changed | refused |
| C10 | truncated manifest or CURRENT | refused |
| C11 | arm killed with SIGKILL mid-session | a fresh arm loads the same identity and answers equal |
| C12 | shape-permuted library (delta') and empty library (idelta) | reported as ablations, scored like delta |

## 10. Decision (#73) and per-issue conclusions

rho(arm) = (sum over FINAL of the arm's round-trip ns, every request including start-to-ready,
plus acquisition ns) / (the same sum for full). Uncertainty: percentile bootstrap over the 24
session groups (clusters; each group's 3 repetitions averaged first; paired across arms), 10,000
resamples, splitmix64 seeded with the first 8 bytes of the FINAL seed; 95 % intervals.

- **STOP**: any wrong answer or wrong refusal from full, cache, method, delta, idelta, delta' or
  select on FINAL; an undetected control; a tampered or revoked artifact served as a method; or
  lower bound of rho(delta) >= 1.
- **GO**: no STOP condition; upper bound of rho(delta) < 1; upper bound of rho(delta)/rho(cache)
  < 1; upper bound of rho(delta)/rho(idelta) < 1 (the learned library contributes); and at least
  half of the transfer-class QUERYs of delta served by a method (route 3 or 4).
- **INVESTIGATE**: otherwise.

Reported, never folded into the decision: rho and p50/p95/p99 per arm, session type, novelty
class, family and n; coverage and fallback; memory; payback; Cbar terms; T and ΔT; echo-subtracted
ratios as a secondary figure; the ns per interpreter step of full against the evaluator's
reference (so the denominator is not a strawman). #742 conclusion: transfer coverage, bind
failures, controls C1-C3, C6-C11, restart identity. #743: rho(delta)/rho(method) and
rho(idelta)/rho(full) per change regime; C4, C8. #744: rho(select)/rho(delta) (selector enabled
versus disabled at matched correctness): upper bound < 1 "selector helps", lower bound >= 1 "does
not help", otherwise inconclusive. Verdicts are given separately for source (review), host (Spark
CPU runs), simulator (NOT_RUN) and physical (NOT_RUN).

## 11. Limits and blockers, stated before implementation

- **Cortex (#742 persistence item): BLOCKED.** `rx_cortex` is C; the omega-bridge ABI runs
  C -> .om only; there is no Cortex CLI; an `.om` Cortex writer would be a second writer (ADR 0022).
  Admission claims go to the evidence directory. Route: #651 and the ADR 0022 owner.
- **#699 experience (#73 item): NOT MET.** R1 records come from the `.om` baseline tap on generated
  workloads, not from World/J-Space transitions committed through compose, AEGIS and Cortex (PR
  #707, C). R1 records carry #699's fields (prior state digest, action = the change counts and
  query, outcome, cost) but not its receipt chain.
- **Judge signature: BLOCKED** (7.5).
- **Independence: UNISOLATED** (7.3).
- **FSM run loop**: not proven (3.3).
- Run-time discovery and rewrites: NOT_RUN in v1.0.
- GB10 NOT_RUN; energy NOT_MEASURED; simulator NOT_RUN.

## 12. Unit interfaces (frozen for parallel authors)

Units are concatenated by `drive.om` in this order into one program: `om/sha256/sha256.om` (main,
unchanged), `r1.om`, `exp.om`, `check.om`, `delta.om`, `select.om`, `lib.om`, generated
`methods.om`, `router.om` (holds `fn main`). Name prefixes: r1 (R1*), ex (EX*), ck (CK*), dl (DL*),
sl (SL*), lb (LB*), mt (MT*), rt (RT*), lg and cg (learn.om, codegen.om, in `drive.om` only).
Globals are pools; scalar state is a 1-element pool.

r1.om: `global R1P ulong 64`, `R1N ulong 1`, `R1X ulong 4096`, `R1R ulong 2`, `R1T ulong 256`
(FSM table). `fn r1valid to int` (0 or 2, R1P), `fn r1indom v ulong to bool`,
`fn r1lane x ulong to ulong`, `fn r1ident f ulong to ulong`, `fn r1fold f ulong a ulong b ulong to
ulong`, `fn r1delta s ulong a ulong to ulong`, `fn r1full to void` (R1P, R1N, R1X -> R1R),
`fn r1shape to ulong`, `fn r1pdig to void` and `fn r1sdig to void` (digests into `R1PD ulong 4`,
`R1SD ulong 4`). Globals cannot be passed, so hashing goes through one fixed global buffer:
`global R1HB uchar 65536`, and `fn r1hash n ulong to void` hashes R1HB[0..n) into `R1HD ulong 4`
(through local 4096-byte chunks handed to `omsha`).
methods.om (generated; empty library = no templates): `fn mtfind to long` (template index for R1P
or -1), `fn mtlane t ulong x ulong to ulong`, `fn mtrun t ulong to void` (-> R1R), `fn mtcount to
ulong`, `fn mtshape t ulong to ulong`, `fn mtsrc t ulong D edit ulong 4 to void` (source SHA-256).
check.om: `fn ckbind t ulong to int` (0 pass, 1 fail; AGG all-x check, FSM table build into R1T).
delta.om: `fn dlbind t long to int` (t = -1: baseline lane map / δ), `fn dlchg op ulong i ulong v
ulong old ulong to int` called after the router has validated and applied one change to R1X
(`old` = previous value at i for SET and DEL, the value at j for SWAP; for SWAP v = j),
`fn dlres to int` (0 = R1R filled, 1 = needs recomputation), `fn dldrop to void`.
select.om: `fn slload to int` (calibration from `SLPATH uchar 4096`, 0 ok, 1 absent),
`fn slbind t long to bool`, `fn slpick nchg ulong to int` (1 full, 3 method, 4 delta).
lib.om: `fn lbload to int` (0 = exec'd, never returns; nonzero = refused with a code), `fn lbpub`
(drive.om only), manifest read and check functions.
router.om: argv, the request loop, change validation and application, the arms.
drive.om: `drive base OMC OUTDIR`, `drive build OMC ALLOW CALIB OUTLIB EXP...`, `drive selftest`;
it forks omc with stdin and stdout redirected by `dupfd`.
Build input and output files are named in `om/reuse/README.md`. An interface change needs a
contract version bump.
