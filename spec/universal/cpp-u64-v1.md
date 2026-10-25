# C++17 std::uint64_t pure profile v1 (#739, G3)

Status: experimental, author-tested (TESTED), not QUALIFIED. Profile `cpp17-uint64-pure-v1` (source-aware v2
`source-aware-v2.md`: OMSA semantic header version 2, language 5 C++17, profile 7, runtime 0, ABI 1, op dialect
5, arithmetic obligations 1). Additive to `translation-v1.md`, `translation-compose-v1.md` and
`translation-compose-v2.md`; S1 semantics (integers modulo 2^64, ops 1/2/3/4/9) are unchanged. C++ compilers
are used only as test oracles; both adapters are handwritten seeded Omega.

## 1. Accepted source

The input starts with the exact bytes `#include <cstdint>\n` and is then exactly one function definition:

```
source  := "#include <cstdint>\n" ws TYPE ws NAME ws "(" ws [param (ws "," ws param)*] ws ")" ws
           "{" ws "return" ws expr ws ";" ws "}" ws EOF
param   := TYPE ws NAME
expr    := term (ws ("+" | "-") ws term)*            (left associative)
term    := primary (ws "*" ws primary)*              (left associative, binds tighter than + -)
primary := NAME | LITERAL | "(" ws expr ws ")"
LITERAL := TYPE ws "{" ws DEC "ULL" ws "}"           (S1 op 1)
TYPE    := "std::uint64_t"                           (one lexeme: no whitespace or comment inside)
DEC     := "0" | [1-9][0-9]*                         (value at most 2^64-1)
ws      := (SP | TAB | LF | CR | "//" comment-to-LF | "/*" comment "*/")*
```

- 0..6 parameters, all `std::uint64_t`, named and distinct; zero parameters are written `()`. `+`, `-`, `*`
  are S1 ops 2, 3, 4. Parentheses group only and produce no operation.
- `NAME` (function and parameters): at most 32 ASCII bytes, first byte `a`..`z`, then `[A-Za-z0-9_]`; no
  `__` anywhere (reserved, [lex.name]); not beginning with `int` or `uint` (the `<cstdint>` names); not a C++17
  or C++20 keyword, alternative token, or identifier with special meaning (`final`, `override`, `import`,
  `module`); not `main`, `std`, `linux` or `unix`. The function name is `calc` or `omega_` followed by at least
  one identifier byte. Other names refuse; nothing is renamed.
- Comments are retained as source bytes and may contain any byte except NUL (no UTF-8 check, as in the C
  profile); code outside comments is ASCII.
- At most 4096 bytes, at most 64 upper operations (inputs and the synthetic pair included), parenthesis depth
  at most 16 (literal braces are not counted).

### Type rules (what the importer establishes before emitting ops 2/3/4)

`translation-compose-v2.md` section 4 requires both operands of every `+ - *` to be exactly `std::uint64_t`,
with no integral promotion and no mixed types. In this grammar every operand is a parameter, a `LITERAL` or a
parenthesised `expr`, and each of those is a prvalue or lvalue of exactly `std::uint64_t`, so every operator
has two `std::uint64_t` operands and a `std::uint64_t` result ([expr.arith.conv]: same type, no conversion).
With the environment assumption below (`int` at most 64 bits), the integral promotions of [conv.prom] never
apply to `std::uint64_t`, and unsigned arithmetic is modulo 2^64 ([basic.fundamental]).

- Literal spelling. `123ULL` alone has type `unsigned long long`. On the qualified target (Linux AArch64,
  LP64) `std::uint64_t` is `unsigned long`, a distinct type, so `a + 1ULL` is a mixed-type expression whose
  result type is `unsigned long long`; it refuses 68. The only constant spelling is
  `std::uint64_t{<DEC>ULL}`: a braced explicit type conversion ([expr.type.conv]) whose result is exactly
  `std::uint64_t`. List-initialization forbids narrowing ([dcl.init.list]), so a value that does not fit is
  ill-formed instead of being converted (the self-test has an oracle control). The `ULL` suffix inside the
  braces is required because an unsuffixed decimal at or above 2^63 has no standard type ([lex.icon]); one
  uniform spelling covers all 2^64 values. `ull`, `UL`, `U`, unsuffixed, hexadecimal, octal, binary, leading
  zero, digit separators `'` and user-defined literals refuse 68. `std::uint64_t{}`, `std::uint64_t{a}`,
  `std::uint64_t{1ULL+2ULL}` and `std::uint64_t(...)` refuse 69 (only a single decimal literal may be braced).
