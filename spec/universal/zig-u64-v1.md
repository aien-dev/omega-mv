# Zig 0.17 u64 wrapping profile v1 (#740, G3)

Status: experimental, author-tested (TESTED), not QUALIFIED. Profile `zig-u64-wrapping-pure-v1`
(source-aware v2: OMSA version 2, language 6 Zig 0.17, profile 8, runtime 0, ABI 1, op dialect 6,
arithmetic obligations 1). Additive to `translation-v1.md`, `translation-compose-v1.md` and
`translation-compose-v2.md`; S1 semantics (integers modulo 2^64, ops 1/2/3/4/9) are unchanged. Zig is
used only as a test oracle; both adapters are handwritten seeded Omega.

## 1. Accepted source

The whole input is exactly one function declaration:

```
source  := ws "pub" ws "fn" ws NAME ws "(" ws [param ("," ws param)*] ")" ws "u64" ws "{" ws "return" ws expr ws ";" ws "}" ws EOF
param   := (NAME | "_") ws ":" ws "u64" ws
expr    := term (("+%" | "-%") ws term)*             (S1 ops 2, 3; left-associative, Zig precedence 60)
term    := primary ("*%" ws primary)*                 (S1 op 4; left-associative, Zig precedence 70)
primary := PARAM | LITERAL | "@as" ws "(" ws "u64" ws "," ws LITERAL ws ")" | "(" ws expr ws ")"
LITERAL := "0" | [1-9][0-9]*                          (value at most 2^64-1)
```

(`ws` is optional whitespace and comments; tokens are separated as in Zig, so `return(a)` is accepted.)

- `pub fn` only. A non-`pub` function cannot be referenced from another file, which is how the oracle
  harness (and any caller) uses the unit; `export`, `extern`, `inline`, `noinline` and `callconv` refuse.
- 0..6 parameters, all `u64`. A parameter is a name or the discard `_`; every parameter (named or `_`) is
  one input slot in declaration order. A named parameter must be referenced at least once: Zig rejects an
  unused named parameter, so the profile writes `_` for an unused input. `_` is not a value (Zig rejects
  it in an expression). Named parameters are distinct and different from the function name (Zig rejects
  a parameter that shadows the declaration).
- `NAME` (function and parameters): at most 32 ASCII bytes, first byte `a`..`z`, then `[A-Za-z0-9_]`; not a
  Zig keyword (`addrspace align allowzero and anyframe anytype asm break callconv catch comptime const
  continue defer else enum errdefer error export extern fn for if inline noalias noinline nosuspend opaque
  or orelse packed pub resume return linksection struct suspend switch test threadlocal try union
  unreachable var volatile while`, and the retired `async await usingnamespace`), not `main`, not a Zig
  primitive (`anyerror anyframe anyopaque bool c_char c_short c_ushort c_int c_uint c_long c_ulong
  c_longlong c_ulonglong c_longdouble comptime_float comptime_int f16 f32 f64 f80 f128 false isize
  noreturn null true type undefined usize void` and every `i<digits>` / `u<digits>` integer type name, as
  in `std/zig/primitives.zig`; so `i0` is refused, Zig rejects it as a shadowed primitive). The function
  name is `calc` or `omega_` followed by at least one identifier byte. Nothing is renamed.
- Operators: only the wrapping operators `+%`, `-%`, `*%`, each with whitespace on both sides or on
  neither (Zig's parser rejects whitespace on one side only, `std/zig/Parse.zig`; whitespace for this
  rule is space, tab, LF, CR). `*%` binds tighter than `+%` and `-%`; all are left-associative.
  Parentheses group only; they produce no operation.
- Literals: decimal only, no `_` separators, no `0x`/`0o`/`0b` prefixes, no leading zero (Zig rejects it),
  no fraction or exponent, value at most 2^64-1. `@as(u64, N)` with a literal `N` is the same constant
  (op 1) as `N`; any other `@as` argument or type refuses.
