# Translation profile S2 v1: signed i64, comparisons, select (#624, PROPOSED)

Status: PROPOSED 2026-10-26 by the #624 integration coordinator. Not frozen; no tool implements it.
Additive: `translation-v1.md`, `translation-compose-v1.md`, `translation-compose-v2.md`,
`source-aware-v1.md` and `source-aware-v2.md` are unchanged and every S1 unit, identity and status stays
byte-identical. Freezing this file needs one review pass from the #621 codec owner (envelope rows of
section 2) and the C and Rust lane owners (section 4); the freeze replaces this status line and nothing
else.

Purpose: the smallest widening that real C -> Rust ports need first (aien-dev/babel). It stays pure and
bounded: no loops, recursion, memory, calls or effects, so every S2 unit is a straight-line DAG with
selection, and evaluation always terminates (the PostScript -> PDF lesson). Order of work: C import and
Rust export first, then the other ten directions; S2 is QUALIFIED only when all 12 directions and 30
compositions pass independent validation, and S3 (arrays/slices) does not start before that.

## 1. Semantic profile S2

One pure function of 0..6 inputs, each `u64` or `i64`, returning `u64`, `i64` or `bool`. Values:

| type | values |
|---|---|
| u64 (type 1, as S1) | integers 0..2^64-1 |
| i64 (type 5, new) | integers -2^63..2^63-1, two's complement bit pattern in a constant's u64 immediate |
| bool (type 6, new) | false, true; not an integer (no arithmetic on it, no implicit conversion) |

Operations (S1's 1..4 and 9 unchanged; new ones listed):

| op | meaning | operand types -> result |
|---|---|---|
| 1 | constant | u64 / i64 / bool (immediate 0 or 1 for bool) |
| 2/3/4 | add/sub/mul | (u64,u64)->u64 attr 1; (i64,i64)->i64 attr 5 or 6 |
| 10..15 | eq, ne, lt, le, gt, ge | (T,T)->bool, T is u64 or i64; i64 compares signed, u64 unsigned |
| 16 | select c ? a : b | A = bool c; B = a; immediate = earlier SSA index of b (op 8 precedent); a, b same type |
| 17 | not | bool -> bool |
| 9 | input | u64 or i64 |

Arithmetic attributes for i64 (new):

- 5 = wrapping: the result is the exact result reduced to -2^63..2^63-1 modulo 2^64. Total.
- 6 = overflow-is-error: defined only when the exact result is in range; obligation 32
  (no-signed-overflow) is attached. Outside the range the source language's behavior is an error (C/C++
  undefined behavior; Rust panic in debug and wrap in release; Zig checked illegal behavior). Any total
  realization that equals the exact result whenever it is in range is a REFINEMENT of attr 6 and is
  labelled PROVEN_REFINEMENT only with a proof, BOUNDED_VALIDATION otherwise.

No signed/unsigned mixing, casts, division, remainder, shifts, bitwise operators, short-circuit `&&`/`||`
(they are selects and come with S2.1), loops, early returns or statements other than the single return
expression.

## 2. Envelope rows (source-aware v3, #621 codec owner)

One new OMSA semantic header version value, 3, for S2 profiles only; v1/v2 units are unchanged. New
registry rows: type 5 i64, type 6 bool; attributes 5 and 6; obligation bit 32; ops 10..17 as above with
effects 0, alias 0; op 16 B/immediate rule as in section 1. Profiles (one per language):

| profile | name | language |
|---|---|---|
| 10 | c17-int64-select-v1 | 1 |
| 11 | rust-i64-select-v1 | 2 |
| 12 | python-i64-select-v1 | 3 |
| 13 | cpp17-int64-select-v1 | 5 |
| 14 | zig-i64-select-v1 | 6 |
| 15 | go-i64-select-v1 | 7 |

`retarget` and the N(U) neutral form extend as in compose-v2 section 2: N(U) is the profile 10 unit.

## 3. Source forms each importer must establish (before emitting a record)

| language | i64 attr 5 (wrapping) | i64 attr 6 (overflow-is-error) | compare / select |
|---|---|---|---|
| C17 | `(int64_t)((uint64_t)a + (uint64_t)b)` exact shape (GCC 13 pinned: conversion is modulo 2^64, implementation-defined in C17) | `a + b` on two `int64_t` | `a < b` etc. only as the condition of `?:`; `c ? x : y` |
| Rust | `a.wrapping_add(b)` | `a + b` on `i64` | `if c { x } else { y }`; comparison operators |
| Python | `((a + b + 2**63) & (2**64-1)) - 2**63` exact shape | `_ck(a + b)` with the profile's range-check helper | `x if c else y` |
| C++17 | as C with `std::int64_t` and `static_cast` | as C | as C |
| Zig | `a +% b` on `i64` | `a + b` on `i64` | `if (c) x else y` |
| Go | `a + b` on `int64` (always wrapping, language spec) | never produced | Go has no `?:`; a single `if c { return x }; return y` form |

## 4. Exporters

