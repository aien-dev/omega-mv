# Go 1.27 uint64 profile v1 (#741, G3)

Status: experimental, author-tested (TESTED), not QUALIFIED. Profile `go-u64-pure-v1` (source-aware v2:
OMSA semantic header version 2, language 7 Go 1.27, profile 9, runtime 0, ABI 1, op dialect 7, arithmetic
obligations 1; `source-aware-v2.md` section 2, `translation-compose-v2.md` section 1). Additive to
`translation-v1.md`, `translation-compose-v1.md` and `translation-compose-v2.md`; S1 semantics (integers
modulo 2^64, ops 1/2/3/4/9) are unchanged. Go is used only as a test oracle; both adapters are
handwritten seeded Omega.

## 1. Accepted source

The whole input is one Go source file of package `main` holding exactly one function declaration:

```
file    := "package" "main" ";" "func" NAME "(" [params] ")" "uint64" "{" "return" expr [";"] "}" [";"] EOF
params  := group ("," group)*
group   := PNAME ("," PNAME)* "uint64"
expr    := term (("+" | "-") term)*                       (S1 ops 2, 3; left associative)
term    := primary ("*" primary)*                         (S1 op 4; left associative)
primary := PNAME | LITERAL | "(" expr ")"
LITERAL := "0" | [1-9][0-9]*                              (value at most 2^64-1)
```

- Tokens are separated by spaces, tabs, CR, LF and comments. `;` is the explicit semicolon or one
  inserted by Go's rule: a newline (LF) or the end of input after an identifier other than `package` and
  `func`, a literal, `)`, `]` or `}` is a `;` token. So `return a\n+ b` ends the return statement after
  `a` (then `+ b` refuses 69), `func calc(a uint64\n)` and `(a\n+ b)` refuse 65, while a newline after a
  binary operator (`a +\n b`) or after `{`, `(` and `,` is plain whitespace. CR alone never inserts.
- Package clause exactly `package main`. The file is a translation unit that a caller links with its
  own `func main` (the oracle does exactly that); no other package name, no `import`, no other
  declaration. The function name is `calc` or `omega_` followed by at least one identifier byte.
- Parameters: 0..6, named, distinct, type exactly `uint64`, grouped (`a, b uint64`) or not; the slot is
  the position in declaration order, unused parameters are kept. Names: at most 32 ASCII bytes, first
  byte `a`..`z`, then `[A-Za-z0-9_]`; not a Go keyword, not a predeclared identifier (types `any bool byte
  comparable complex64 complex128 error float32 float64 int int8 int16 int32 int64 rune string uint uint8
  uint16 uint32 uint64 uintptr`, constants `true false iota nil`, functions `append cap clear close complex
  copy delete imag len make max min new panic print println real recover`), not `main` or `init`. Since
  the file declares nothing else, `uint64` always denotes the predeclared type.
- Result: exactly one unnamed `uint64` (not parenthesised). Body: exactly one `return` of one expression;
  an explicit `;` may follow it, nothing else.
- Literals: decimal only, `0` or `[1-9][0-9]*`. Refused spellings (68): legacy octal `010` (octal 8 in
  Go), `0o`/`0x`/`0b` prefixes, `_` separators, float (`2.0`, `1e3`, `.5`) and imaginary (`1i`) constants,
  rune literals. Go converts `1e3` and `'a'` exactly to `uint64`, but they are second spellings of a
  number; the profile keeps one spelling per value as the C and Rust profiles do.
- Comments: line comments `//` and general comments `/* */` (which do not nest) are retained as source
  bytes; a general comment containing a newline acts as a newline for semicolon insertion, as in Go.
  Compiler directives are not ordinary comments and refuse 72: the byte after `//` must be a space, tab,
  CR, LF or the end of input (this excludes `//go:...`, `//go:build`, `//line`, `//export`, `//extern`),
  a line comment whose text starts with `+build` after spaces/tabs refuses (legacy build constraint), and
  the byte after `/*` must be a space, tab, CR, LF or `*` (this excludes `/*line`). Comment text may be any
  valid UTF-8; code outside comments is ASCII.
