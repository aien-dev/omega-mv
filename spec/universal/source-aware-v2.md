# Source-aware upper layer v2

Issue #621, source-aware v2 (ledger L832). Additive to `source-aware-v1.md`, which is not edited and
stays normative for every rule this file does not change. Every v1 unit, byte, identity, verdict and
status is unchanged: a v1 unit decodes, encodes, identifies, replays and gets legality exactly as under
v1. v2 adds three source languages and three profiles for restricted pure-u64 C++, Zig and Go units
(issues #739, #740, #741). Independent review is pending (#623 writes an independent v2 reference
decoder from this file alone). No importer, exporter, lowering, converter or proof claim.

## 1. What changes on the wire

Exactly one byte gains a value: the semantic header version (OMSA offset 4, envelope offset 17) may be
1 or 2. Everything else in the canonical wire format of `source-aware-v1.md` is unchanged:

| field | v1 | v2 |
|---|---|---|
| OMSB envelope version (envelope offset 4) | 1 | 1 |
| OMSA semantic header version (OMSA offset 4) | 1 | 1 or 2 |
| dialect version (OMSA offset 7) | 1 | 1 |
| operation version (operation offset 1) | 1 | 1 |
| OMSP sidecar version (OMSP offset 4) | 1 | 1 |

Any other value of any of these bytes refuses 66 (UNKNOWN_VERSION), as in v1. Record sizes (13-byte
envelope prefix, 44-byte semantic header, 20-byte operations, 74-byte sidecar header, 8-byte spans),
bounds (1..64 operations, source at most 4096 bytes, input at most 8192 bytes, semantic length at most
1324, sidecar length at most 4682), magic numbers, the parent `spec/universal/semantics.md` hash, the
sidecar rules and the command interface are those of v1.

## 2. Registry additions

Languages / dialects (OMSA offset 5; operation offset 0 must equal it, as in v1):

| code | language | pin |
|---|---|---|
| 5 | C++17 | ISO/IEC 14882:2017 |
| 6 | Zig 0.17 | Zig 0.17 language release |
| 7 | Go 1.27 | Go 1.27 language version (`go 1.27`) |

Profiles (OMSA offset 6):

| code | name | language | semantic header version |
|---|---|---|---|
| 7 | cpp17-uint64-pure-v1 | 5 | 2 |
| 8 | zig-u64-wrapping-pure-v1 | 6 | 2 |
| 9 | go-u64-pure-v1 | 7 | 2 |

Each of profiles 7, 8 and 9 has exactly the typed-registry row of profile 1 (c17-uint64-pure-v1):

- runtime 0 and ABI 1 only;
- type 1 (u64) only; every other type refuses 68 (PROFILE_MISMATCH);
- operations 1, 2, 3, 4, 5 and 9 only; operations 6, 7 and 8 refuse 68;
- operation fields exactly as profile 1: constant (op 1) attribute 1, effects 0, alias 0, obligations
  0, operands unused (65535); add/sub/mul/div (ops 2/3/4/5) attribute 1 (modulo 2^64), effects 0,
  alias 0, immediate 0, both operands earlier type-1 results, obligations 1 for ops 2/3/4 and 3
  (input-domain + nonzero-divisor) for op 5; input (op 9) attribute 0, effects 0, alias 0, obligations
  1, no operands, unique slot immediate 0..5. Obligation 4 (source-explicit-masking) is never part of
  these profiles; a unit carrying it refuses 72 (OBLIGATION_MISMATCH).
- legality is computed exactly as for profile 1 (v1 "Native core API and commands"): profiles 7/8/9 are
  neither profile 3 nor profile 6, so for target 2 u64 inputs, constants and modulo arithmetic return
  3 (LOWERABLE_WITH_OBLIGATIONS) with their exact obligations when ABI and effects are allowed.

Meaning of the u64 operations for each source language (what an importer must establish before it may
emit the record; the codec checks only the record shape):

