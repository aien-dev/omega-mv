# Translation composition contract v1 (#624 G2)

Status: FROZEN for G2 (C, Rust, Python) on 2026-10-25 by the #624 integration coordinator. Additive to
`translation-v1.md`, which stays unchanged and governs G1. Envelope v1 (`source-aware-v1.md`) unchanged.
C++, Zig and Go need new registry codes (a v2 envelope owned by the #621 codec lane); they are not
covered here.

## 1. The S1 family

The u64 pure profiles already in the v1 registry share one operation set and one semantics (integers
modulo 2^64, ops 1/2/3/4/9, `translation-v1.md` section 2):

| member | header language / profile / runtime / ABI | op dialect byte | arithmetic (ops 2/3/4) obligations |
|---|---|---|---|
| C | 1 C17 / 1 c17-uint64-pure-v1 / 0 / 1 | 1 | 1 |
| Rust | 2 Rust2021 / 2 rust-u64-wrapping-pure-v1 / 0 / 1 | 2 | 1 |
| Python | 3 Python3 / 4 python-explicit-u64-pure-v1 / 0 / 1 | 3 | 5 (input-domain + source-explicit-masking) |

Inputs (op 9) and constants (op 1) have the same fields in every member (`translation-v1.md` section 2).
Profile 3 (CPython bigint) and 6 (dynamic retained) are not in the family: their semantics differ, so
they are never retargeted to a u64 member. An exporter given them refuses 68.

## 2. Canonical form N and retargeting R

`R(U, M)` rewrites a family unit U to member M: the header language and profile bytes, every op's
dialect byte, and the arithmetic obligations per the table. Nothing else changes: same record count,
order, operands, immediates, input slots and parent-semantics hash. `N(U) = R(U, C)`; the
language-neutral semantic identity of U is `sha256(N(U) semantic bytes)`. R is total on the family and
`N(R(U, M)) = N(U)` for every member M.

Every exporter accepts any family unit, so retargeting is not a separate tool. Its output is the source
of its own language generated from the records; the source language of the input is ignored. Every
importer emits units of its own member only.

## 3. Normalization pairs

Each importer declares in its profile spec whether it appends a normalization pair (`translation-v1.md`
section 4; the C importer always does). `strip(U)` removes one trailing NORMALIZED pair iff the
importer that produced U declares one. An exporter emits the expression of the pre-normalization root
iff U is NORMALIZED (`translation-v1.md` section 4), whatever the member.

## 4. Directions and compositions

An adapter direction is (language, import) or (language, export): 12 for 6 languages, 6 in G2 (C, Rust,
Python x import/export). A directed composition A -> B (A != B) is: `U_A = import_A(src)`;
`src_B = export_B(U_A)`; `U_B = import_B(src_B)`. 30 for 6 languages; 6 in G2. Required evidence per
composition and fixture:
1. Identity: `N(strip(U_B)) = N(strip(U_A))` byte for byte.
2. DIFFERENTIAL: the original A program (compiled or run by A's declared oracle), the reference model
   on U_A, the generated B program (by B's oracle) and the native realization of U_A
   (`translation-v1.md` section 5 lowering, after R to C if the lowering accepts only C) agree on the
   section 3 vectors.
3. Generated, not replayed: `src_B` is exported from the records; a semantic-only envelope gives the
   same bytes.
Same-language round trips (A -> A) are reported separately and are not counted among the 30.

## 5. Fixture set F2

F2 = the F1 expressions (`fixtures-f1.json`) rewritten by each source lane into its own language, with
the same input count and the same expected values; IDs are `F2-<lang>-<F1 id>`. A lane that cannot
express an F1 expression in its profile records it as `NOT_IN_PROFILE` with the reason; it is not
dropped silently. Lanes own `tests/universal/translation/fixtures-f2-<lang>.json`.

## 6. Ownership (adds to translation-v1.md section 9)

| lane | paths |
|---|---|
| Rust (#626) | `src/adapters/rust-u64-import.om`, `src/adapters/rust-u64-export.om`, `tools/adapters/rust-import`, `tools/adapters/rust-export`, `tools/adapters/build-rust-*.py`, `mk/rust-u64.mk`, `spec/universal/rust-u64-v1.md`, `tests/universal/translation/rust_selftest.py`, `tests/universal/translation/fixtures-f2-rust.json` |
| Python (#627) | the same pattern with `python` |
| C export family extension (#622) | `src/adapters/c-u64-export.om` (accept the whole family, section 2) |
| composition validation (#623) | `tests/universal/translation/validate_compose*.py` |