- A plain `1` is `int`; `1 - a` and `a - 1` mix `int` and `std::uint64_t` (usual arithmetic conversions);
  they refuse 68 rather than being typed implicitly.
- `uint64_t` unqualified refuses 66: `<cstdint>` declares `std::uint64_t`; whether it also declares
  `::uint64_t` is unspecified ([headers]). `<stdint.h>` is a deprecated header ([depr.c.headers]) and is not
  accepted (72). Other types (`int`, `auto`, `unsigned long long`, `std::int64_t`, `std::uint32_t`,
  `std::size_t`, `::uint64_t`, `std :: uint64_t` with inner whitespace, `(void)`) refuse 66.

### Why the other constructs are excluded

- `constexpr`, `consteval`, `inline`, `static`, `extern`, `extern "C"`, `noexcept`, `[[...]]` attributes and
  trailing `const`: they change linkage, the ODR status, the function type or the diagnostics of the
  declaration, not its value; each would be a second spelling of the same S1 function, and an exporter would
  have to decide which to emit. They refuse 69.
- Templates, namespaces, `using`, `auto`, references, pointers, casts (`static_cast` etc. and functional
  casts), lambdas, user-defined literals, operator overloading, statements other than the single `return`,
  control flow, a second declaration (also a stray `;`): outside S1 or a second spelling; 69 (types 66).
- Division `/` (division by zero is undefined behaviour; op 5 is outside the family), `%`, shifts, bitwise
  and logical operators, comparisons, `?:`, unary `+ - ~ !`, assignment, `++`/`--` and the alternative tokens
  (`and`, `or`, `not`, `xor`, `bitand`, `bitor`, `compl`, `*_eq`): 68.
- Preprocessing beyond the first line: any `#` or `%:` outside comments refuses 72. Every backslash byte and
  every C trigraph spelling (`??` followed by one of `= / ' ( ) ! < > -`) refuses 72 anywhere, including inside
  comments, exactly as in the C profile: C++17 removed trigraphs, but `-trigraphs` and pre-C++17 modes still
  replace them, and a `??/` or `\` at the end of a `//` comment splices the next line into the comment.
- `linux` and `unix` are predefined object-like macros in GCC's GNU modes (`-std=gnu++17`, g++'s default) and
  not in `-std=c++17`; reserving them keeps every accepted source meaningful under both.

Environment assumptions (as in the C profile): `std::uint64_t` exists (it is optional in the standard),
`unsigned long long` is exactly 64 bits, `int` is at most 64 bits, and no user-defined or predefined macro
changes an accepted identifier. Qualification uses the recorded Linux AArch64 g++ environment.

## 2. Refusals (no stdout, named stderr `CPP_IMPORT_*`)

| status | name | cases |
|---|---|---|
| 65 | SYNTAX | NUL; unterminated block comment; missing `;`, `)` or operand; trailing comma; default argument |
| 66 | UNSUPPORTED_TYPE | return or parameter type other than `std::uint64_t` (section 1) |
| 67 | LIMIT | over 4096 bytes; over 6 parameters; over 64 upper operations; parenthesis depth over 16; literal above 2^64-1 |
| 68 | UNSUPPORTED_ARITHMETIC | operators and literal spellings outside section 1 (including bare `ULL` and plain `int` literals) |
| 69 | UNSUPPORTED_CONSTRUCT | specifiers, attributes, templates, namespaces, `using`, references, pointers, casts, braced non-literals, lambdas, statements, a second declaration, keywords such as `this`/`sizeof`/`true` in expressions, unnamed parameters, non-ASCII outside comments |
| 70 | UNKNOWN_IDENTIFIER | an identifier that is not a parameter (`c`, `std::max`, unqualified `uint64_t{...}` in an expression) |
| 71 | UNSUPPORTED_SIGNATURE | function or parameter name rules; duplicate parameter |
| 72 | UNSUPPORTED_PREPROCESSOR | first line not exactly `#include <cstdint>\n` (BOM, CRLF, `<stdint.h>`, missing); `#`/`%:`; backslash; trigraphs |
| 77 | IO | read or write failure |

