# Rust 2021 u64 wrapping profile v1 (#626, G2)

Status: experimental, author-tested (TESTED), not QUALIFIED. Profile `rust-u64-wrapping-pure-v1`
(source-aware v1 language 2 Rust2021, profile 2, runtime 0, ABI 1, op dialect 2). Additive to
`translation-v1.md` and `translation-compose-v1.md`; S1 semantics (integers modulo 2^64, ops 1/2/3/4/9)
are unchanged. Rust is used only as a test oracle; both adapters are handwritten seeded Omega.

## 1. Accepted source

The whole input is exactly one function item:

```
source  := ws ["pub" ws] "fn" ws NAME ws "(" ws [param ("," ws param)*] ")" ws "->" ws "u64" ws "{" ws expr ws "}" ws EOF
param   := NAME ws ":" ws "u64" ws
expr    := primary ("." ws METHOD ws "(" ws expr ws ")" ws)*
primary := PARAM | LITERAL | "(" ws expr ws ")" | "u64" ws "::" ws METHOD ws "(" ws expr ws "," ws expr ws ")"
METHOD  := "wrapping_add" | "wrapping_sub" | "wrapping_mul"           (S1 ops 2, 3, 4)
LITERAL := ("0" | [1-9][0-9]*) "u64"                                  (value at most 2^64-1)
```

- 0..6 parameters, all `u64`, distinct. The receiver of a method call is the left operand, the argument
  the right operand; `u64::wrapping_add(x, y)` (and sub/mul) is the same operation as `x.wrapping_add(y)`.
  Parentheses group only; they produce no operation.
- `NAME` (function and parameters): at most 32 ASCII bytes, first byte `a`..`z`, then `[A-Za-z0-9_]`; not a
  Rust keyword (strict, reserved and weak: `as break const continue crate else enum extern false fn for if
  impl in let loop match mod move mut pub ref return self static struct super trait true type unsafe use
  where while async await dyn abstract become box do final macro override priv typeof unsized virtual
  yield try union gen safe raw auto default macro_rules`), not `main`, not a primitive type name (`u8 u16
  u32 u64 u128 usize i8 i16 i32 i64 i128 isize f16 f32 f64 f128 bool char str`). The function name is `calc`
  or `omega_` followed by at least one identifier byte. Other names refuse; nothing is renamed.
- Literals: decimal only with the exact suffix `u64`, no `_` separators (neither `1_000u64` nor `1_u64`),
  no hexadecimal/octal/binary prefixes, no leading zero (`010u64` is decimal 10 in Rust but has a second
  spelling, so it refuses, as in the C profile), no other suffix, no unsuffixed literal.
- `::` and `->` are single tokens (no inner whitespace). Trailing commas in the parameter list or in
  calls refuse (one spelling). The body is exactly one tail expression: no `return` (anywhere), no
  statements, no `let`, no blocks.
- Whitespace is space, tab, LF and CR. Ordinary comments are retained as source bytes: line comments `//`
  (but not `///` followed by a byte other than `/`, and not `//!`) and block comments `/* */`, which nest as
  in Rust. Doc comments (`///`, `//!`, `/**` not followed by `*` or `/`, `/*!`) are attribute sugar and
  refuse. Comment text may be any valid UTF-8; code outside comments is ASCII.
- The whole input is strict UTF-8 (no overlong form, surrogate or code point above U+10FFFF), contains no
  NUL and has no byte-order mark. At most 4096 bytes.

### Why plain `+ - *` refuse

Rust's `a + b`, `a - b`, `a * b` on `u64` panic on overflow when `overflow-checks` is on (the default of
debug builds) and wrap when it is off (the default of release builds); the meaning of the source depends
on the build profile, not on the text. S1 is total modulo 2^64, which only the `wrapping_*` methods
express in every build mode. Plain operators therefore refuse 68 instead of being imported as wrapping.
The self-test compiles every original and exported program with `-C overflow-checks=on`, so a plain
operator that slipped through would panic on the boundary vectors.