- Typing rule (comptime_int): a bare literal is an untyped `comptime_int` in Zig. A wrapping operation
  with at least one `u64` operand (a parameter, an `@as(u64, N)`, or another operation) coerces the literal
  to `u64` and is computed modulo 2^64. A wrapping operation whose two operands are both bare literals is
  evaluated by Zig exactly at compile time with no wrapping (oracle-checked on Zig 0.17.0: `2 *% 3 +% 4`
  is 10, `(0 -% 1) +% 2` is 1 with the intermediate -1, `0 -% 1` and `18446744073709551615 +% 1` are
  compile errors); it is not modulo 2^64 and refuses 68. Write `@as(u64, 2) *% 3`. Consequently every
  accepted operation is a `u64` operation. A bare literal as the whole return expression is coerced to the
  return type `u64` and is a plain constant.
- The body is exactly `return <expr>;`: no other statement, no locals, no blocks, no `_ = x;` discards.
- Whitespace outside comments is space, tab, LF and CR. Ordinary line comments `//` (including `////...`)
  are retained as source bytes; doc comments `///` (not followed by `/`) and `//!` refuse. Comment bytes
  are those the Zig 0.17 tokenizer admits: 0x20..0x7E, UTF-8 sequences, and CR only immediately before LF;
  tab, other control bytes, DEL and a bare CR inside a comment refuse (Zig rejects them). Zig has no block
  comments. Code outside comments is ASCII.
- The whole input is strict UTF-8 (no overlong form, surrogate or code point above U+10FFFF), contains no
  NUL and has no byte-order mark (Zig accepts a BOM; the profile keeps one spelling). At most 4096 bytes.

### Why plain `+ - *` refuse

Zig's `a + b`, `a - b`, `a * b` on `u64` have checked illegal behavior on overflow: a panic in Debug and
ReleaseSafe builds and undefined behavior in ReleaseFast/ReleaseSmall. They are not modulo 2^64 in any
mode. Only `+%`, `-%`, `*%` express S1 (`source-aware-v2.md` section 2), so plain operators refuse 68 and
are never imported as wrapping. The self-test builds every original and exported program in Debug and in
ReleaseSafe and runs the boundary vectors, and first proves the control: the same harness with `a + b`
panics with `integer overflow` on `(2^64-1) + 1` in both modes, while `a +% b` returns 0.

## 2. Refusals (no stdout, named stderr `ZIG_IMPORT_*`)

| status | name | cases |
|---|---|---|
| 65 | SYNTAX | NUL; missing `;`; empty body; `return;`; trailing comma in the parameters or in `@as`; operator whitespace on one side only (also `+%//`); unbalanced parenthesis; missing operand; `;;` |
| 66 | UNSUPPORTED_TYPE | parameter or return type other than `u64` (`u32`, `i64`, `usize`, `comptime_int`, `*u64`, `?u64`, `[2]u64`); `@as` to another type; a primitive type or value used as an operand (`u64`, `true`) |
| 67 | LIMIT | over 4096 bytes; over 6 parameters; over 64 upper operations (inputs and the synthetic pair included); parenthesis depth over 16 (grouping and `@as` parentheses together); literal above 2^64-1 |
| 68 | UNSUPPORTED_ARITHMETIC | plain `+ - *`; `/ % << >> & \| ^ ~ !`, unary `-` and `-%`, saturating `+\| -\| *\| <<\|`, `++ **`, comparisons, compound assignment; an operation on two bare literals (comptime_int, exact); literal spelling outside section 1 |
| 69 | UNSUPPORTED_CONSTRUCT | non-`pub`, `export`, `extern`, `inline`, `callconv`, `comptime`/`noalias` parameters, error unions `!u64`, statements and locals (`const`, `var`, `_ = x;`, a second `return`), control flow (`if`, `switch`, `while`), `try`, `catch`, `orelse`, `comptime`, `unreachable`, error values, builtins other than `@as(u64, N)`, `@"..."` identifiers, calls, field access (`std.math`), labels and blocks, `test`, `usingnamespace`, imports, a second declaration, `_` as a value, character literals, non-ASCII outside comments |
| 70 | UNKNOWN_IDENTIFIER | an identifier that is not a parameter, keyword or primitive |
| 71 | UNSUPPORTED_SIGNATURE | function name rules; parameter name rules (keyword, primitive such as `i0`/`u8`/`bool`, uppercase or `_` first byte, over 32 bytes); parameter shadows the function name; duplicate parameter; unused named parameter |
| 72 | UNSUPPORTED_SOURCE_FORM | invalid UTF-8; byte-order mark; doc comments; a comment byte Zig rejects |
| 77 | IO | read or write failure |

