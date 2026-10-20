# OSC-4 float slice: f16, bf16, f8e4m3, fma and strict functions

Status: implemented on branch `feat/osc4-floats` (ledger L135, roadblock R2), on top of
Slice A (f32 and f64, docs/osc/OSC-4-DESIGN.md section 1). Evidence: section 9.

## 1. What this adds, in plain words

Omega programs could already compute with the two common sizes of decimal-point
numbers (f32 and f64). This slice adds the three small sizes that AI models store
their weights in, plus one operation and one safety switch:

| type | size | what it is | largest value |
|---|---|---|---|
| `f16` | 2 bytes | IEEE half precision: about 3 decimal digits | 65504 |
| `bf16` | 2 bytes | "brain float": the range of f32 with about 2 digits | about 3.4e38 |
| `f8e4m3` | 1 byte | OCP FP8 E4M3: 4 exponent bits, 3 fraction bits, no infinity | 448 |

- `fma(a, b, c)` computes a times b plus c with one rounding at the end.
- `strict fn` marks a function in which producing a NaN ("not a number") or an
  infinity stops the program with a named trap (FLOAT) instead of carrying on.

Every one of these runs two ways, on the reference interpreter and as real Arm
machine code on the Spark, and the two give the same bits, checked against an
independent C reference (section 9).

## 2. The one rule per operation

Rounding is always "round to nearest, ties to even" (RNE), the IEEE default. The
hardware forces nothing else here: the Spark CPU (Cortex-X925 / A725) has native
half-precision arithmetic (`fphp`) and the bf16 convert (`bf16`), and no FP8
instructions. FPCR, the CPU's float control register, stays at the process default
(round to nearest, no flush to zero, NaN propagation on, AH 0).

| type | + - * / sqrt min max | fma | neg, abs |
|---|---|---|---|
| f32, f64 | IEEE operation, one rounding (Slice A) | one rounding | sign bit |
| f16 | IEEE half-precision operation, one rounding | one rounding | sign bit |
| bf16 | widen both exactly to f32, the f32 operation, one RNE rounding back | f32 fma of the widened values (one rounding), one rounding back | sign bit |
| f8e4m3 | same as bf16 | same as bf16 | sign bit |

bf16 and f8e4m3 are *storage formats*: the hardware has no arithmetic for them, so
the rule says exactly what happens (widen, compute in f32, round once). For + - * /
and sqrt the f32 step is wide enough that the result equals a direct correctly
rounded bf16 or f8e4m3 operation (24 bits is at least 2p + 2 for p = 8 and p = 4).
For fma it is the stated two-step rule, no more.

`neg` and `abs` only flip or clear the sign bit, in every format, so they never
round and keep NaN payloads.

min and max follow the Arm FMINNM / FMAXNM rule: if one operand is a quiet NaN and
the other a number, the result is the number; -0 is less than +0.

Comparisons (`==`, `!=`, `<`, `<=`, `>`, `>=`) are false on NaN except `!=`.
bf16 and f8e4m3 compare their exact f32 widenings, which order exactly as the
narrow values do.

## 3. Conversions

Width and signedness are always written out; nothing widens or narrows on its own
(decision 20, docs/PROGRAM.md). `x + y` with x an f16 and y a bf16 is refused;
write `x + (y as f16)`.

- **Float to float** rounds once (RNE). From f64 to bf16 or f8e4m3 the chip has no
  single instruction, so it uses FCVTXN (f64 to f32 with "round to odd") and then the
  RNE narrowing; the Arm manual documents that pairing as one correct rounding
  (DDI0487 M.d C7.2.102 FCVTXN: Round to Odd "enables a floating-point value to be converted
  to a lower precision format using an intermediate precision format while avoiding double
  rounding errors").
- **Integer to float** rounds once. Into bf16 and f8e4m3 only integers of at most 32
  bits are accepted, because a 64-bit integer would be rounded twice on the way; the
  checker says so and asks for `x as f64 as bf16`, which makes the two roundings
  visible in the source.
- **Float to integer** truncates toward zero and saturates at the integer's range; NaN
  gives 0 (the FCVTZS / FCVTZU rule from Slice A). Never a trap.
- **Bit casts** (`x as u16 bits`, `n as f16 bits`) move the bit pattern unchanged.

## 4. NaN, infinity and f8e4m3 specifics

- NaN results follow the Arm manual (J1.4 FPProcessNaNs, FPProcessNaNs3,
  FPConvertNaN): a signalling NaN operand wins and is quietened, else the first quiet
  NaN; for fma the addend `c` is looked at first. Narrowing a NaN keeps its sign and
  the top fraction bits. The interpreter and the C reference spell this choice out
  instead of leaving it to C: C does not say which of two NaN operands comes out, and
  gcc may swap the operands of `+`, `*` or of fma's multiplication (gate G1 caught the
  interpreter doing exactly that for fma).
