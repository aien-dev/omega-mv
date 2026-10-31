# Translation handoff contract v1 (#624 G0)

Status: FROZEN for the first executable slice (G1, C17 pure u64) on 2026-10-25 by the #624 integration
coordinator. Additive. It changes no existing format: `semantics.md`, `source-aware-v1.md` (OMSB/OMSA/OMSP
v1), `c-u64-import-v1.md`, OSC, Omegese, checker2 and every v1 identity stay byte-identical. A change to
anything below needs `translation-v2.md`, never an edit of this file.

## 1. Frozen inputs (exact bytes)

| input | identity |
|---|---|
| semantics `spec/universal/semantics.md` (#625, reviewed 69790fb4/c1da86d6) | sha256 `d4ed6148bfa97cc693d3fa4495e2d7e2e68aba04607a7e96dd9a9c54bf9073a8` |
| upper envelope `spec/universal/source-aware-v1.md` (#621 cd2c41f3) | sha256 `e0efbc717b61976614c1a1e9eee543284bcfb5410922d23b4ad70cb6a99615ec` |
| C import profile `spec/universal/c-u64-import-v1.md` (#622 50f75da2) | sha256 `84f06ce1ad1653bed08c940696b23d87ce22afb7ebb1b5c3e5970a23b3916012` |
| importer/codec toolchain | as pinned by those specs (bridge seed `0fc7d866`, compiler `191b776e…98e9`), unchanged |
| native realization compiler | omega-mv seed `seed/out/omc`, sha256 `734f0a39319fbfa930bcc62cb30dbf06d47d78e126372b74f5baf478e6126b61`, born from the seed by `seed/omega-from-seed.sh` |
| native binding generator | omega-bridge `src/native.om` at `012860dbf26ee62a9838594b9dfd1e3e94a540a9` (#8, #12 last-function entry + private helpers, #13 read-only runtime table) |

## 2. Semantic profile S1 (the only shared profile of G1)

One pure function of 0..6 u64 inputs returning u64; values are integers modulo 2^64; operations are
constants 0..2^64-1 and add, sub, mul modulo 2^64 (upper ops 1, 2, 3, 4, input op 9). No division,
shifts, bitwise operators, comparisons, control flow, recursion, memory, calls, effects, exceptions or
runtime. Representation limits are those of the upper v1 envelope: 1..64 SSA results including inputs
and constants, the last result is the function result, input slot immediates are a permutation of
0..n-1 (unused inputs are kept), source at most 4096 bytes, envelope input at most 8192 bytes.

An upper unit is in S1 iff it decodes under source-aware v1 with language 1 profile 1 runtime 0 ABI 1,
and every operation is of type 1, effects 0, alias 0, with exactly these attribute/obligation fields
(source-aware-v1.md typed registry): input op 9 attribute 0 obligations 1; constant op 1 attribute 1
obligations 0; add/sub/mul ops 2/3/4 attribute 1 (modulo 2^64) obligations 1.
(Rust profile 2 and Python profile 4 units use the same operations and join S1 in G2; their source
profiles are separate. Profile 4 adds obligation 4, source-explicit-masking, to arithmetic.)

## 3. Observable equivalence

Two artifacts agree on S1 iff for every input vector they return the same u64, and both refuse or both
accept (with the declared status) the same source. Evidence classes, never mixed in a claim:
- REFERENCE: the #625 integer model evaluated on Python bigints with explicit `& (2^64-1)`.
- DIFFERENTIAL: finite input sets (boundary values 0, 1, 2, 2^32-1, 2^32, 2^63-1, 2^63, 2^64-2, 2^64-1
  per input, plus seeded random vectors) compared across artifacts.
- PROOF: none in G1. Equal SHA256 semantic identities are content identity, not equivalence.

## 4. The importer's `+0` normalization

The C importer appends a synthetic `const 0` (empty span) and `add(root, that const)` so the returned
value is always the last SSA result (`c-u64-import-v1.md`). Rules:
- A unit is NORMALIZED iff it has at least 3 results, result n-2 is `const 0` and result n-1 is
  `add(r, n-2)` with r < n-2. The normalization adds exactly one such pair per import.
- Exporters emit the expression of r (the pre-normalization root) for a NORMALIZED unit, and the
  expression of the last result otherwise. They never strip a second, source-written `+ 0ULL`.
- Round-trip check: import(export(U)) must have the same semantic identity as U for a NORMALIZED U from
  the C importer (the re-import adds the pair back). A unit that is not NORMALIZED gains one pair on
  re-import; its round trip is checked by equal evaluation (section 3), and the identity difference is
  recorded, not hidden.

## 5. Lowering seam (compiler/core lane): upper -> restricted .om -> native

`tools/adapters/lower` (Omega source `src/adapters/upper-lower.om`) reads one S1 envelope on stdin and
writes one restricted Omegese function-mode unit on stdout:

```
note omega.universal.lower.v1 semantic <64 lowercase hex of the unit's semantic identity>
fn <name> i0 ulong i1 ulong ... to ulong      (exactly n inputs, slot order; "fn <name> to ulong" for 0)
v = new ulong <count> 0
put v <k> i<slot>                              (op 9)
put v <k> <decimal>                            (op 1)
put v <k> add|sub|mul get v <a> get v <b>      (ops 2, 3, 4)
ret get v <count-1>
end
```

One line per SSA result in record order; `<name>` is `calc` unless the caller passes a name
(`lower NAME`, a C identifier of at most 63 bytes that is not an Omegese reserved word). Output is
byte-deterministic. Refusals: non-S1 unit 68 PROFILE_MISMATCH; malformed envelope 65; bad name 64.
Native realization = the pinned omc compiling that unit in function mode, bound by the pinned bridge
generator as `uint32_t omega_<name>(uint64_t *result, uint64_t i0, ...)` (status 0 ok).

## 6. C export (#622 lane): upper -> newly generated C17

`tools/adapters/c-export` (Omega source `src/adapters/c-u64-export.om`) reads one S1 envelope on stdin
and writes C17 source on stdout, generated from the operation records only (the sidecar's stored bytes
are never read for output; a semantic-only envelope must export identically). Frozen output form:

```
#include <stdint.h>
uint64_t <name>(uint64_t i0, uint64_t i1, ...) { return <expr>; }      ("(void)" for 0 inputs)
```

followed by one LF. `<expr>`: input slot s is `i<s>`; a constant is `<decimal>ULL`; a binary op is
`<A> + <B>`, `<A> - <B>` or `<A> * <B>` with single spaces, parenthesised only where C precedence and
left associativity require it to keep the exact SSA tree: with precedence leaf 3, `*` 2, `+`/`-` 1, a
left operand gets parentheses iff its precedence is lower than the parent's, a right operand iff its
precedence is lower than or equal to the parent's (so `a+(b+c)` and `a-(b-c)` keep their tree, a
left-leaning chain `a+a+...+a` is emitted without parentheses). Shared SSA results are expanded as
trees (C has no sharing in this profile); the root per section 4. The output must import under
`c-u64-import-v1` (so: at most 4096 bytes, parenthesis depth at most 16, at most 64 upper operations
after re-import). An export that would break any of these refuses 67 LIMIT with no stdout; it never
writes unimportable C. The other refusals are the same as lowering (section 5): malformed envelope 65,
non-S1 unit 68, bad command or name 64. `<name>` is `calc` unless given (`c-export NAME`,
C-import-profile name rules).

Required boundaries: every F1 positive and every accepted boundary control of `c_import_controls.py`
(including operation-capacity, the 61-term `a+...+a` chain at 64 operations, and nesting-limit, 16
redundant parentheses that export without any) must export, re-import to the same semantic identity
(section 4), and evaluate equal. A required boundary that refuses is a FAIL of the export lane, not an
accepted limit.

## 7. Process and concurrency rules

Every tool reads stdin to EOF, writes stdout only on success, names its refusal on stderr, and exits
with the statuses above. The #621 core and the importer use fixed global buffers and are not reentrant:
harnesses call them as separate processes, one at a time per artifact; no in-process sharing.

## 8. Fixture identities F1 (frozen)

F1 = the 35 positive and 48 refusal sources of `tests/universal/c_import_gate.py` at this commit,
identified by the SHA256 of their exact bytes (`tests/universal/translation/fixtures-f1.json`, generated
from that file and checked against it), plus the 15 boundary controls of `c_import_controls.py`. New
fixtures get new IDs in a new fixture set; F1 IDs are never reused or edited.

## 9. Lanes and path ownership (one writer per path)

| lane | owner | paths |
|---|---|---|
| integration, contract, fixtures, build, matrix (#624) | coordinator | `spec/universal/translation-v1.md`, `tests/universal/translation/fixtures-*.json`, `mk/translation.mk`, `docs/universal/MATRIX.md` |
| lowering + native realization (compiler/core) | compiler lane | `src/adapters/upper-lower.om`, `tools/adapters/lower`, `tools/adapters/build-lower.py`, `mk/lower.mk`, `spec/universal/upper-lower-v1.md`, `tests/universal/translation/lower_selftest.py` |
| C export (#622) | C adapter lane | `src/adapters/c-u64-export.om`, `tools/adapters/c-export`, `tools/adapters/build-c-export.py`, `mk/c-u64-export.mk`, `spec/universal/c-u64-export-v1.md`, `tests/universal/translation/c_export_selftest.py` |
| independent validation (#623) | validation lane | `tests/universal/translation/validate*.py`, `tests/universal/translation/mutants/`, `mk/translation-validate.mk` |
| bindings, six callers, artifact manifests | bridge lane | omega-bridge repository |
| Rust (#626), Python (#627), C++ (#739), Zig (#740), Go (#741) | one agent each, after G1 | `src/adapters/<lang>-u64-*.om`, `spec/universal/<lang>-u64-*-v1.md`, `tools/adapters/<lang>-*` |
| upper registry extension for C++/Zig/Go | #621 codec owner only | `src/source-aware/core.om`, `spec/universal/source-aware-v2.md` |

The validation lane does not author lowering or export code and the authors do not certify their own
work. Shared files above are written only by their owner; other lanes send changes to the owner.

## 10. Status vocabulary (matrix)

Each language x profile x direction carries four separate flags: PLANNED, IMPLEMENTED, TESTED (the
lane's own tests ran), QUALIFIED (independent validation PASS on an exact reviewed head plus
exact-head CI). Evidence is labelled REFERENCE / DIFFERENTIAL / PROOF and NATIVE / EMULATED /
NOT_RUN. Anything not run is NOT_RUN, never PASS.
