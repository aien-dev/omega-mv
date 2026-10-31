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
