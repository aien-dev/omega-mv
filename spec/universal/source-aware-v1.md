# Source-aware upper layer v1

Issue #621. Parent contract: omega.universal.semantics.v1 at stacked base
69790fb4ad1e803ca79245430745b96129d1dcf4. This additive, finite interchange
contract does not change OSC, Omegese, checker2, policy or receipt formats.
Independent review is pending. No general importer, converter or proof claim.

## Bounds and identities

A unit is a straight-line typed expression with 1..64 SSA results. Its last
result is the unit result. At most six distinct input slots, no functions,
branches, recursion, heap, floating point, exceptions implemented as execution,
concurrency, arbitrary dialect extensions or native FFI. Source at most 4096
bytes. Input at most 8192 bytes. Oversized values refuse before indexing.

Semantic identity = SHA256(`omega.source.semantic.v1` || canonical semantic
bytes). These bytes include every language/profile/runtime/ABI/dialect/version,
operation/type/attribute/effect/alias/obligation/operand/immediate field and the
32-byte hash of spec/universal/semantics.md. Formatting, spans, raw source and
target selection are excluded. Provenance identity = SHA256(
`omega.source.provenance.v1` || complete canonical envelope). No target
realization identity is minted here. These are intensional content identifiers,
not proofs of behavioral equivalence, and SHA256 collision resistance is an
assumption. Changing profile definitions requires a new version, not reusing IDs.

Raw bytes, source encoding and byte spans live only in the sidecar. Verified
association checks both semantic and source SHA256. This detects inconsistent
association, not a malicious importer's false claim that text means an IR.
No parser establishes that claim in this slice. Private source must remain local.
Exact replay exports stored bytes; it never reconstructs source from lower IR.

## Canonical wire format

All integers unsigned little endian, all lengths exact; no padding, varints,
implicit NUL or trailing bytes. Numbers below are offsets from each record.

Envelope: ASCII OMSB (0..3), version=1 (4), semantic length u32 (5), sidecar
length u32 (9), semantic bytes (13), optional sidecar bytes. Sidecar length zero
means absent, not empty source. Semantic-only units may decode/encode/identify;
replay returns PROVENANCE_MISSING. Input total must equal 13+both lengths.

Semantic header (44 bytes): ASCII OMSA (0), version=1 (4), language (5), profile
(6), dialect version=1 (7), runtime (8), ABI (9), operation count u16 (10),
SHA256 of exact parent semantics.md bytes (12). Then count 20-byte operations.

Operation: importer dialect (0), op version=1 (1), opcode (2), result type (3),
arithmetic attribute (4), effects bitset (5), alias set (6), obligations bitset
(7), operand A u16 (8), operand B u16 (10), immediate u64 (12). Result index is
record order. An operand must reference an earlier result of the specified type.
Unused operands MUST be 65535; unused immediate MUST be zero. There are no ignored
fields. Input slot immediates 0..5 must be unique in the unit.

Sidecar: ASCII OMSP (0), version=1 (4), encoding (5), source length u32 (6),
SHA256 of semantic bytes (10), SHA256 of source bytes (42), count pairs of
(start u32,end u32) (74), then exact source bytes. Spans are half-open byte offsets
0<=start<=end<=source length; empty/synthetic span is [0,0]. UTF8 spans must be on
codepoint boundaries. Encoding 0 preserves arbitrary bytes including invalid
UTF8; encoding 1 validates strict UTF8 (no overlong, surrogate or >U+10FFFF).
Raw source may contain NUL, high bytes, CRLF and invalid text sequences.

## Typed registry

Languages/dialects: 1=C17, 2=Rust2021, 3=Python3, 4=OSC5. Each operation's dialect
must equal its header language. Types: 1=u64, 2=Python builtin arbitrary-precision
int, 3=Python object (operator overloads preserved), 4=symbolic byte-view binding.
Byte-view is an upper symbolic input; it never becomes an invented OscScalar.

Profiles: 1=c17-uint64-pure-v1 (language1); 2=rust-u64-wrapping-pure-v1 (language2);
3=cpython-int-pure-v1 (language3); 4=python-explicit-u64-pure-v1 (language3);
5=osc-v5-u64-pure-v1 (language4); 6=python-dynamic-retained-v1 (language3).
Profile6 is a retention-only extension: dynamic calls, symbolic byte access,
exceptions and unknown effects remain unresolved. It supplies no executable
Python semantics. Profile1/2/4/5 permit only u64 inputs/constants/arithmetic.
Profile3 permits only builtin-int inputs/constants/arithmetic. Profile6 permits
all registry types but u64 arithmetic remains explicit-mask semantics.

Runtime: 0=none; 1=CPython3.12 model; 2=CPython3.13 model. ABI: 1=abstract-pure-v1,
2=cpython-object-v1 (symbolic only, no layout/FFI support). Profiles1/2/4/5 require
runtime0 ABI1; profile3 requires runtime1 or2 ABI1; profile6 requires runtime1 or2
ABI2. These versioned models do not claim support for arbitrary Python releases.

Arithmetic attributes: 0=none; 1=modulo2^64; 2=builtin-bigint; 3=dynamic;
4=byte-access. Effects: read1,write2,allocation4,IO8,exception16,unknown32.
Alias sets: 0=none, 1..8=importer-declared may-alias partition, 255=unknown/top.
Different nonzero partitions are not a proof of disjointness. Obligations:
1=input-domain, 2=nonzero-divisor, 4=source-explicit-masking,
8=runtime-model, 16=alias-and-bounds-validation. Obligations are required exactly,
never a caller-controlled 'proved' flag. There is no discharge mechanism here.