- C++17 (7): `std::uint64_t` / `uint64_t` values; `+ - *` on two `uint64_t` operands are modulo 2^64
  ([basic.fundamental] unsigned arithmetic). The profile assumes an implementation whose `int` is at
  most 64 bits wide, so integral promotion never applies to `std::uint64_t`; the importer refuses any
  expression whose operand types are not exactly `std::uint64_t`.
- Zig 0.17 (8): `u64` values with the wrapping operators `+%`, `-%`, `*%`, which are modulo 2^64. The
  plain operators `+`, `-`, `*` have checked illegal behavior on overflow and are not modulo 2^64; an
  importer must not map them to ops 2/3/4.
- Go 1.27 (9): `uint64` values; `+ - *` on unsigned integers are modulo 2^64 by the language
  specification.

Division (op 5) stays representable exactly as in profile 1 (nonzero-divisor obligation). Like the C
importer, an importer for these profiles refuses division at the source level; in all three languages
division by zero is not a value (C++ undefined behavior, Zig illegal behavior, Go run-time panic).

## 3. The profile fixes the version

The semantic header version is a function of the profile:

- profiles 1..6 are valid only with semantic header version 1;
- profiles 7..9 are valid only with semantic header version 2.

(version 2, profile 1..6) and (version 1, profile 7..9) refuse 68 (PROFILE_MISMATCH). Profile 0 and
profiles above 9 refuse 68 under either version. Consequence: every valid unit has exactly one
admissible semantic header version byte, so no unit has two canonical encodings and therefore two
identities.

A v1 profile 7..9 unit already refused 68 under v1 (profiles above 6 were unknown), so this rule does
not change any v1 verdict.

## 4. Identities

The domain strings are unchanged for both versions: semantic identity = SHA256(
`omega.source.semantic.v1` || canonical semantic bytes); provenance identity = SHA256(
`omega.source.provenance.v1` || complete canonical envelope). The `.v1` suffix names the hashing
construction, not the semantic header version.

No v2 identity can equal a v1 identity unless SHA256 collides: the hashed semantic bytes start with
`OMSA` followed by the semantic header version byte, which is 1 for every v1 unit and 2 for every v2
unit. Two preimages with the same domain prefix and different bytes at that fixed offset are different
byte strings. The provenance preimage contains the semantic bytes at fixed envelope offset 13, so the
same argument holds there (envelope offset 17). Section 3 makes the version byte a function of the
other header fields, so the identity of a given unit is unique as well.

Changing the meaning of profiles 1..6 is still not allowed; v2 adds codes only.

## 5. Decode order (normative for v1 and v2)

Statuses are those of v1 (64..78). A decoder checks in this order and returns the first failure; the
same order gave every v1 verdict, and v2 adds only the version and profile cases marked "v2".
`b` is the envelope, `s` = semantic bytes starting at envelope offset 13, `p` = sidecar bytes.

1. total length > 8192: 67; total length < 13: 65; `b[0..4]` != `OMSB`: 65; `b[4]` != 1: 66.
2. semantic length > 1324 or sidecar length > 4682: 67; total != 13 + both lengths: 65;
   semantic length < 44: 65; `s[0..4]` != `OMSA`: 65.
3. v2: `s[4]` not in {1, 2}: 66. `s[7]` (dialect version) != 1: 66.
4. operation count 0 or > 64: 67; semantic length != 44 + 20 x count: 65; parent hash
   `s[12..44]` != SHA256(`spec/universal/semantics.md`): 78.
5. Profile row (all 68): profile 0 or > 9; language != the profile's language; v2: (version,
   profile) pairing of section 3; runtime/ABI not allowed for the profile.
