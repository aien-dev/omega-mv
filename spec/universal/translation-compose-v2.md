# Translation composition contract v2 (#624 G3)

Status: FROZEN for G3 (C++, Zig, Go) on 2026-10-25 by the #624 integration coordinator. Additive:
`translation-v1.md` and `translation-compose-v1.md` stay unchanged and govern everything this file does
not extend. Envelope: `source-aware-v2.md` (#621 v2, ledger L832, branch `universal/621-source-aware-v2`,
spec sha256 `ea2e1465…ce1c` at 02283530): semantic header version 2 for profiles 7/8/9 only.

## 1. Extended S1 family

| member | OMSA version | language / profile / runtime / ABI | op dialect | arithmetic obligations |
|---|---|---|---|---|
| C | 1 | 1 / 1 / 0 / 1 | 1 | 1 |
| Rust | 1 | 2 / 2 / 0 / 1 | 2 | 1 |
| Python | 1 | 3 / 4 / 0 / 1 | 3 | 5 |
| C++ | 2 | 5 / 7 / 0 / 1 | 5 | 1 |
| Zig | 2 | 6 / 8 / 0 / 1 | 6 | 1 |
| Go | 2 | 7 / 9 / 0 / 1 | 7 | 1 |

Inputs and constants have the fields of `translation-v1.md` section 2 in every member. Division (op 5)
is outside the family although profiles 7/8/9 can represent it; every family tool refuses it with 68.

## 2. R, N and the retarget tool

`R(U, M)` (compose-v1 section 2) additionally sets the OMSA version byte to M's version, and always
yields a semantic-only envelope (sidecar length 0). A sidecar binds the source member's semantic bytes
and source text, so carrying it across a retarget would bind the wrong language; provenance stays with
U. `N(U) = R(U, C)` is a v1 C unit for every member, so the language-neutral identity, the lowering and
every G1 tool are unchanged, and `N(R(U, M)) = N(U)` for all six members.

`tools/adapters/retarget M` (Omega source `src/adapters/upper-retarget.om`, built on the v2 codec
`src/source-aware/core-v2.om`) reads one envelope of any family member and writes `R(U, M)`;
`M` is one of `c rust python cpp zig go`. Refusals: malformed 65 (or the decoder's status 66..78),
not in the family 68, bad command 64. Output is byte-deterministic.

## 3. Exporters and importers

- C++/Zig/Go importers emit units of their own member (version 2) with an OMSP sidecar, as v1
  importers do, and declare whether they append a normalization pair (compose-v1 section 3).
- C++/Zig/Go exporters decode with the v2 codec and accept every member of the extended family.
- The v1-core tools (`c-import`, `lower`, `c-export`, Rust and Python adapters) are unchanged and keep
  refusing version 2 units with empty stdout: the v1 codec `sa` with 66 (UNKNOWN_VERSION); the tools
  with 65 (`*_MALFORMED`), because translation-v1 sections 5 and 6 map every decode failure other
  than 68 to 65. (Amended 2026-10-25 after the Go lane measured 65; the first text said 66 for all.)
  A composition into a v1-core exporter, and the native realization, use `retarget`:
  `export_B(R(U_A, B))` and `lower(N(U_A))`. Moving those tools onto the v2 codec is a separate change
  and not part of G3.

## 4. Compositions and evidence

Compose-v1 sections 4 and 5 apply unchanged to all 30 directed compositions of the six members, with
`retarget` inserted where section 3 requires it. Fixture sets: `fixtures-f2-cpp.json`,
`fixtures-f2-zig.json` and `fixtures-f2-go.json` (IDs `F2-<lang>-<F1 id>`). Oracles, declared and
pinned in each profile spec: g++ (`-std=c++17 -O2 -Wall -Werror`), Zig 0.17.0, Go 1.27.3 (user-space
toolchains in `~/.local/toolchains`, sha256 verified against the official release indexes).

Semantics each importer must establish before emitting ops 2/3/4 (`source-aware-v2.md` section 2):
- C++17: both operands exactly `std::uint64_t` or `uint64_t`; no integral promotion, no mixed types.
- Zig: only the wrapping operators `+%`, `-%`, `*%`; plain `+ - *` refuse (checked illegal behavior).
- Go: `uint64` operands; `+ - *` are modulo 2^64 by the language specification; untyped constant
  expressions are exact and arbitrary precision at compile time, so any expression with no `uint64`
  variable operand must refuse or be shown to fit (decide in the Go profile spec, and test).

## 5. Ownership (adds to the earlier tables)

| lane | paths |
|---|---|
| retarget (compiler/core lane) | `src/adapters/upper-retarget.om`, `tools/adapters/retarget`, `tools/adapters/build-retarget.py`, `mk/retarget.mk`, `tests/universal/translation/retarget_selftest.py` |
| C++ (#739) | `src/adapters/cpp-u64-*.om`, `tools/adapters/cpp-*`, `tools/adapters/build-cpp-*.py`, `mk/cpp-u64.mk`, `spec/universal/cpp-u64-v1.md`, `tests/universal/translation/cpp_selftest.py`, `fixtures-f2-cpp.json` |
| Zig (#740) | the same pattern with `zig` |
| Go (#741) | the same pattern with `go` |
| v2 reference decoder and G3 composition validation (#623) | `tests/universal/source-aware/v2_reference*`, `tests/universal/translation/validate_compose*.py` |