| Op | Meaning | Required shape |
|---|---|---|
| 1 | constant | type1 or2; attr1 or2 respectively; effects/alias/obligations0; unused operands; u64 magnitude (nonnegative bigint subset) |
| 2/3/4/5 | add/sub/mul/div | type1 or2; same typed A/B; immediate0; attr1/2; u64 effects0 alias0, bigint effects20 alias255; obligations below |
| 6 | Python dynamic call | profile6, type3 attr3 effects63 alias255 obligations8; A object, unused B, immediate0 |
| 7 | symbolic byte read | profile6, type1 attr4 effects17 alias1..8 or255 obligations24; A view, B u64, immediate0 |
| 8 | symbolic byte write | profile6, type1 attr4 effects19 alias1..8 or255 obligations24; A view, B u64, immediate=earlier type1 SSA value-to-store; no execution/lowering |
| 9 | external input | attr0 effects0; no operands; immediate unique slot0..5; type4 alias1..8 or255, otherwise alias0; obligations1 |

Arithmetic obligations: u64 1, plus2 for div, plus4 for profiles4/6; bigint8,
plus2 for div. Bigint division means Python floor division including negative
input/results. No wrapped-u64 substitution. Type2 constants here are restricted
to unsigned64 magnitudes; negative/large constants need a future version.
Op8 returns the written u64 value, with a pending byte-range obligation in addition
to its alias/bounds/runtime obligations. Legality is always UNSUPPORTED to OSC.

## Native core API and commands

Production core src/source-aware/core.om is handwritten seeded Omega. It uses
fixed global input/output buffers and typed operation arrays. `decode n` validates
the complete unit and leaves output length zero on failure; `encode` reconstructs
the canonical bytes from validated fields. `perform mode n` resets output first,
decodes, and prepares all output before any write. Return code zero means success.
This whole-unit interface matches the qualified grammar; no shared-object ABI,
thread safety or reentrant library is claimed. External adapters must serialize
calls. Failed decode may leave private scratch fields; no result may be used.

`tools/source-aware/sa encode|decode|identity|provenance|replay|legality` reads a
canonical envelope on stdin and writes bytes on stdout. encode/decode produce
identical canonical envelopes. identity/provenance emit 32 raw digest bytes.
legality uses four prefix bytes before the envelope: target(1=upper retention,
2=OSC), available runtime(0..2), ABI(1..2), allowed effects(0..63). The reply is
one pair (legality,obligations) per operation. Invalid target capabilities refuse.
Statuses: 1=LEGAL (upper retention only); 2=REQUIRES_RUNTIME;
3=LOWERABLE_WITH_OBLIGATIONS; 4=UNSUPPORTED. Missing runtime on bigint/dynamic
operations returns2; provided runtime never makes them lowerable to OSC.
For target2 only u64 inputs/constants and modulo u64 arithmetic can return3,
with their exact obligations; all others return4. Missing ABI or effect allowance
returns4 first. No status certifies a lowering or grants capability authority.

Named refusals/status: 64=BAD_COMMAND, 65=MALFORMED, 66=UNKNOWN_VERSION,
67=LIMIT, 68=PROFILE_MISMATCH, 69=UNKNOWN_OPERATION, 70=TYPE_MISMATCH,
71=EFFECT_ALIAS_MISMATCH, 72=OBLIGATION_MISMATCH, 73=PROVENANCE_MISSING,
74=PROVENANCE_MISMATCH, 75=SOURCE_SPAN, 76=SOURCE_ENCODING, 77=IO,
78=CONTRACT_MISMATCH. Semantic failure produces no stdout; named message on stderr.
The CLI buffers output into an exclusive temporary directory before stdout copy.
Direct native code validates everything before output. OS I/O failure can of
course interrupt a stream; no atomic multi-byte pipe write is claimed. Neither
command manages destination paths: shell redirection truncates before invocation.
Use caller-owned temporary files and rename on success for durable destinations.

## Existing lower interfaces and trust boundary

Omegese's `osc_compile` produces existing OscUnit values. Existing
`osc_ir_encode`, `osc_ir_decode`, `osc_ir_validate` and `c2_check_bytes` remain the
only lower serializers/checkers. Upper bytes are never passed as OSC and never
labelled lower proof. A future importer must fill these typed upper records;
a future lowering must consume legality obligations, produce unchanged OSC,
and submit that OSC through both existing decoder/validator and checker2.
The test-only C adapter exercises exactly these existing APIs, using legacy
host orchestration openly. No production C compiler/runtime is added.

Issue #623 owns source-to-upper/OSC translation validation and signed receipts.
This layer provides exact semantic/provenance digests to bind there, but cannot
mint an approved receipt or certify source correspondence. Target/transform,
source profile, obligations and validation status must be bound by that future
adapter through reviewed existing receipt versioning. Full local judge and
independent API review remain publication gates.

## Qualification plan

Seeded native compile only, pinned immutable source archive and existing bootstrap
entry; existing native SHA256 core imported without edits and hash-checked.
Independent Node buffers/crypto generate and check 10000 valid and 100000
structurally malformed requests, with category membership, consumed-input hashes,
per-response actual counts, replay/identity/output assertions and declared seeds.
Test-only framed native entry calls the same core. Public commands and direct
core failure-after-success probes must leave no partial semantic output or stale
PASS receipt. Compiled semantic mutants must compile and run before being counted;
byte corruptions test source/span/profile/version/effect/runtime/length refusals.
Legacy fixture hashes are compared to pinned-base encodings through unchanged
OSC and checker2. Receipts are unsigned development evidence only.

The build defaults to build/L574. SA_BUILD may select a child of that scratch
area; symlinks, hardlinked files and special files there refuse before writes.
SA_SEED_REPO may point to another local clone containing the exact pinned bridge
commit. Only committed archived sources are consumed. This is an explicit pinned
source dependency, not an embedded or silently downloaded compiler binary.