6. For each operation i in record order: dialect byte != header language: 68; operation version != 1:
   66; type 0 or > 4: 70; type not allowed by the profile: 68; opcode 0 or > 9: 69; then the
   operation row. For profiles 7/8/9 (type is 1 here) the rows are checked in this order, the same
   order as profile 1 under v1:
   - op 1: attribute != 1: 70; A != 65535: 70; B != 65535: 70; obligations != 0: 72; effects != 0 or
     alias != 0: 71. The immediate is the constant (any u64).
   - ops 2/3/4/5: attribute != 1: 70; A not an earlier (< i) type-1 result: 70; same for B: 70;
     immediate != 0: 65; effects != 0 or alias != 0: 71; obligations != 1 (ops 2/3/4) or 3 (op 5): 72.
   - ops 6/7/8: 68.
   - op 9: attribute != 0: 70; A != 65535: 70; B != 65535: 70; immediate > 5: 67; immediate equal to
     the immediate of an earlier op 9: 70; obligations != 1: 72; effects != 0: 71; alias != 0: 71.
   Profiles 1..6 keep their v1 rows unchanged.
7. If the sidecar length is nonzero (`p` at envelope offset 13 + semantic length): sidecar length
   < 74: 65; `p[0..4]` != `OMSP`: 65; `p[4]` != 1: 66; encoding > 1: 76; source length > 4096: 67;
   sidecar length != 74 + 8 x count + source length: 65; `p[10..42]` != SHA256(s): 74;
   `p[42..74]` != SHA256(source): 74; encoding 1 and source not strict UTF-8: 76; then for each span
   in order: start > end or end > source length: 75; encoding 1 and start or end not on a codepoint
   boundary (a boundary is the source length or a byte outside 0x80..0xBF): 75.

`perform mode n` checks first: n > 8192: 67; mode 0 or > 6: 64. For `legality` (mode 6) the four
capability bytes are checked next exactly as in v1 (fewer than 4 bytes: 65; target 0 or above 2,
runtime above 2, ABI 0 or above 2, or effects above 63: 68), then the envelope after them.

`encode` and `decode` emit the canonical envelope with the decoded semantic header version: decode
followed by encode returns the input bytes exactly for v1 and for v2 units.

## 6. Compatibility with v1 consumers

The frozen #621 codec and `tests/universal/source-aware/reference.mjs` refuse every v2 unit with 66
(UNKNOWN_VERSION) at step 3, before reading any v2 field, and write no output. The consumers built on
the v1 core decode with it and report their own refusal: the lowering (`tools/adapters/lower`) and
the C exporter (`tools/adapters/c-export`) exit 65 (`LOWER_MALFORMED`, `C_EXPORT_MALFORMED`) with no
stdout, exactly as for a v1 envelope with an unknown semantic header version. The C importer reads C
source, not envelopes, and emits only v1 profile 1 units. Adopting v2 in a consumer is a separate,
coordinator-planned change; this file does not change any consumer.

## 7. Implementation and build

`src/source-aware/core.om` (the v1 core, sha256 `24c0837d05990fcb1daaafec13d2408d4359ae4fa6c9866307b73c5132c8b7eb`)
stays byte-identical, because the lowering (`build/L831`) and C exporter (`build/L622`) builds prepend
that path and their dispatchers pin its bytes. The v2 codec is the single handwritten seeded Omega file
`src/source-aware/core-v2.om` with the same non-reentrant fixed-buffer API (`decode n`, `encode`,
`perform mode n`); it accepts semantic header versions 1 and 2. After a successful `decode n` the global
`M 8` holds the decoded semantic header version, next to the v1 fields `M 0..7`; consumers that
prepend `core-v2.om` read the version there. `tools/source-aware/build-v2` compiles
`src/source-aware/vendor/sha256.om`, `core-v2.om` and the unchanged `src/source-aware/main.om` with
the same pinned bridge seed compiler as v1 (`191b776e…98e9`) into `build/L832/sa`; the commands are
`SA_BUILD=build/L832 tools/source-aware/sa encode|decode|identity|provenance|replay|legality`.

`make -f mk/source-aware-v2.mk test-source-aware-v2` runs the compatibility gate
`tests/universal/source-aware/v2_compat.mjs`: frozen-v1 equivalence for every input whose envelope
byte 17 is not 2, frozen-codec refusal of v2, the profile-1 corpus retargeted to profiles 7/8/9, stale
state across interleaved requests, and the unchanged v1 gates. Receipts are unsigned development
evidence only.