- The whole input is strict UTF-8 (no overlong form, surrogate or code point above U+10FFFF), contains
  no NUL and has no byte-order mark. At most 4096 bytes.

### 1.1 Untyped constants (the Go-specific rule)

`+ - *` on two `uint64` operands are modulo 2^64 by the Go specification. A Go expression whose operands
are all constants is an untyped constant expression: it is evaluated exactly (arbitrary precision; the gc
type checker rejects results wider than 512 bits) and only its final value is converted to `uint64` where
it meets a `uint64` operand or the result; a value outside 0..2^64-1 is a compile error. So `0 - a` and
`18446744073709551615 + a` wrap at run time, while `a + (0 - 1)` and `return 18446744073709551615 + 1`
do not compile, and `(0 - 1) + 1` compiles to 0 through a negative intermediate.

Rule of this profile: every constant-only node of the expression tree (a literal, or an operation whose
two operands are both constant-only) must evaluate exactly within 0..2^64-1; otherwise the importer
refuses 68. The importer checks every such node as it builds it (unsigned overflow detection on add,
sub and mul).

Why the accepted programs mean the S1 tree: reduction modulo 2^64 is a ring homomorphism, so a
constant-only node's exact value is congruent to its modulo-2^64 tree value; when the exact value lies in
0..2^64-1 the two are equal. Every maximal constant expression is such a node, so its exact value equals
the tree value, fits `uint64` and converts without error; every other operation has a `uint64` operand
and is Go's modulo-2^64 arithmetic. Intermediate constants are within 0..2^64-1 too, far below the 512-bit
limit. Hence every accepted source compiles and computes the S1 tree. The rule is narrower than Go: it
refuses constant expressions with an out-of-range intermediate whose final value fits (`(0 - 1) + 1`,
`18446744073709551615 * 2 - 18446744073709551615 + a`), which Go accepts. Such a source has no
in-range S1 tree, and refusing it keeps the profile free of exact arithmetic beyond 64 bits.

The self-test checks this rule against `go build` (section 6): the refusal fixtures record Go's verdict
(`go_build`), and seeded random trees mixing constants (0, 1, 2, 3, 2^32-1, 2^32, 2^63-1, 2^63, 2^64-2,
2^64-1) and variables check that the importer accepts exactly the trees satisfying the rule, that every
accepted tree compiles and returns the modulo-2^64 value, and that Go's own verdict on every refused tree
equals the modelled Go rule above (maximal constant expressions in range, intermediates within 512 bits).

## 2. Refusals (no stdout, named stderr `GO_IMPORT_*`)

| status | name | cases |
|---|---|---|
| 65 | SYNTAX | NUL; unterminated general comment; missing return value or statement; trailing comma; a `;` inserted where the grammar has none (newline before `)`, inside parentheses, after `return`); package clause not terminated; missing operator or `)`; parameter group without a type |
| 66 | UNSUPPORTED_TYPE | parameter or result type other than `uint64` (`int`, `uint`, `uintptr`, `uint32`, `byte`, `*uint64`, `[]uint64`, ...); a predeclared type name in the body (conversions `uint64(x)`) |
| 67 | LIMIT | over 4096 bytes; over 6 parameters; over 64 upper operations (inputs and the synthetic pair included); parenthesis depth over 16; literal above 2^64-1 |
| 68 | UNSUPPORTED_ARITHMETIC | `/ % & \| ^ &^ << >>`, comparisons, unary `+ - ^ ! & *`, `++`; literal spelling outside section 1; a constant-only node outside 0..2^64-1 (section 1.1) |
| 69 | UNSUPPORTED_CONSTRUCT | `import`, `var`, `const`, `type`, a second declaration, a declaration before the package clause, statements other than the one `return` (`:=`, `var`, `if`, `go`, `defer`, a second statement after an inserted `;`, empty statements), type parameters, method receivers, named, parenthesised or multiple results, `return a, b`, variadic parameters, calls, selectors, strings, predeclared constants and functions in the body (`iota`, `max`), assignment, non-ASCII outside comments |
| 70 | UNKNOWN_IDENTIFIER | an identifier that is not a parameter, keyword or predeclared name (`c`, the function itself) |
| 71 | UNSUPPORTED_SIGNATURE | package name other than `main`; function name rules (`Calc`, `main`, `foo`); parameter name rules (`_`, `A`, a keyword or predeclared name, over 32 bytes); duplicate parameter; missing result type |
| 72 | UNSUPPORTED_SOURCE_FORM | invalid UTF-8; byte-order mark; compiler directives and build constraints in comments |
| 77 | IO | read or write failure |