Public dispatch (`tools/adapters/cpp-import`) adds 64 BAD_COMMAND (any argument) and 78 for a missing or stale
build (source or artifact hash differs from `build/L739/build.json`).

## 3. Imported unit

A source-aware v2 OMSB envelope of the C++ member: OMSA version 2, language 5, profile 7, dialect version 1,
runtime 0, ABI 1, the #625 contract digest; every operation dialect 5, version 1, type 1, effects 0, alias 0.
Input op 9 per parameter in declaration order (slot = position, kept when unused; attribute 0, obligations
1); constant op 1 (attribute 1, obligations 0); ops 2/3/4 (attribute 1 modulo 2^64, obligations 1).
Operations are emitted in source order with operands before their user, exactly as the C importer orders the
equivalent C expression, so `N(import_C++(s')) = N(import_C(s))` for every F1 expression (self-tested).

Normalization pair: **yes**. Like the C importer, the C++ importer always appends `const 0` and
`add(root, that const)` (both with the empty span [0,0]) per `translation-v1.md` section 4, for the same
reasons as the Rust profile: a body that is a bare earlier parameter has no other representation whose root is
the last result, and `strip` then treats C, Rust and C++ units alike.

Sidecar OMSP v1, encoding 0 (raw bytes, as the C importer), exact source bytes. Spans: an input covers its
parameter name; a constant covers the whole literal `std::uint64_t{5ULL}` (from `std` to `}`); an arithmetic
op covers its operator byte.

## 4. Export