## 2. Refusals (no stdout, named stderr `RUST_IMPORT_*`)

| status | name | cases |
|---|---|---|
| 65 | SYNTAX | NUL; unterminated (also nested) block comment; missing tail expression; trailing comma; wrong arity; missing separator |
| 66 | UNSUPPORTED_TYPE | parameter or return type other than `u64` (`i64`, `usize`, `Wrapping<u64>`, `&u64`, ...); a primitive path other than `u64` in the body (`i64::wrapping_add`) |
| 67 | LIMIT | over 4096 bytes; over 6 parameters; over 64 upper operations (inputs and the synthetic pair included); parenthesis depth (grouping and call parentheses together) over 16; literal above 2^64-1 |
| 68 | UNSUPPORTED_ARITHMETIC | plain `+ - *`; `/ % << >> & \| ^ ! <  >`, unary `-`/`*`, `as`, compound assignment; any method or associated item other than the three (`wrapping_div`, `wrapping_neg`, `checked_*`, `overflowing_*`, `saturating_*`, `pow`, `u64::MAX`); literal spelling outside section 1 |
| 69 | UNSUPPORTED_CONSTRUCT | `unsafe`, `const fn`, `async`, `extern`, `pub(...)`, attributes `#[..]`/`#![..]`, generics, `where`, turbofish, `<T>::` paths, `let`, `return`, statements, nested items, `use`, macros, control flow, blocks, closures, references, patterns, field access, a second item, non-ASCII outside comments |
| 70 | UNKNOWN_IDENTIFIER | an identifier that is not a parameter, keyword or primitive type (`c`, `std::...`) |
| 71 | UNSUPPORTED_SIGNATURE | function name rules; missing `-> u64`; parameter name rules (incl. `mut`, `self`); duplicate parameter |
| 72 | UNSUPPORTED_SOURCE_FORM | invalid UTF-8; byte-order mark; doc comments |
| 77 | IO | read or write failure |

Public dispatch (`tools/adapters/rust-import`) adds 64 BAD_COMMAND (any argument) and 78 for a missing or
stale build (source or artifact hash differs from `build/L626/build.json`).

## 3. Imported unit

A source-aware v1 OMSB envelope of the Rust member (`translation-compose-v1.md` section 1): header
language 2, profile 2, dialect version 1, runtime 0, ABI 1, the #625 contract digest; every operation
dialect 2, version 1, type 1, effects 0, alias 0. Input op 9 per parameter in declaration order (slot =
position, kept when unused; attribute 0, obligations 1); constant op 1 (attribute 1, obligations 0); ops
2/3/4 (attribute 1 modulo 2^64, obligations 1). Operations are emitted in source order with operands
before their user, exactly as the C importer orders the equivalent C expression, so the same expression
imported from C and from Rust has the same canonical form N.

Normalization pair: **yes**. Like the C importer, the Rust importer always appends `const 0` and
`add(root, that const)` (both with the empty span [0,0]) per `translation-v1.md` section 4. Reason: a
body that is a bare parameter other than the last one (`fn calc(a: u64, b: u64) -> u64 { a }`) has no
other representation in which the root is the last result, and with the pair `N(import_Rust(s_R)) =
N(import_C(s_C))` holds for every F1 expression, so `strip` treats C and Rust units the same way.

Sidecar OMSP v1, encoding 1 (strict UTF-8), exact source bytes. Spans: an input covers its parameter
name, a constant its literal token (`42u64`), an arithmetic op its method name (`wrapping_add`); spans are
ASCII tokens and therefore on code point boundaries.

## 4. Export

`tools/adapters/rust-export` (Omega source `src/adapters/rust-u64-export.om`, prepended with the unchanged
vendor SHA256 and `src/source-aware/core.om`) reads one OMSB envelope on stdin, validates it with the #621
`decode` (sidecar framing, hashes, spans and UTF-8 included), and accepts every S1-family unit: C
(1/1, arithmetic obligations 1), Rust (2/2, 1) and Python (3/4, 5), runtime 0, ABI 1, ops 1/2/3/4/9 with
the S1 fields, input slots a permutation of 0..n-1. Output is generated from the operation records only;
the sidecar is never read for output, so a semantic-only envelope and every member retargeting R(U, M)
export the same bytes.