Public dispatch (`tools/adapters/go-import`) adds 64 BAD_COMMAND (any argument) and 78 for a missing or
stale build (source or artifact hash differs from `build/L741/build.json`). Many refusals are Go compile
errors anyway; others (`1e3`, `010`, `x := a`, directives, ...) are valid Go outside this narrow profile.
The fixture field `go_build` records which (section 6).

## 3. Imported unit

A source-aware v2 OMSB envelope of the Go member: OMSB version 1, OMSA semantic header version 2,
language 7, profile 9, dialect version 1, runtime 0, ABI 1, the #625 contract digest; every operation
dialect 7, version 1, type 1, effects 0, alias 0. Input op 9 per parameter in declaration order (slot =
position, kept when unused; attribute 0, obligations 1); constant op 1 (attribute 1, obligations 0); ops
2/3/4 (attribute 1 modulo 2^64, obligations 1). Operations are emitted in source order with operands
before their user, exactly as the C importer orders the equivalent C expression, so the same expression
imported from C and from Go has the same canonical form N (`translation-compose-v2.md` section 2).

Normalization pair: **yes**. Like the C and Rust importers, the Go importer always appends `const 0` and
`add(root, that const)` (both with the empty span [0,0]) per `translation-v1.md` section 4, so the root is
the last result even for a bare earlier parameter, and `N(import_Go(s_G)) = N(import_C(s_C))` holds for
every F1 expression.

Sidecar OMSP v1, encoding 1 (strict UTF-8), exact source bytes. Spans: an input covers its parameter
name, a constant its literal token, an arithmetic op its operator byte (`+`, `-`, `*`); spans are ASCII
tokens and therefore on code point boundaries.

## 4. Export

`tools/adapters/go-export` (Omega source `src/adapters/go-u64-export.om`, prepended with the unchanged
vendor SHA256 and the #621 v2 codec `src/source-aware/core-v2.om`) reads one OMSB envelope on stdin,
validates it with the v2 `decode` (sidecar framing, hashes, spans and UTF-8 included) and accepts every
unit of the extended S1 family: C (OMSA version 1, language/profile 1/1, arithmetic obligations 1), Rust
(1, 2/2, 1), Python (1, 3/4, 5), C++ (2, 5/7, 1), Zig (2, 6/8, 1) and Go (2, 7/9, 1), runtime 0, ABI 1,
ops 1/2/3/4/9 with the S1 fields, input slots a permutation of 0..n-1. Output is generated from the
operation records only; the sidecar is never read for output, so a semantic-only envelope and every
member retargeting R(U, M) export the same bytes.

```
package main

func calc(i0 uint64, i1 uint64, ...) uint64 {         ("func calc() uint64 {" for 0 inputs)
	return <expr>
}
```

with one LF after every line (a tab before `return`). `<expr>`: input slot s is `i<s>`; a constant is its
decimal value; ops 2/3/4 are `+`, `-`, `*`. Parentheses are minimal for the exact SSA tree under Go
precedence (operand 6, `*` 5, `+ -` 4, left associative): a left operand is parenthesised iff its
precedence is lower than its parent's, a right operand iff lower or equal (so `a+(b+c)` and `a-(b-c)`
keep their tree and a left chain has none). Spacing follows go/printer: an operator gets a blank on each
side iff its precedence is below the cutoff of its binary expression (cutoff 5 at depth 1 when the
unparenthesised binary region mixes precedences 4 and 5, 6 at depth 1 otherwise, 4 at deeper levels; a
left operand of the same precedence keeps the depth, every other operand gets depth + 1, a parenthesised
operand is printed one level shallower). The output is therefore gofmt-clean: `gofmt` reproduces it
byte for byte (checked on every export in the self-test). Shared SSA results are expanded as trees. The
root is the pre-normalization root of a NORMALIZED unit, otherwise the last result (`translation-v1.md`
section 4); a second, source-written `+ 0` is never stripped.