Public dispatch (`tools/adapters/zig-import`) adds 64 BAD_COMMAND (any argument) and 78 for a missing or
stale build (source or artifact hash differs from `build/L740/build.json`).

## 3. Imported unit

A source-aware v2 OMSB envelope of the Zig member (`translation-compose-v2.md` section 1): OMSA version 2,
header language 6, profile 8, dialect version 1, runtime 0, ABI 1, the #625 contract digest; every operation
dialect 6, version 1, type 1, effects 0, alias 0. Input op 9 per parameter in declaration order (slot =
position, `_` included; attribute 0, obligations 1); constant op 1 (attribute 1, obligations 0); ops 2/3/4
(attribute 1 modulo 2^64, obligations 1). Operations are emitted in source order with operands before
their user, exactly as the C importer orders the equivalent C expression, so the same expression imported
from C and from Zig has the same canonical form N (checked for all 35 F1 positives).

Normalization pair: **yes**. Like the C importer, the Zig importer always appends `const 0` and
`add(root, that const)` (both with the empty span [0,0]) per `translation-v1.md` section 4, for the same
reason as the Rust profile: a body that is a bare parameter other than the last one has no other
representation in which the root is the last result, and with the pair `N(import_Zig(s_Z)) =
N(import_C(s_C))` holds for every F1 expression.

Sidecar OMSP v1, encoding 1 (strict UTF-8), exact source bytes. Spans: an input covers its parameter name
(or `_`), a constant its decimal literal token (inside `@as(...)` too), an arithmetic op its operator token
(`+%`, `-%`, `*%`); spans are ASCII tokens and therefore on code point boundaries.

## 4. Export

