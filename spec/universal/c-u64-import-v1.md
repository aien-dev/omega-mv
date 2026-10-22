# Bounded C-to-Omega importer, first #622 slice

Status: experimental, author-tested, not merge-approved. This implements import,
not export, OSC lowering, formal translation validation or a C runtime. It is not
completion of #622 or architecture #295.

## Source profile and environmental assumptions

The input starts with the exact bytes `#include <stdint.h>\n`. There is exactly
one function definition, with a `uint64_t` return, either `(void)` or one through
six distinct named `uint64_t` parameters, and a body containing one return
expression. The function name is `calc` or starts with `omega_` and has at least
one following identifier character. Names are at most 32 ASCII bytes, start with
a lowercase letter, contain ASCII letters/digits/underscore, are not C keywords,
and do not begin with `int` or `uint`. `main` is also reserved by this adapter. This intentionally avoids `<stdint.h>`
typedef/macro collisions and C standard-library external namespaces. Other names
are unsupported, not automatically renamed.

Expressions contain declared parameters, decimal literals of spelling `0ULL` or
`[1-9][0-9]*ULL`, parentheses, and binary `+`, `-`, `*`. Multiplication binds tighter
than addition/subtraction; equal-precedence operations associate left. All values
and results are unsigned 64-bit; operations wrap modulo 2^64. Literals exceeding
UINT64_MAX refuse. The C environment must provide uint64_t and exactly 64-bit
unsigned long long, with the standard header and no additional predefined/user
macros affecting accepted identifiers. Qualification currently uses the recorded
Linux AArch64 GCC environment, not every possible C implementation.

Spaces, tabs, LF and CR and ordinary block/line comments are retained as source
bytes. Comments can contain non-ASCII bytes; code identifiers cannot. NUL,
backslash bytes anywhere, all nine C trigraph spellings, extra preprocessing,
other includes, additional declarations, storage qualifiers, signed/narrow types,
casts, pointers, effects, calls, division, shifts, branches and loops refuse.
Backslashes/trigraphs are excluded before comment removal: C translation phases
can otherwise splice a line comment and silently change the return expression.
Octal, hexadecimal and alternate literal suffixes are not decimal literals and
refuse. These exclusions are part of the adapter profile, not changes to C.

Bounds: source at most 4096 bytes, at most six parameters, parenthesis depth at
most 16 and at most 64 upper operations including parameter declarations and two
synthetic return-selection operations. Exactly-at-capacity and over-capacity
controls are executable. This is a whole-input, one-process interface, not a
reentrant/shared-memory parser or sandbox.

## Implementation and preserved identities

`src/adapters/c-u64-import.om` is handwritten seeded Omega. Its lexer, precedence
parser, symbol/slot mapping, arithmetic profile, span tracking, envelope writing
and SHA256 calls run in its static native AArch64 ELF. The SHA256 source is the
unchanged hash-pinned import already used by #621. No C/Rust/Python parser,
transpiler, library or compute fallback constructs the imported graph.

The adapter produces the existing additive `source-aware-v1.md` OMSB envelope:
profile1/runtime0/abstract-pure ABI1, u64 input/constant/add/sub/mul SSA records,
required obligations, the #625 contract digest, semantic association and the
separate raw-byte source sidecar. Input records preserve distinct argument slots,
even when unused. Literal spans identify their source token; arithmetic spans
identify the operator token; input spans identify declarations. A final synthetic
`root + 0` makes the chosen return root the last SSA result even when the source
returns an earlier parameter. The synthetic zero and add have empty spans.

Original source bytes are stored exactly, including comments and CRLF body
bytes. Exact replay is a source-storage guarantee, not proof that the imported
semantics correspond to C. Semantic/provenance identities remain separate.
Identical hashes are never treated as behavioral-equivalence proofs.

No historical OSC encoding, canonical program identity, Omegese/checker2 source or
receipt schema is modified. The existing #621 public decoder, replay command and
legality checker consume each positive imported unit in the gate. Its
LOWERABLE_WITH_OBLIGATIONS result is not a completed lowering or proof. The current
behavioral test evaluates the upper graph in an independent development reference
and compares with compiled original C. It does **not** execute the C computation
through an Omega CPU backend yet.