The output must re-import under section 1: at most 4096 bytes, parenthesis depth at most 16, at most 64
operations after re-import (inputs + expanded tree + 2), and every constant-only node of the emitted
tree within 0..2^64-1 exactly (section 1.1; a C unit of `(0ULL - 1ULL) + a` wraps in C and would be a Go
compile error). Otherwise it refuses 67 with no stdout. Other refusals: 65 malformed envelope (any v2
decode failure other than 68, including unknown versions, or more than 8192 bytes); 68 PROFILE_MISMATCH
(decode status 68, or a valid unit outside the family: profiles 3/5/6, division op 5 in any profile,
other fields, slot gaps); 77 IO; public dispatch 64 (any argument) and 78 (stale or missing build). The
function name is always `calc`.

## 5. Toolchain and commands

Both adapters are compiled by the bridge-seed compiler `191b776ef98821597c3e30655da6d680a9b66edbc9ceaca3c39c61ea6df098e9`
(bridge commit `0fc7d8662cb799e7c206b17c18ea4c3ec2b8fea9`), built from source by `tools/source-aware/build`,
the compiler of the reviewed C importer and of the #621 v1 and v2 codecs. `tools/adapters/build-go-u64.py`
writes `build/L741/{go-import,go-export,build.json}` with the compiler, input and binary hashes; the
public commands check them before dispatch.

```sh
export SA_SEED_REPO=/path/to/omega-bridge        # contains 0fc7d866; mk/go-u64.mk defaults to ~/workspace/omega-bridge
make -f mk/go-u64.mk test-go-u64
tools/adapters/go-import < calc.go > calc.upper
tools/adapters/go-export < calc.upper > exported.go
```

Oracle: Go 1.27.3 linux/arm64 (`~/.local/toolchains/go/bin/go`, user-space toolchain, sha256 recorded in
the self-test result), `go build -trimpath` in a fresh temporary module (`go 1.27`) with
`GOTOOLCHAIN=local GOFLAGS=-mod=mod GOPROXY=off GOWORK=off CGO_ENABLED=0`; the unit file is compiled
unmodified beside a harness file of package `main` that reads decimal vectors and prints `calc(...)`.
`gofmt` from the same toolchain checks the export form.

## 6. Evidence and limits

`tests/universal/translation/go_selftest.py` (fixtures `fixtures-f2-go.json`: all 35 F1 positives in this
profile, `not_in_profile` empty, 12 Go-specific extra positives, and Go refusal fixtures with the
`go build` verdict of each refused file) checks per positive: import; an independent decoder (header,
fields, sidecar binding, spans); the #621 v2 codec `sa identity|decode|replay` (`build/L832`) and the v1
codec's refusal (66) of the v2 unit; N equality with the C import of the F1 source; export of the unit and
of R(U, M) semantic-only for all six members (identical bytes, gofmt-clean); re-import identity; Go on the
original and the exported program and `cc` on the F1 C, compared with a bigint evaluation of the records
on the boundary vectors of `translation-v1.md` section 3 (all pairs for two inputs) and seeded random
vectors. Also: the untyped-constant rule (section 1.1) against `go build`, import and export boundary
controls, the exporter refusals, public-command closure, the composition C -> Go on every F1 positive and
the four accepted C import boundary controls, and Go -> C on every F2-go positive through R(U, C) (in test
code until `tools/adapters/retarget` lands) and the #622 C exporter, with the native realization
`lower(N(U_A))` (G1 lowering, omc `734f0a39`, bridge binding) on both compositions. A piece whose tool is
absent is reported NOT_RUN. Finite DIFFERENTIAL evidence, not PROOF.