`tools/adapters/cpp-export` (Omega source `src/adapters/cpp-u64-export.om`, prepended with the unchanged vendor
SHA256 and the #621 v2 codec `src/source-aware/core-v2.om`) reads one OMSB envelope on stdin, validates it with
the v2 `decode` (sidecar framing, hashes and spans included) and accepts every member of the extended family
(`translation-compose-v2.md` section 1): C 1/1, Rust 2/2, Python 3/4 (arithmetic obligations 5) under OMSA
version 1 and C++ 5/7, Zig 6/8, Go 7/9 under version 2 (the codec ties the version to the profile); runtime 0,
ABI 1, ops 1/2/3/4/9 with the S1 fields, input slots a permutation of 0..n-1. Output is generated from the
operation records only; the sidecar is never read for output, so a semantic-only envelope and every
retargeting R(U, M) export the same bytes.

```
#include <cstdint>
std::uint64_t calc(std::uint64_t i0, std::uint64_t i1, ...) { return <expr>; }      ("calc()" for 0 inputs)
```

followed by one LF. `<expr>`: input slot s is `i<s>`; a constant is `std::uint64_t{<decimal>ULL}`; a binary
op is `<A> + <B>`, `<A> - <B>` or `<A> * <B>` with single spaces, parenthesised exactly as the C exporter
(`translation-v1.md` section 6: leaf 3, `*` 2, `+`/`-` 1; a left operand gets parentheses iff its precedence
is lower than the parent's, a right operand iff lower than or equal), because C++ gives `+ - *` the same
precedence and left associativity as C and the braced literal is a postfix-expression (a leaf). Shared SSA
results are expanded as trees. The root is the pre-normalization root of a NORMALIZED unit, otherwise the last
result (`translation-v1.md` section 4); a second, source-written `+ std::uint64_t{0ULL}` is never stripped.

The output must re-import under section 1: at most 4096 bytes, parenthesis depth at most 16, at most 64
operations after re-import (inputs + expanded tree + 2). Otherwise it refuses 67 with no stdout. Other
refusals: 65 malformed envelope (any v2 decode failure other than 68, including unknown versions 66 and
obligation mismatches 72, or more than 8192 bytes); 68 PROFILE_MISMATCH (decode status 68, or a valid unit
outside the family: profiles 3/5/6, division op 5 in any profile, other ops or fields, slot gaps); 77 IO;
public dispatch 64 (any argument) and 78 (stale or missing build). The function name is always `calc`.

## 5. Toolchain and commands

Both adapters are compiled by the bridge-seed compiler `191b776ef98821597c3e30655da6d680a9b66edbc9ceaca3c39c61ea6df098e9`
(bridge commit `0fc7d8662cb799e7c206b17c18ea4c3ec2b8fea9`), built from source by `tools/source-aware/build`,
the compiler of the C importer, the #621 codecs and the Rust/Python adapters. `tools/adapters/build-cpp-u64.py`
writes `build/L739/{cpp-import,cpp-export,build.json}` with the compiler, input and binary hashes; the public
commands check them before dispatch. The exporter needs `src/source-aware/core-v2.om` (#621 v2 codec lane).

```sh
export SA_SEED_REPO=/path/to/omega-bridge        # contains 0fc7d866 (and 012860db)
export OMEGA_BRIDGE_REPO=$SA_SEED_REPO OMEGA_NATIVE_OMC=/path/to/omc   # omc sha256 734f0a39
make -f mk/cpp-u64.mk test-cpp-u64               # CXX/CC default to g++/cc on PATH
tools/adapters/cpp-import < calc.cpp > calc.upper
tools/adapters/cpp-export < calc.upper > exported.cpp
```

Oracles (declared and pinned here, translation-compose-v2 section 4): g++ 13.3.0 (Ubuntu 13.3.0-6ubuntu2~24.04.1,
`/usr/bin/g++`) with `-std=c++17 -O2 -Wall -Wextra -Werror -Wconversion -Wsign-conversion -pedantic-errors
-Wno-unused-parameter` (unused named inputs are valid and deliberately kept); `cc` with `-std=c17 -O2 -Wall
-Wextra -Werror -Wno-unused-parameter` for C sources; the native realization uses the pinned omc `734f0a39`
and omega-bridge `native.om` at `012860db` exactly as `lower_selftest.py`.

## 6. Evidence and limits

`tests/universal/translation/cpp_selftest.py` (fixtures `fixtures-f2-cpp.json`: all 35 F1 positives in this
profile, `not_in_profile` empty, plus C++ refusal fixtures) checks per fixture: import; an independent decoder
(v2 header, fields, sidecar binding, spans); v2 codec `sa identity|decode|replay|legality` from `build/L832`
(NOT_RUN when that build is absent); N equality with the C import of the F1 source; export of the unit and of
R(U, M) semantic-only for all six members (identical bytes); same-language re-import with identical semantic
bytes; g++ on the original and the exported C++ and `cc` on the F1 C, compared with a bigint evaluation of the
records on the boundary vectors of `translation-v1.md` section 3 (all pairs for two inputs) and seeded random
vectors. Also: oracle controls (a `ULL` literal is not `std::uint64_t`, the braced form and the operators are;
an out-of-range braced literal does not compile), import boundary controls, exporter refusals and accepted
exporter boundaries, public-command closure, and the compositions C -> C++ (F1 positives and the four accepted
C import boundary controls) and C++ -> C (F2 positives and the accepted C++ boundary controls). C++ -> C uses
`export_C(R(U, C))` with the unchanged v1-core G1 C exporter (translation-compose-v2 section 3). R is the #624
`tools/adapters/retarget` when the checkout has it (`mk/cpp-u64.mk` builds it; `--retarget` or
`CPP_SELFTEST_RETARGET` names another one), and every tool output must then equal the test-code R byte for
byte; without it the test-code R alone is used. The self-test has no default outside the checkout: a missing
g++/cc, `OMEGA_NATIVE_OMC` or `OMEGA_BRIDGE_REPO` is NOT_RUN (exit 3), a pin mismatch is FAIL. Both compositions check identity under N and the
DIFFERENTIAL agreement of the original program, the reference, the generated program and the native
realization `lower(N(U_A))`. Finite DIFFERENTIAL evidence, not PROOF.
