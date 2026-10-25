# Python explicit-mask u64 adapter, profile `python-explicit-u64-pure-v1` (#627)

Status: experimental, author-tested (TESTED), not QUALIFIED and not merge-approved. Lane Python of
`translation-compose-v1.md` (G2). This file defines the Python member of the S1 family. It adds no
envelope, registry, codec, OSC or checker change. The importer and exporter are handwritten seeded
Omega; Python is used only as the host test oracle and for build/dispatch orchestration.

## 1. Unit produced

| field | value |
|---|---|
| header language / profile / dialect version / runtime / ABI | 3 Python3 / 4 python-explicit-u64-pure-v1 / 1 / 0 / 1 |
| op dialect byte | 3 |
| input (op 9) | type 1, attribute 0, effects 0, alias 0, obligations 1 (input-domain), slot = parameter position |
| constant (op 1) | type 1, attribute 1, obligations 0 |
| add/sub/mul (ops 2/3/4) | type 1, attribute 1 (modulo 2^64), effects 0, alias 0, obligations 5 (input-domain + source-explicit-masking) |
| sidecar | OMSP v1, encoding 1 (strict UTF-8; accepted sources are ASCII), raw source bytes stored exactly |

Semantics are S1 (`translation-v1.md` section 2): integers modulo 2^64. `N(U) = R(U, C)`
(`translation-compose-v1.md` section 2) of a Python import equals `N` of the C import of the same
expression tree, byte for byte (checked on all 35 F2 positives).

## 2. Accepted source

The whole input is exactly two lines, each ended by LF (0x0A), and nothing after the second LF:

```
def <name>(<p0>, <p1>, ...):
    return <expr>
```

- Line 1 starts at byte 0 (no indentation). Tokens: `def`, name, `(`, 0..6 parameter names separated by
  `,`, `)`, `:`. Spaces (0x20) may separate tokens and may end the line. No trailing comma.
- Line 2 starts with exactly four spaces, then the token `return`, then `<expr>`; spaces may separate
  tokens and may end the line. The indentation rule is exactly four spaces: any other indentation,
  a blank line, a one-line suite or any further line is refused (section 4).
- Names (function and parameters): ASCII `[a-z][A-Za-z0-9_]*`, at most 32 bytes, not a Python keyword
  or soft keyword (`False None True and as assert async await break class continue def del elif else
  except finally for from global if import in is lambda nonlocal not or pass raise return try while with
  yield match case type`). Parameters are distinct. Any such function name is accepted (the name is not
  part of the semantic bytes). Builtin names (`int`, `print`) are ordinary names here.

Expression grammar (tokens; Python precedence is respected because every accepted form is fully
parenthesised):

```
expr    := operand | masked
masked  := "(" operand op operand ")" "&" MASK
operand := param | literal | "(" expr ")"
op      := "+" | "-" | "*"
MASK    := 0xFFFFFFFFFFFFFFFF          (exactly this spelling: lowercase x, 16 uppercase F)
literal := "0" | [1-9][0-9]*            (value at most 2^64-1)
```

Every arithmetic result is masked in source, by itself: `(x op y) & 0xFFFFFFFFFFFFFFFF`. A masked form
used as an operand needs its own parentheses, `((a + b) & 0xFFFFFFFFFFFFFFFF) * c`, because `&` binds
more loosely than `+ - *` in Python. At most 128 parentheses may be open at once (CPython's own limit is
200). Redundant parentheses are allowed (`((a))`).

Why the mask is mandatory: without it `a + b` is CPython bigint arithmetic (profile 3,
`cpython-int-pure-v1`), not S1. `(a + b + c) & MASK` is also refused, although it has the same value
modulo 2^64, because the profile requires every arithmetic result to be masked explicitly. Masking
a non-arithmetic operand (`a & MASK`), masking twice, or any other mask spelling refuses.

## 3. Mapping and spans

The importer emits, in this order: one input record per parameter (declaration order, slot = position;
unused parameters are kept), then the expression in post-order (left operand, right operand, operation,
as the C importer), then the normalization pair.

- One masked form `(x op y) & MASK` maps to exactly ONE upper operation (2 add, 3 sub, 4 mul). The mask is
  the source-explicit-masking obligation (bit 4), not an operation; there is no record for `&` or for the
  mask literal.
- Normalization pair: YES. Like the C importer, every import appends a synthetic `const 0` and
  `add(root, that const)`, so the unit is always NORMALIZED (`translation-v1.md` section 4) and
  `strip()` removes exactly that pair (`translation-compose-v1.md` section 3). This is required in
  any case for `return b` with parameters `a, b`: the last result must be the function result.