- bf16 widens to f32 as the manual defines it, its 16 bits followed by 16 zeros
  (J1.4.3.106 BFMulAddH: `value1 = op1 :: Zeros{16}`), so a signalling bf16 NaN is
  still signalling in f32 and the operation then quietens it. C's own `(float)` of a
  `__bf16` (libgcc) quietens it already in the conversion; the slice does not follow C there.
- f16, bf16, f32, f64: overflow rounds to infinity.
- f8e4m3 has no infinity. Its NaN is S.1111.111 (one per sign). Anything above 448
  and infinity *saturate* to plus or minus 448; any NaN becomes S.1111.111. Widening
  an f8e4m3 NaN gives sign | 0x7FC00000.
- Literals: `0.1` written where an f16, bf16 or f8e4m3 is expected is rounded once
  from the decimal. A literal so close to a midpoint between two values that the
  compiler cannot be sure of one rounding is refused, and so is an f8e4m3 literal
  whose size is over 464 (it would only saturate).

## 5. Determinism

One float expression always compiles to one fixed instruction sequence:

- no reassociation: `(a + b) + c` is never computed as `a + (b + c)`;
- fma only where the source writes `fma`: `a * b + c` is a multiply then an add,
  two roundings (the C reference is built with `-ffp-contract=off` for the same reason);
- reductions in source order: a loop that sums an array adds element 0, then 1, and so on.

Compiling the same unit twice gives the same machine code (checked for every gate unit).

## 6. Strict functions

`strict fn f(...)` adds a check after every float operation in f (arithmetic, fma,
sqrt, neg, abs, min, max, conversions and bit casts into a float): if the result is
NaN or infinite the program stops with trap FLOAT (code 15). Parameters, loads,
stores and plain copies are not checked. Without `strict`, NaN and infinity simply
propagate (the default, as Slice A decided: no float traps unless asked for).
f8e4m3 never overflows (it saturates), so in strict code only its NaN traps.

## 7. How the chip does it

All machine words come from our encoder `src/compiler/osc_a64.c` and decode with
its mirror decoder; GNU `as` is used only as an outside check of the words
(`tests/compiler/fp_slice_golden.sh`). Arm Architecture Reference Manual DDI0487 M.d:

| job | instructions (manual section) |
|---|---|
| f16 arithmetic | FADD C7.2.52, FSUB C7.2.201, FMUL C7.2.162, FDIV C7.2.114, FMAXNM C7.2.124, FMINNM C7.2.134, FSQRT C7.2.199, FNEG C7.2.166, FABS C7.2.48, all with ftype 11 (half) |
| fma | FMADD C7.2.120 (H, S, D) |
| compare | FCMP C7.2.70 (H, S, D) |
| float to float | FCVT C7.2.73 (H to S, H to D, S to H, D to H), BFCVT C7.2.14 (S to bf16), FCVTXN C7.2.102 (D to S, round to odd) |
| int to float, float to int | SCVTF C7.2.279, UCVTF C7.2.413, FCVTZS C7.2.106, FCVTZU C7.2.111 (H forms added) |
| moving bits | FMOV (general) C7.2.157 (W to H, H to W added), FMOV (register) C7.2.156 |
| f8e4m3 narrowing | integer RNE on the f32 bits, FCVTNS C7.2.91 for the smallest values |
| memory | f16 / bf16 / f8e4m3 live in the low bits of an 8-byte slot, moved through X registers (LDR / STR immediate) |

f8e4m3 is widened with a trick: its bits placed inside an f16 (S.0EEEE.MMM0000000)
are the same number times 2^-8, so FCVT S, H then a multiply by 256 is exact. It is
narrowed with integer arithmetic on the f32 bit pattern (rebias, add half plus the
kept low bit, shift), FCVTNS for values below the smallest normal, a fixed NaN and
the 448 saturation.

## 8. IR

The three types are scalar tags 15, 16, 17; FMA is opcode 37 and FCHK (the strict
check) opcode 38; trap FLOAT is code 15 (spec/one-layer-format.md). A unit that uses
any of them is IR version 6, the version Slice A floats already took; a unit
without floats keeps its old version and its old bytes (gate G4).

