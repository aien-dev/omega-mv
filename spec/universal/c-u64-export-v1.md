# C17 uint64 exporter v1 (#622 C export lane)

Status: author-tested (TESTED), not QUALIFIED. Implements `spec/universal/translation-v1.md` section 6
(contract sha256 `063f1d630e31925876454129c0024539bea0c7eb17f7187bc0e7ef56790de86e`). It changes no
existing format, the importer, the #621 codec or the contract.

## Command

```sh
tools/adapters/c-export [NAME] < unit.upper > unit.c
```

stdin is one OMSB v1 envelope (with or without the OMSP sidecar). On success stdout is exactly

```
#include <stdint.h>
uint64_t <NAME>(uint64_t i0, ..., uint64_t i<n-1>) { return <expr>; }
```

with one trailing LF, `(void)` for zero inputs. `NAME` defaults to `calc`; otherwise it must follow the
C import profile (`c-u64-import-v1.md`): `calc`, or `omega_` followed by at least one of `[A-Za-z0-9_]`,
at most 32 bytes.

## Generation

Generated from the OMSA operation records only. The sidecar is validated (it is part of decoding a v1
unit) but its stored source bytes are never read for output, so a semantic-only envelope exports
byte-identically.

- Input record with slot s is `i<s>`; parameters are declared in slot order. A constant is `<decimal>ULL`.
- Add/sub/mul are `<A> + <B>`, `<A> - <B>`, `<A> * <B>`. With precedence leaf 3, `*` 2, `+`/`-` 1, the
  left operand is parenthesised iff its precedence is lower than the parent's, the right operand iff it
  is lower or equal. This is the minimal parenthesisation that keeps the exact SSA tree.
- Root (translation-v1 section 4): for a NORMALIZED unit (at least 3 results, result n-2 is `const 0`,
  result n-1 is `add(r, n-2)` with r < n-2) the expression of r; otherwise the last result. A second,
  source-written `+ 0ULL` is kept.
- Shared SSA results are expanded as trees. Before any output the exporter computes, per record and
  saturating at 2^20, the expanded text length, parenthesis nesting depth and the number of upper
  operations a re-import would create (constants and operators; inputs are declarations).

## Refusals (no stdout, named stderr)

| status | stderr | cause |
|---|---|---|
| 64 | `C_EXPORT_BAD_COMMAND` | more than one argument, empty or invalid NAME, missing input frame |
| 65 | `C_EXPORT_MALFORMED` | envelope fails source-aware v1 decode (any core status other than 68), or input over 8192 bytes |
| 67 | `C_EXPORT_LIMIT` | output over 4096 bytes, parenthesis depth over 16, or re-import over 64 upper operations (inputs + expanded operations + the 2 synthetic records) |
| 68 | `C_EXPORT_PROFILE_MISMATCH` | core decode status 68, or a valid v1 unit outside S1 (language/profile/runtime/ABI not 1/1/0/1, an operation other than 1/2/3/4/9, type, effects, alias, attribute or obligations not the S1 values, input slots not a permutation of 0..n-1) |
| 77 | `C_EXPORT_IO` | read or write failure |
| 78 | `C_EXPORT_STALE_OR_MISSING_BUILD` | public wrapper only: build manifest, source or binary identity mismatch |

The status mapping is the same as `tools/adapters/lower`. Without sharing, the 4096-byte bound cannot
be reached before the 64-operation bound (at most about 26 bytes per operation), so for expanded
shared graphs the length check is the first that fires.

## Implementation and build

`src/adapters/c-u64-export.om` is handwritten seeded Omega (program mode). The build concatenates the
unchanged `src/source-aware/vendor/sha256.om`, the unchanged #621 decoder `src/source-aware/core.om`
(the exporter calls its `decode n`) and the exporter, and compiles them with the same pinned bridge
seed compiler as the importer (`191b776e…98e9`, built by `tools/source-aware/build`). Native input
frame: one byte name length (0 = `calc`), the name bytes, then the envelope to EOF. The Python
wrapper `tools/adapters/c-export` only checks the build identities in `build/L622/build.json`, adds
the frame and copies stdout on success; it parses nothing.

```sh
export SA_SEED_REPO=/path/to/omega-bridge   # clone containing 0fc7d8662cb799e7c206b17c18ea4c3ec2b8fea9
make -f mk/c-u64-export.mk test-c-export
```

## Evidence

`tests/universal/translation/c_export_selftest.py` (author self-test, DIFFERENTIAL + REFERENCE,
NATIVE AArch64, no PROOF): every F1 positive and the four accepted boundary controls of
`c_import_controls.py` (exact-source-capacity, comment-at-capacity, operation-capacity, nesting-limit)
are imported, exported, re-imported with byte-identical OMSA semantic bytes and equal `sa identity`,
exported again from the semantic-only envelope with identical output, and the original and exported C
are compiled with `cc -std=c17 -O2 -Wall -Werror` and compared on all boundary-value pairs (or
rotations for more than two inputs) plus 64 seeded random vectors. Constructed semantic-only units
cover the non-NORMALIZED path (exactly one `+0` pair added on re-import), shared SSA expansion,
permuted input records, depth exactly 16 and the maximum constant; refusals cover each status above.
Independent validation (#623) is a separate gate.
