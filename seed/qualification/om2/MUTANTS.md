# OM-2 qualification: mutation plan and harness (#748, ledger L836)

Status: Phase A, written before the OM-2 language contract (#745/#746/#747/#655/#665/#625) is frozen.
Rows marked **A: run** were executed in Phase A against the current baseline: omega main `acb3f15`,
`om/sha256/sha256.om`, pinned omc `734f0a39…6b61`. Rows marked **B: NOT_RUN** wait for the frozen
contract and the OM-2 SHA consumer. A Phase A kill is evidence about the baseline consumer and the
baseline compiler. It is not evidence about OM-2.

## What the gate does

`gate.om` is handwritten, seeded Omega and is the coordinator. It is modelled on
`security673/gate669.om`, and its header documents the case-list grammar. In short:

- `gate OMC OMC_SHA256 CASES SCRATCH`. Before running anything, the gate hashes OMC itself
  (FIPS 180-4 in `.om`, with K/IV read from the bc-derived fixtures) and compares the result with
  the pinned digest. On a mismatch every case is NOT_RUN and the gate is INVALID. The pin itself
  comes from `/usr/bin/sha256sum` (GNU coreutils 9.4), an external tool. The gate's own SHA-256
  and coreutils agreeing on `734f0a39…` is the cross-check.
- Each child (compiler, compiled program) runs with stdin, stdout and stderr redirected to files in
  SCRATCH, under a deadline (default 30 s, `deadline` directive). The gate polls `waitpid` and
  sends SIGKILL at the deadline. It prints every command with its redirects.
- Verdicts:
  - **PASS**: exit, stdout and stderr equal the expectation.
  - **FAIL**: deterministic wrong behaviour, reported with the first differing byte or the observed
    exit or refusal code.
  - **INVALID**: no evidence. Causes: signal, timeout, exec failure (child 127), malformed line,
    unreadable input, scratch I/O, or a substitution that does not match exactly once.
  - **NOT_RUN**: planned but not run.

  Final line and exit status: `OM2GATE PASS` 0, `FAIL` 1, `NOT_RUN` 2, `INVALID` 3. Any INVALID
  case, an identity mismatch, or an empty list makes the run INVALID.
- Mutants are applied by the gate. `subst FROM TO` replaces the bytes of FROM by TO in the next
  case's concatenated source. The source must contain FROM exactly once, else INVALID. The mutated
  unit is written only to `SCRATCH/<line>.src`, and the offset is logged (`MUTATE … at byte N`).
  `cc SRC` builds a compiler from a scratch-mutated `seed/omc/omc.om` with the pinned compiler and
  prints its sha256. No tracked source is ever edited. `seed/omc/omc.om` is read only.

## What counts as a kill

A mutant is **killed** only when one of these holds against an independent oracle:

1. **Vector kill** (`kill` mode). The mutant compiles (exit 0, ELF), runs to a clean exit with the
   predicted status, and its stdout differs from the oracle digest. The gate reports the first
   differing byte.
2. **Refusal kill** (`refuse` mode). The compiler exits with the contract's error code, writes no
   output, and its diagnostic names `error CODE`.
3. **Trap kill** (`run` mode). A specified witness exits with the specified trap status (for
   example 103 = trap 3) and no output, where the mutant shows a different predicted behaviour.
   Both sides are cases: the correct side traps, and the mutant side prints its predicted line.

These are never kills: a signal, a timeout, an exec failure, a compiler crash, or any exit other
than the predicted one. The gate turns the first three into INVALID and the rest into FAIL ("not a
meaningful kill"). A survivor (stdout equal to the oracle) is FAIL "mutant survived". A compiler
mutant must also compile the unmutated consumer correctly on a control vector, so that the kill
comes from the mutation and not from a broken compiler (guard.cases `sha-abc-guard-mutant`).

## Oracles

| id | oracle | identity |
|---|---|---|
| O1 | SHA-256 digests: 5 FIPS messages, seq lengths 0..130, 1000, 4095..4097, 8191..8193, 65536; rep:97 lengths 55, 56, 63..65, 4096; 8 seeded random messages | `/usr/bin/sha256sum` (coreutils 9.4) = Python hashlib (OpenSSL 3.0.13). abc, 448-bit and million-a also match the digests printed in NIST FIPS 180-2 Appendix B. |
| O2 | K[0..63], H(0)[0..7] | exact integer roots in bc 1.07.1; Python integer and Decimal(120) cross-check; FIPS 180-4 §4.2.2/§5.3.3 tables extracted by pdftotext |
| O3 | dup32 table: x, (2^32+1)x, low32, high32 for 0, 1, 2^31, 2^32-1 and 8 seeded values | bc + Python |
| O4 | slice table: wrapped vs exact bounds for 16 (n, o, k) triples; row 1 is the issue witness | bc + Python |
| O5 | refusal codes | the frozen OM-2 contract (Phase B); current language: OMEGESE §9 |
| O6 | identities: compiler, artifact, profile | `/usr/bin/sha256sum` pin plus gate.om's in-language SHA-256; the gate prints the sha256 of every compiler it builds |
| O7 | instruction encodings of compiler mutants | GNU objdump 2.42 (`-D -b binary -m aarch64`) |
| O8 | optional historical C (`git show acb3f15:src/sha256.c`, built by gcc outside the closure) | not used in Phase A |

Provenance: `fixtures/PROVENANCE.txt`. The recipes that were run are in `fixtures/oracle/`.

## Mutants

| # | required mutant | application | detecting oracle and meaningful kill | status |
|---|---|---|---|---|
| M1 | wrong constant | `baseline/mutants/k0`: round 0 K 1116352408 to …409. `iv`: init H0 1779033703 to …704 | O1: `mut-k0-fips-abc` differs at byte 1; `mut-iv-fips-empty` differs at byte 0 | **A: run, killed** |
| M1b | wrong constant in the OM-2 source | the same one-digit change in the OM-2 constant table (K[0], K[63], H0), including a constant written with the #745 literal tooling | O1 vector kill; O2 must agree with the table | B: NOT_RUN |
| M2 | wrong rotation amount | `rot`: round 0 Σ1 rotate 25 to 24 | O1: `mut-rot-fips-abc` differs at byte 0 | **A: run, killed**; B: same on the OM-2 rotate |
| M3 | wrong round order | `order`: K[0] and K[1] exchanged between rounds 0 and 1 | O1: `mut-order-fips-abc` differs at byte 0 | **A: run, killed**; B: reversed or swapped round index in the compact loop |
| M4 | missing write / missing initialization | `nowrite`: delete the store of the 64-bit length in `final` | O1: `mut-nowrite-fips-abc` killed. Adequacy control: on the empty message the length is 0 and the mutant survives (`adequacy.cases`, expected FAIL) | **A: run, killed** |
| M4b | missing initialization under #746 | delete one initialization of a checked-initialized variable in the OM-2 SHA | O5: refusal kill with the contract's code. If the compiler accepts, O1 must still kill it, and the acceptance is recorded as a #746 FAIL | B: NOT_RUN |
| M5 | constructor bypass | build a representation-invariant value (dup32 cell, checked struct) without its constructor, or write one half only | O5 refusal kill; O3: constructor output for every table row must give low32 = high32 = x, checked as emitted values, not algebra | B: NOT_RUN |
| M6 | alias violation | the same array as both edit and view argument | current language: `baseline/alias.om` refused with error 19 (`language.cases`, PASS). OM-2: the #747 slice-alias rule with its contract code | **A: control run**; B: NOT_RUN |
| M7 | stale generation / artifact / profile identity | (a) compiler: wrong pin; (b) artifact: consumer built by a different compiler; (c) generation/profile identity of #745/#665: change the source or profile and keep the recorded identity | (a) O6: identity mismatch gives every case NOT_RUN and the gate INVALID (evidence `identity-mismatch.log`). (b) O6: built mutant compilers print sha256 `75297a57…` and `06f000e5…`, not `734f0a39…`. (c) O5 refusal or a specified loader status | (a), (b) **A: run**; (c) B: NOT_RUN |
| M8 | bad ABI length | the #747 slice ABI adapter passes n+1 (over capacity) or n-1 | n+1: trap kill (exit 103, no output) on a witness. n-1: O1 vector kill. The post-final context policy also gets a case | B: NOT_RUN (ABI v1 arrays are fixed 4096) |
| M9 | weakened guard (overflowing slice check) | `compiler-mutants/guard-wrapped`: omc.om `vs_chk` replaced by the wrapped check `ADD u = off + n; CMP u, len; B.HI trap` (scratch copy, built by `cc`) | O4 row 1 (o = 2^64-2, k = 4, len 4, wrapped 1 exact 0): the pinned compiler traps (`witness-pinned` exit 103); the mutant prints `WRAPPED_SLICE_ACCEPTED_4`, exit 0 (`witness-guard-mutant[-exact]`); the mutant still hashes abc correctly | **A: run, killed** |
| M9b | weakened guard in the OM-2 slice check | the same wrapped-sum substitution in the #747 slice-check emission | all 16 O4 rows as cases. wrapped=1 exact=0 rows are the kills; exact=1 rows pass under both compilers; wrapped=0 exact=0 rows trap under both | B: NOT_RUN |
| M10 | faulty optimization pass | `compiler-mutants/shr-imm-plus1`: omc.om `bo_shr` lowers shift-right-by-number with immr = s+1 (scratch copy) | O1: the mutant compiler's consumer build exits 0 and gives wrong digests for abc, empty, 448-bit and million-a (`transform.cases`) | **A: run, killed** |
| M10b | corrupted OM-2 transform (#665) | in a scratch omc.om: unroll factor off by one (drop or repeat one iteration), rotate fusion with the wrong amount (r±1 or 32-r), bounds-check elimination of a non-redundant check | O1 vector kill for unroll and rotate; O4 witness trap kill for check elimination; O7 objdump decode of the emitted instruction confirms the mutation took effect | B: NOT_RUN |
| M11 | separately compiled module boundary | build the SHA unit and its consumer separately (#625/#747); mutate only one side's interface (layout length, parameter order, ABI or generation identity) | O5/O6: the link or load identity check must refuse with the contract code. If accepted, O1 or a trap must kill it, and the acceptance is a module-guarantee FAIL | B: NOT_RUN |

The issue also asks for split coverage. `baseline/sha256.cases` runs 16 vectors with piece sizes 1,
32, 63, 64, 65 and 4095 (96 cases). The `split` mutant (`if lt n lf` to `if le n lf` in `absorb`
loses a block that the buffer exactly completes) is killed with 32-byte pieces on seq-1000. It
survives with 4096-byte pieces (adequacy control). Split vectors therefore stay mandatory for the
OM-2 consumer.

## Phase A evidence

Run from the worktree root. The gate is built with the pinned compiler and the build is byte-for-byte
reproducible (two builds compared equal):

```sh
seed/out/omc < seed/qualification/om2/gate.om > build/om2-qual/gate && chmod +x build/om2-qual/gate
build/om2-qual/gate seed/out/omc "$(cut -c1-64 seed/out/omc.sha256)" seed/qualification/om2/<list>.cases build/om2-qual/<new dir>
```

| list | expected | observed (`evidence/phaseA/*.log`) |
|---|---|---|
| selftest/pass | PASS | `OM2GATE PASS cases=4`, exit 0 |
| selftest/fail | FAIL: `cat-bad-seq` stdout differs at byte 5 | `OM2GATE FAIL pass=1 fail=1`, exit 1 (the second case is the adequacy control, PASS) |
| selftest/controls | each verdict fixed in the list's comments | 5 PASS, 5 FAIL, 8 INVALID (signal: wait byte 9; timeout: SIGKILL at the 1 s deadline; malformed; unreadable source and input; zero-match substitution; EXIT 127; trailing substitution), 1 NOT_RUN; `OM2GATE INVALID`, exit 3 |
| identity mismatch (pin of zeros) | INVALID | 4 NOT_RUN, `OM2GATE INVALID`, exit 3 |
| baseline/sha256 | PASS | 260 PASS: 158 vectors, 96 split cases, 6 mutant kills |
| baseline/adequacy | FAIL (survivors) | 2 FAIL "mutant survived", exit 1 |
| baseline/guard | PASS | 5 PASS (M9) |
| baseline/transform | PASS | 6 PASS (M10) |
| baseline/language | PASS | 1 PASS (alias refused, error 19) |

`selftest/controls` also builds a compiler from the unmodified `seed/omc/omc.om` with `cc`. Its
sha256 equals the pin `734f0a39…`, which is self-reproduction through the gate.

Limits:
- The gate is not a sandbox. Its deadline applies to direct children only. It shares the seed
  compiler lineage with what it tests, which is why every oracle above is external to that lineage.
- The baseline driver `baseline/sha256-main.om` is test scaffolding around the consumer's `omsha`
  and contains no hashing of its own. With piece size 1 it is limited to messages under 1,000,000
  bytes, because an unbounded `while` traps at 1,000,000 passes.
- The constant 2^64-2 in `baseline/slice-witness.om` is copied from `fixtures/obligations.txt`,
  slice row 1.

## Phase B (after the contract is frozen)

1. Replace `baseline/sha256-main.om` with a driver for the OM-2 consumer ABI, including splits and
   the post-final context policy, and generate `om2/sha256.cases` from `fixtures/sha256-vectors.txt`.
2. Retarget M1–M4 and M10 at the compact OM-2 source and transforms. Take M4b, M5, M6, M7c, M8, M9b
   and M11 refusal codes from the contract, and turn the O3/O4 tables into constructor and
   slice-check cases.
3. Compiler mutants stay `subst` + `cc` over a scratch copy of the OM-2 `omc.om`. The pinned digest
   moves to the OM-2 compiler's sha256 from `/usr/bin/sha256sum`.