## 9. Gates and evidence

| gate | what it checks | where |
|---|---|---|
| G1 | every operation at every type (+ - * / min max, fma, neg abs sqrt, comparisons, all float to float conversions, int to float and back, array load and store): each emitted word decodes and re-encodes with our decoder; 10,000 random operand triples per operation on native code and on the interpreter, both bit-exact against the C reference `tests/compiler/float_slice_ref.c` (gcc -O0 -ffp-contract=off; f16 as C _Float16, bf16 through libgcc's software conversions, f8e4m3 decoded by its formula and encoded by searching all 256 codes); strict functions trap, the same code without strict propagates | `make test-compiler-float-g1` |
| G2 | 64-element dot products at all five types (one with fma) and a 64-element f32 softmax with an exp written in Omega, bit-identical to the same C | `make test-compiler-float-g2`, kernels `tests/compiler/float_g2.osc` |
| G3 | every existing compiler suite unchanged | `make test-compiler` |
| G4 | float units are version 6; units without floats encode to the same bytes as before | section 9 receipt |

Results on the Spark, 2026-10-20 (receipt
`evidence/OSC-4/receipts/osc4-floats-e12e2647fd89aaec3397c16fd2e0fd5631147a7df98921337889a1214cac9915.json`
at commit b680e09f, from `make osc4-floats-receipt`; box load average 60 at the start, 44 at the end,
from other jobs on the machine, which does not affect bit-exact results):

- **G1 PASS.** 161 operation cases, 10,000 operand triples each: 1,610,000 runs on native
  code and 1,610,000 on the interpreter, every one bit-exact against the C reference; every
  one of the 6,128 emitted words (630 of them float instructions) decodes and re-encodes to
  itself. The sanitizer build repeats it at 1,000 triples per case. Strict section: 28 runs
  trap FLOAT as expected, 50 propagate as expected.
- **G2 PASS.** 7 kernels, 512 runs each (8 seeds, 64 values of k), 3,584 runs on each side,
  bit-identical to C. The two f32 dot products end in different last bits (0xc3aeef41 with a
  multiply then an add, 0xc3aeef40 with `fma`): fma only appears where the source says so.
- **G3 PASS.** `make test-compiler` unchanged apart from the additions: 54 golden programs
  (53 before, plus `progs/fs_slice.osc`), 166 refusal programs (159 before, plus 7 `neg/fs_*`);
  Slice A's own float fuzz gives the same counts as before the slice
  (`units=250 runs=9000 x_finite=1087 x_inf=137 x_nan=276 mismatches=0`); the new slice fuzz:
  `units=250 runs=10500 mismatches=0`, plain and sanitizer builds.
- **G4 PASS.** Against the main line: 47 programs without the new types have identical IR and
  identical machine code; 7 float programs are new, 6 written as IR files are version 6 (one
  passes references between functions and is not written as a file by design). Against
  Slice A: all 53 earlier programs identical, the new one version 6. A negative control
  (one constant changed on the "old" side) makes G4 fail.
- GNU `as` check of 30 float words: PASS; with one golden word altered it fails.

What these results do not say: nothing here is a speed measurement, and nothing ran on
the GPU. The CPU results are from the Spark's Arm cores only.

## 10. Not in this slice

- Vectors and SIMD (several numbers per instruction): later.
- GPU float operations: the sm_121 instruction table, rung 6.
- Rounding modes other than RNE, float exceptions as flags, `strict` per expression.
- f8e5m2 and other FP8 variants.

## 11. Where this differs from OSC-4-DESIGN.md

- The design planned f32 and f64 only; this slice adds f16, bf16, f8e4m3, `fma` and
  `strict fn` (brief L135).
- The float encoder forms live in the OSC encoder `src/compiler/osc_a64.c`, the one
  the compiler uses, not the legacy `aarch64_encoder.c`.
- Three existing test expectations changed on purpose. Two asserted that the
  half-precision forms were refused: "fsqrt type 3 refused" is now "fsqrt type 2
  refused" (type 3 is half precision, now valid), and the FMADD word that the decoder
  used to refuse now decodes (FNMSUB is checked as refused instead). The third:
  `tests/compiler/neg/st_capacity_fields.osc` named its 17th struct field `f16`, which is
  now a type name; the field is renamed `g16` and the test still checks the same
  17-field capacity refusal.
- `test-compiler-full` is no longer only an alias of `test-compiler`: it adds G1 and G2.