## Commands and clean-source reproduction

Prerequisites: Linux AArch64; git, bash, Python3 and ordinary POSIX utilities for
host build/dispatch; Node plus GCC for independent tests; a local clone containing
bridge commit `0fc7d8662cb799e7c206b17c18ea4c3ec2b8fea9`.

From a checkout of this candidate:

```sh
# Replace this with the actual local clone containing the exact immutable pin.
export SA_SEED_REPO=/path/to/omega-bridge
make -f mk/c-u64-import.mk test-c-u64-import
# Both targets are also included by the root Makefile.
```

The builder reuses `tools/source-aware/build`, which archives the committed
bridge source and runs its original offline bootstrap, verifies source locks and
requires compiler SHA256
`191b776ef98821597c3e30655da6d680a9b66edbc9ceaca3c39c61ea6df098e9`.
It verifies a nonempty correctly targeted ELF before use. No sibling mutable
binary or downloaded compiler is required. Each correctness run uses a fresh
UUID output directory; an existing output directory is refused by the runner.

```sh
# Source file contents, not a filename argument, enter the importer.
tools/adapters/c-import < computation.c > caller-owned-temporary.upper
# Existing qualified component, not another decoder:
tools/source-aware/sa replay < caller-owned-temporary.upper > replayed.c
cmp computation.c replayed.c
```

The build script and public dispatch use Python only for filesystem/process/hash
orchestration; this is explicitly **not** a pure-Omega public command boundary.
The public wrapper rejects missing/stale source/artifact identities and unexpected
arguments before dispatch. Parsing failure emits a named stderr diagnostic and no
stdout. Destination management belongs to the caller: shell `>` truncates before
the command starts. For valuable destinations, create a fresh temporary file and
rename only after status zero. OS I/O failure may interrupt a stream.

Refusals: 65 syntax; 66 unsupported type; 67 capacity; 68 unsupported arithmetic;
69 unsupported construct; 70 unknown identifier; 71 unsupported signature/name;
72 unsupported preprocessing/translation phase; 77 I/O. Public dispatch adds
64 bad command and 78 missing/stale build. Diagnostics do not currently include
line/column detail.

## Evidence and limits

`c_import_gate.py` retains all original 34 programs and 40 refusal fixtures, adds
an accepted `omega_` function and eight source-spelling/namespace regressions.
The original C is compiled and executed by the external native compiler. The
native-produced upper graph is independently decoded/evaluated, and accepted by
the separate existing native #621 component. This is finite differential evidence,
not an exhaustive equivalence proof. Oracle flags suppress unused-parameter
warnings specifically because unused named inputs are valid C and deliberately
exercise the preserved function boundary; all other selected warnings remain
errors. Original fixture-construction failures are preserved, not relabelled.

`c_import_controls.py` exercises exact and excessive source/operation/depth bounds,
closed stdin, public identity mismatch refusals and five successfully compiled
semantic mutants: add/sub/mul, literal digits, parameter mapping. A mutant counts
only when an actual valid imported graph disagrees with the independent compiled
C result; compile failures/refusals alone cannot count as semantic mutation kills.

The author found and reproduced three real spelling defects before freezing:
`010ULL` was incorrectly read as decimal, and line-splice/trigraph-splice comments
changed the expression. Native GCC demonstrated the differing values. Their RED
records and new passing refusals are retained. This is why simple successful
arithmetic examples are not a general C qualification.

Still absent: C exporter, general identifiers/preprocessing, other widths/types,
control flow/memory/runtime features, actual upper-to-OSC lowering, checker-bound
translation receipts, Rust/Python adapters, x86/RV64 backend qualification and GPU
execution. No signed production authority or universal correctness is claimed.
Independent source review and the sole Renda/Spark publication process remain
separate acceptance gates. See the machine-readable adjacent adapter manifest.