`tools/adapters/zig-export` (Omega source `src/adapters/zig-u64-export.om`, prepended with the unchanged
vendor SHA256 and the #621 v2 codec `src/source-aware/core-v2.om`) reads one OMSB envelope on stdin,
validates it with the v2 `decode` (sidecar framing, hashes, spans and UTF-8 included), and accepts every
member of the extended family (`translation-compose-v2.md` section 1): C (1/1), Rust (2/2), Python (3/4,
arithmetic obligations 5), C++ (5/7), Zig (6/8), Go (7/9), runtime 0, ABI 1, ops 1/2/3/4/9 with the S1
fields, input slots a permutation of 0..n-1. Output is generated from the operation records only; the
sidecar is never read for output, so a semantic-only envelope and every member retargeting R(U, M)
export the same bytes.

```
pub fn calc(x0: u64, _: u64, ...) u64 {
    return <expr>;
}
```

followed by the final LF (three lines; `pub fn calc() u64 {` for 0 inputs). Parameters are listed for every
input slot in slot order: a slot reached from the root is named `x<slot>`, every other slot is `_` (Zig
rejects unused named parameters; `i<slot>` would shadow Zig's primitive `i0`..`i5`). `<expr>`: input slot
s is `x<s>`; a constant is its decimal value; ops 2/3/4 are `<A> +% <B>`, `<A> -% <B>`, `<A> *% <B>` with
single spaces. Parentheses only where Zig precedence and left associativity require them to keep the
exact SSA tree (the C exporter rule): precedence leaf 3, `*%` 2, `+%`/`-%` 1; a left operand gets
parentheses iff its precedence is lower than the parent's, a right operand iff lower than or equal. An
operation whose two operands are both constants writes its left operand as `@as(u64, N)`, so no
operation is on two comptime_int literals (section 1); every other constant is bare. Shared SSA results
are expanded as trees. The root is the pre-normalization root of a NORMALIZED unit, otherwise the last
result (`translation-v1.md` section 4); a second, source-written `+% 0` is never stripped.

The output must re-import under section 1: at most 4096 bytes, parenthesis depth at most 16 (grouping
and `@as` parentheses), at most 64 operations after re-import (all input slots + expanded tree + 2).
Otherwise it refuses 67 with no stdout. Other refusals: 65 malformed envelope (any v2 decode failure
other than 68, or more than 8192 bytes); 68 PROFILE_MISMATCH (decode status 68, or a valid unit outside
the family: profiles 3/5/6, division op 5 in any member, other ops or fields, slot gaps); 77 IO; public
dispatch 64 (any argument) and 78 (stale or missing build). The function name is always `calc`.

## 5. Toolchain and commands

Both adapters are compiled by the bridge-seed compiler `191b776ef98821597c3e30655da6d680a9b66edbc9ceaca3c39c61ea6df098e9`
(bridge commit `0fc7d8662cb799e7c206b17c18ea4c3ec2b8fea9`), built from source by `tools/source-aware/build`,
the compiler of the reviewed C importer and the #621 v1/v2 codec. `tools/adapters/build-zig-u64.py` writes
`build/L740/{zig-import,zig-export,build.json}` with the compiler, input and binary hashes; the public
commands check them before dispatch. `make -f mk/zig-u64.mk test-zig-u64` builds every in-tree prerequisite
first (`mk/source-aware.mk`, `mk/source-aware-v2.mk` for the v2 codec `build/L832/sa`, `mk/c-u64-import.mk`,
`mk/c-u64-export.mk`, `mk/lower.mk`, and `mk/retarget.mk` when the tree has it) and uses no other checkout.

Oracles: Zig 0.17.0 from `$ZIG` or `zig` on PATH (build-exe `-O Debug` and `-O ReleaseSafe`, harness I/O
through libc `scanf`/`printf`, `-lc`; missing Zig = NOT_RUN, exit 3); `cc` (gcc 13.3.0) `-std=c17 -O2 -Wall
-Wextra -Werror` for C sources; native realization via the in-tree G1 lowering (`tools/adapters/lower`, omc
`734f0a39` from `$OMEGA_NATIVE_OMC`, omega-bridge `native.om` at `012860db` from `$OMEGA_BRIDGE_REPO`). The
self-test records the Zig version and binary sha256. No default points outside the repository.

```sh
export SA_SEED_REPO=/abs/omega-bridge OMEGA_BRIDGE_REPO=/abs/omega-bridge   # hold 0fc7d866 and 012860db
export OMEGA_NATIVE_OMC=/abs/omc ZIG=/abs/zig                              # omc 734f0a39; Zig 0.17.0 (or zig on PATH)
make -f mk/zig-u64.mk test-zig-u64
tools/adapters/zig-import < calc.zig > calc.upper
tools/adapters/zig-export < calc.upper > exported.zig
```

## 6. Evidence and limits

`tests/universal/translation/zig_selftest.py` (fixtures `fixtures-f2-zig.json`: all 35 F1 positives in this
profile, `not_in_profile` empty, plus Zig refusal fixtures) checks per fixture: import (byte-deterministic);
an independent decoder (header, fields, sidecar binding, spans); v2 codec `sa identity|decode|replay` when
`build/L832/sa` exists (otherwise NOT_RUN); N equality with the C import of the F1 source; export of the
unit and of R(U, M) semantic-only for all six members (identical bytes; v1 members validated by the v1
codec, v2 members by the v2 codec); re-import identity; Zig Debug and ReleaseSafe on the original and the
exported Zig, `cc` on the F1 C, compared with a bigint evaluation of the records on the boundary vectors of
`translation-v1.md` section 3 (all pairs for two inputs) and seeded random vectors. Also: import and
export boundary controls, exporter refusals, public-command closure, and the compositions C -> Zig (every
F1 positive and the four accepted C import boundary controls) and Zig -> C (`export_C(R(U, C))` through the
G1 C exporter, then the C importer), each with the native realization lower(N(U_A)). Every R(U, M) is
computed by `tools/adapters/retarget` when it is in the tree or given (`--retarget`, `ZIG_SELFTEST_RETARGET`),
and each tool output must equal the test-code R byte for byte; without the tool, the test-code R is used
alone. Finite DIFFERENTIAL evidence, not PROOF.