- Spans (sidecar, half-open byte offsets): input = the parameter name token in the signature;
  constant = its digits; arithmetic = the operator token `+`, `-` or `*` inside the masked form;
  synthetic constant and add = `[0,0]`. The parentheses and mask have no span of their own.

## 4. Refusals (statuses mirror the C importer)

Every refusal writes no stdout and one `PY_IMPORT_*` line on stderr.

| status | name | cases |
|---|---|---|
| 65 | SYNTAX | NUL byte; empty input; missing token; indentation other than def at column 0 and return at exactly four spaces; blank line before the body; missing final LF; expression split over lines; bare `return` |
| 66 | UNSUPPORTED_TYPE | parameter annotation (`a: int`), return annotation (`-> int`). Annotations are refused because `int` is a bigint annotation that Python does not enforce |
| 67 | LIMIT | more than 4096 bytes; more than 6 parameters; more than 64 upper operations including inputs and the pair; more than 128 open parentheses; literal above 2^64-1 |
| 68 | UNSUPPORTED_ARITHMETIC | unmasked `+ - *`; two operations under one mask; any mask other than `& 0xFFFFFFFFFFFFFFFF` after a single operation; `a & MASK`; double mask; masked operand without its own parentheses; `/ // % ** << >> & \| ^ ~`, unary `-`/`+`, comparisons, `@`; hex/octal/binary literals other than the one mask, leading zeros (`00`, `010`), `_` separators, floats, imaginary literals |
| 69 | UNSUPPORTED_CONSTRUCT | decorator; anything but `def` first (`async def`, `class`, assignment, lambda); a statement other than `return` (`if`, `while`, `assert`, `global`, assignment); one-line suite; any line after the body; calls; subscripts; attributes; tuples; `;`; strings; keywords in expressions (`lambda`, `not`, `True`, conditional expressions) |
| 70 | UNKNOWN_IDENTIFIER | a name in the expression that is not a parameter (includes builtins such as `abs`) |
| 71 | UNSUPPORTED_SIGNATURE | default values, `*args`, `**kwargs`, `/` and `*` markers, trailing comma, duplicate parameters, names breaking section 2 (uppercase/underscore first, keyword, longer than 32 bytes) |
| 72 | UNSUPPORTED_SOURCE_FORM | any byte above 0x7E (non-ASCII, so invalid UTF-8 and NFKC-normalised Unicode identifiers such as `ａ`), tab, CR (CRLF), any other control byte but LF, backslash (explicit line joining), `#` (comments; a comment in the first two lines can be a PEP 263 coding declaration, e.g. `# coding: utf-7`, that changes how CPython decodes the remaining bytes) |
| 77 | IO | read/write failure, closed stdin |

The public command adds 64 BAD_COMMAND (any argument) and 78 STALE_OR_MISSING_BUILD.

## 5. Input-domain obligation

Python callers can pass negative ints, ints of 2^64 or more, `bool`, or non-ints. S1 inputs are
0..2^64-1. This domain is obligation 1 (input-domain) on every input and arithmetic record; it is a
caller obligation and is not enforced in the source. Neither the importer nor the exporter accepts or
emits an `assert` or range check: `python -O` removes `assert` statements, so an assert is not an
enforcement, and a guard would be a second statement outside this grammar. Outside the domain the
generated function is not S1 (for example `return a` returns a negative argument unchanged). The tests
call every Python program only with in-domain ints.

## 6. Exporter

`tools/adapters/python-export [NAME]` (Omega source `src/adapters/python-u64-export.om`) reads one envelope
on stdin. The reviewed #621 decoder (`src/source-aware/core.om`, `decode`) is prepended unchanged and
validates the whole envelope, sidecar included. Then the unit must be an S1-family member
(`translation-compose-v1.md` section 1): (language, profile) = (1,1), (2,2) or (3,4), runtime 0, ABI 1, every
record of type 1, effects 0, alias 0, input op 9 attribute 0 obligations 1, constant attribute 1
obligations 0, add/sub/mul attribute 1 with the member's arithmetic obligations (1, 1, 5), input slots a
permutation of 0..n-1. The source language of the input is ignored; output is generated from the
records only (the sidecar is never read; a semantic-only envelope exports the same bytes).

Output form (byte-exact, `NAME` defaults to `calc`, LF line ends):

```
def <NAME>(i0, i1, ...):            ("def <NAME>():" for 0 inputs; parameter i<s> is slot s)
    return <expr>
```