Each exporter emits plain source in the form above for every attribute it can express. For attr 6 the
exporters emit the language's own overflow-is-error form where one exists (C/C++ plain `+`, Rust plain
`+`, Zig plain `+`, Python `_ck`). Go export of attr 6 emits plain `+` (wrapping), recorded as a
REFINEMENT, never as equality. Round trip: import(export(U)) must have U's semantic identity except
where this table maps two attributes to one source form (Go attr 6 -> attr 5 on re-import), which is
recorded per fixture, not hidden.

## 5. Lowering and realization

`lower` emits `long` (i64) values in restricted Omegese; the pinned omc realizes 64-bit `long` add, sub
and mul as wrapping (seed/omc/omc.om at f8588e10, `bo_fix`: type 9 takes no overflow check) and
compares signed with `lt`/`le`/`gt`/`ge` on `long`. A realization of attr 6 is therefore a REFINEMENT.
Bindings: omega-bridge tools/native must carry `int64_t` and a `bool`-as-`uint64_t` 0/1 result; that is
a bridge change (ABI v1 has `uint64_t` only), owned by the bridge lane.

## 6. Required tests per direction (the S1 set, widened)

Positive; boundary (i64 -2^63, -1, 0, 1, 2^63-1 per input, all compare outcomes, both select arms);
overflow (attr 5 wraps; attr 6 at exactly the boundary and one past it, where the C oracle is built with
`-fsanitize=signed-integer-overflow -fno-sanitize-recover` and the Rust oracle in debug, both must
report the error); malformed; unsupported feature (each excluded construct in section 1 refuses with its
exit code); mutation (attr 5 <-> 6, signed <-> unsigned compare, select arm swap, operand swap of
non-commutative ops, constant sign flip). Oracles: REFERENCE on Python bigints; DIFFERENTIAL against gcc
13.3.0, rustc 1.98.1, CPython 3.12.3, g++ 13.3.0, Zig 0.17.0, Go 1.27.3.

## 7. Amendments (universal/624-s2-impl, BabelS2, 2026-10-26)

Marked amendments made while implementing this profile. Each is the smallest choice consistent with the
text above; the self-test `tests/universal/translation/s2_selftest.py` (`mk/s2.mk test-translation-s2`)
exercises every one.

- **A1 (corrects section 5).** The pinned omc 734f0a39 does NOT wrap `long` add/sub/mul: a signed
  overflow at run time exits with trap status 1 (observed on the native caller). `lower-s2` therefore
  computes every attr 5 and attr 6 operation with wrapping `ulong` arithmetic on the two's-complement bits,
  and keeps a poison bit per value: an attr 6 op ORs its own overflow predicate into poison, a select ORs
  its condition's poison with the chosen arm's poison, and a poisoned root executes `trap 1`. attr 6 is
  thus realized exactly (trap iff the C/Rust oracle reports overflow on the demanded path), not refined.
- **A2.** Select is demand-driven, as C `?:` and Rust `if`: overflow in the arm not taken is not an
  error. The reference evaluator, the importer, the exporter and the lowering all follow this.
- **A3 (records attributes and obligations).** i64 and bool constants attr 0, u64 constants attr 1;
  compare, select and not attr 0 with obligations 0; inputs attr 0 with obligations 1; attr 5 ops
  obligations 1; attr 6 ops obligations 33.
- **A4 (C importer grammar, section 3).** Parameters and result are `int64_t` only (`uint64_t`, `int`
  and other types refuse 66). Literals are `0LL` or `[1-9][0-9]*LL` up to 2^63-1 (larger refuses 67).
  Unary minus, `!`, casts, a comparison outside a `?:` condition, chained comparisons and `<<`/`>>`
  refuse 68; a lone `=` refuses 69.
- **A5.** An imported result that would otherwise be a bare input is normalized by the pair const 0
  (type 5, attr 0) followed by add (type 5, attr 5, obligations 1); exporters strip the pair.
- **A6 (corrects section 5, bindings).** No bridge change: ABI v1 passes an i64 argument and result as its
  `uint64_t` two's-complement bits, and a bool root is returned as `uint64_t` 0/1. The C caller converts.
- **A7.** Oracle versions as run: rustc 1.98.1 (48a229cea 2026-09-01); gcc 13.3.0
  (Ubuntu 13.3.0-6ubuntu2~24.04.1).
- **A8 (function names, requested by consumer aien-dev/babel).** The function name is not part of the
  semantic unit (identity is unchanged by it); it appears only in the provenance source text.
  - `c-i64-import`: the function may have any C identifier of at most 63 bytes that is not a C keyword
    (C17 and C23, plus `int64_t`, `uint64_t`, `main`), not a C17 7.1.3 reserved identifier (`__x`, `_X`),
    not a `*_t` typedef name and not an all-upper-case `*_MAX`/`*_MIN`/`*_WIDTH`/`*_C` `<stdint.h>` macro
    name; otherwise 71. Parameter names keep the section 3 rule.
  - `rust-i64-export [NAME]`: optional NAME, default `calc`; a C identifier of at most 63 bytes that is
    not a Rust keyword (strict, reserved or 2024 edition `gen`) nor `_`; otherwise 64. The dispatcher
    frames it exactly as `c-export` does (one length byte, the name, then the envelope).
  - `lower-s2 [NAME]`: optional NAME with the S1 `lower` rule (not an Omegese reserved word); otherwise
    64. The bridge symbol becomes `omega_<NAME>`.
  S1 tools are unchanged.