```
pub fn calc(i0: u64, i1: u64, ...) -> u64 { <expr> }        ("pub fn calc() -> u64 { <expr> }" for 0 inputs)
```

followed by one LF. `<expr>`: input slot s is `i<s>`; a constant is `<decimal>u64`; ops 2/3/4 are
`<A>.wrapping_add(<B>)`, `.wrapping_sub(`, `.wrapping_mul(`. A method call binds tighter than every
other Rust expression form and the receiver of every emitted call is a path, a suffixed literal
(`5u64.wrapping_add(i0)` is valid) or another method call, so no receiver ever needs parentheses and
the exporter emits none. Shared SSA results are expanded as trees. The root is the pre-normalization
root of a NORMALIZED unit, otherwise the last result (`translation-v1.md` section 4); a second,
source-written `.wrapping_add(0u64)` is never stripped.

The output must re-import under section 1: at most 4096 bytes, call-parenthesis depth at most 16 (depth
of `A.m(B)` = max(depth A, depth B + 1)), at most 64 operations after re-import (inputs + expanded tree +
2). Otherwise it refuses 67 with no stdout. Other refusals: 65 malformed envelope (any #621 decode
failure other than 68, or more than 8192 bytes); 68 PROFILE_MISMATCH (decode status 68, or a valid v1
unit outside the family: profiles 3/5/6, other ops, other fields, slot gaps); 77 IO; public dispatch 64
(any argument) and 78 (stale or missing build). The function name is always `calc`.

## 5. Toolchain and commands

Both adapters are compiled by the bridge-seed compiler `191b776ef98821597c3e30655da6d680a9b66edbc9ceaca3c39c61ea6df098e9`
(bridge commit `0fc7d8662cb799e7c206b17c18ea4c3ec2b8fea9`), built from source by `tools/source-aware/build`.
It is the compiler of the reviewed C importer, the #621 codec and the G1 C exporter: the vendored SHA256
and `core.om` compile unchanged under it, and neither adapter needs argv (the native omc `734f0a39` was
not needed). `tools/adapters/build-rust-u64.py` writes `build/L626/{rust-import,rust-export,build.json}`
with the compiler, input and binary hashes; the public commands check them before dispatch.

```sh
export SA_SEED_REPO=/path/to/omega-bridge        # contains 0fc7d866; mk/rust-u64.mk defaults to ~/workspace/omega-bridge
make -f mk/rust-u64.mk test-rust-u64
tools/adapters/rust-import < calc.rs > calc.upper
tools/adapters/rust-export < calc.upper > exported.rs
```

## 6. Evidence and limits

`tests/universal/translation/rust_selftest.py` (fixtures `fixtures-f2-rust.json`: all 35 F1 positives in
this profile, `not_in_profile` empty, plus Rust refusal fixtures) checks per fixture: import; an
independent decoder (header, fields, sidecar binding, spans); #621 `sa identity|decode|replay` from the
reviewed universal-read build; N equality with the C import of the F1 source; export of the unit and of
R(U, C|Rust|Python) semantic-only (identical bytes); re-import identity; `rustc --edition 2021 -O -C
overflow-checks=on` on the original and the exported Rust and `cc` on the F1 C, compared with a bigint
evaluation of the records on the boundary vectors of `translation-v1.md` section 3 (all pairs for two
inputs) and seeded random vectors. Also: import and export boundary controls, the exporter refusals,
public-command closure, and the composition C -> Rust on every F1 positive and the four accepted C
import boundary controls. Rust -> C runs only when a passing G1 C exporter is given (`--c-export`), after
R(U, C), because the G1 exporter accepts C-member units only; otherwise it is reported NOT_RUN. The
native realization through the lowering seam is NOT_RUN here. Finite DIFFERENTIAL evidence, not PROOF.