`<expr>`: input slot s is `i<s>`; a constant is its decimal; an operation at the root is
`(<A> <op> <B>) & 0xFFFFFFFFFFFFFFFF`, and as an operand `((<A> <op> <B>) & 0xFFFFFFFFFFFFFFFF)`, with single
spaces. Shared SSA results are expanded as trees. The root follows `translation-v1.md` section 4: the
pre-normalization root for a NORMALIZED unit, the last result otherwise.

Exporter statuses: 64 bad command or NAME (NAME follows section 2 name rules); 65 malformed envelope
(any decoder refusal other than 68); 67 LIMIT when the output would not re-import (more than 4096 bytes,
more than 128 open parentheses, or more than 64 operations after re-import, counted as inputs plus the
tree-expanded constants and operations plus the pair); 68 not an S1-family unit (profiles 3, 5, 6, ops
5..8, other types/effects/obligations); 77 IO; public command 78 stale build. With at most 64 records
the operation count is the only limit that can be reached; the other two are checked anyway.

Round trip: for a unit produced by this importer, `import(export(U))` has the same semantic bytes as U.
For any S1-family unit, `N(strip(import(export(U)))) = N(strip(U))` holds when U is import-shaped
(inputs first in slot order, then the post-order tree, then the pair); otherwise the round trip is
checked by equal evaluation and the identity difference is recorded (`translation-v1.md` section 4).

## 7. Build, pins and commands

Compiler: bridge-seed omc `191b776ef98821597c3e30655da6d680a9b66edbc9ceaca3c39c61ea6df098e9` (source
archive `0fc7d8662cb799e7c206b17c18ea4c3ec2b8fea9`, built by `tools/source-aware/build`), recorded in each
`build.json`. Chosen because it is the pinned compiler of the C importer, the C exporter and the #621 codec:
the vendor SHA256 core and the prepended `core.om` decoder are qualified under it, and the bounded
stdin/stdout interface needs no argv (the exporter name arrives in a one-byte-length frame, as in the C
exporter). The native omc `734f0a39…6b61` is not used.

Importer = `src/source-aware/vendor/sha256.om` + `src/adapters/python-u64-import.om`; exporter = vendor
SHA256 + `src/source-aware/core.om` + `src/adapters/python-u64-export.om`. Outputs: `build/L627/import/`
and `build/L627/export/` (`program.om`, the ELF, `build.json`). The public commands refuse a changed
source or binary (78).

```sh
make -f mk/python-u64.mk test-python-u64
# SA_SEED_REPO (default ~/workspace/omega-bridge) holds bridge commit 0fc7d866.
# PY_SA_ROOT (default ~/workspace/universal-read): reference codec `bash tools/source-aware/sa`
#   and the C importer build/L589/c-import.
# PY_C_EXPORT=<path>/tools/adapters/c-export runs Python -> C; empty means Python -> C is NOT_RUN.
tools/adapters/python-import < computation.py > unit.upper
tools/adapters/python-export [NAME] < unit.upper > generated.py
```

## 8. Evidence and limits

`tests/universal/translation/python_selftest.py` (author self-test, TESTED):
same-language round trip for every F2-python positive and boundary control; the reference codec's
`identity` accepts every imported unit and equals SHA256(`omega.source.semantic.v1` || semantic bytes);
compose section 4.1 identity; the F2 rewrite has the same `N()` as the C import of its F1 source; python3
runs the original and the exported source on boundary vectors (0, 1, 2, 2^32-1, 2^32, 2^63-1, 2^63,
2^64-2, 2^64-1) and 64 seeded random vectors per program, compared with a bigint evaluation of the records
(REFERENCE + DIFFERENTIAL). C -> Python on every F1 positive and accepted C boundary control: C importer,
cc-compiled original C, generated Python; R(U, Rust) and R(U, Python) export identically. Python -> C runs
only when a translation-v1 section 6 exporter is given: it accepts the C member only, so the test applies
R(U, C) first. The native-realization leg (section 5 lowering) is NOT_RUN in this lane. Refusal fixtures
check exact statuses. None of this is a proof of equivalence.

`tests/universal/translation/fixtures-f2-python.json`: the 35 F1 positives rewritten
(`F2-python-F1-Pnn`; F1-P33 drops its C comments and CRLF, which are source decoration), four boundary
controls (exact 4096 bytes, the 61-term chain at 64 operations, 16 and 128 nested parentheses), the
NOT_IN_PROFILE list (`comment-at-capacity`: no comments in this profile) and the refusal fixtures with
their statuses.

Not covered: Python versions other than the CPython 3 used as oracle, any other Python syntax, bigint or
dynamic profiles (3, 6), runtime input-domain enforcement, independent validation and QUALIFIED status.
