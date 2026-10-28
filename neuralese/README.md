# Neuralese supplement to Omega

Status: **PROPOSED** learned-representation design. The bounded, hand-authored
codebook below is **IMPLEMENTED / EXERCISED** on the host, not learned reasoning or
hardware **QUALIFIED**. It remains a pending implementation candidate. Omega keeps
its name. Neuralese is a representation supplement, not a second execution
language, compiler mode, authority source or bypass.

Source baseline: `aien-dev/omega-mv` main
`6999fa9e6dae4847c155ef4eacaece74f491d3ca` (2026-10-28).
Compiler: committed `seed/out/omc`, SHA-256
`d6290c7663c2b4bafb59eb3fdf812476e977f4c144def71095c6248ef9b96b84`.
Compiler source SHA-256:
`3f937160c823d3a90a6b4cc296f00f517f36ede7fd27a06505c998985ec62240`.
Authority: `docs/one_layer/OMEGESE.md`, sections 9, 10 and 12, and
`seed/omc/omc.om` at that baseline. No syntax changes are proposed by this patch.

## Proposed learned-representation envelope

An untrusted producer may submit a learned representation for communication or an
intermediate proposal. The envelope must bind all of the following:

- Envelope version and an unambiguous framing length, with a receiver-owned bound.
- Producer model identity: an immutable model artifact digest plus architecture,
  tokenizer/preprocessor and representation-layer/version identities. A friendly
  model name alone is insufficient. A non-model producer must identify itself as
  such; it cannot claim a model identity.
- Decoder identity: immutable decoder artifact digest, codebook/schema version,
  decoder configuration and exact compatible producer identities.
- Dimension/shape, dtype, byte encoding, byte order and canonical serialization.
  A decoder's registry entry fixes the permitted values and payload limit.
- Checksum algorithm and digest of the metadata and payload together. A checksum
  detects accidental corruption; it authenticates nobody and grants no authority.
- Context/task binding when representations depend on a task or preceding state.
  A future contextual decoder must reject missing, stale or mismatched context.

Identity pairs are exact registry keys. Vectors from different models, layers,
quantizations or decoder revisions are not interchangeable even if their shape
matches. Registry admission is a separate reviewed change, not a model decision.
The proposed learned format is not accepted by this reference implementation.

## Decode boundary and normal checked execution

1. Bound and frame the entire input; reject unsupported identities, shapes,
   encodings, checksum mismatches and noncanonical fields before emitting output.
2. Decode only into explicit Omega source or a separately specified explicit
   operation representation. Keep the original envelope and decoded artifact for
   replay; never treat payload bytes, logits or model confidence as instructions.
3. Feed the explicit artifact into the ordinary compiler. Apply the same type,
   ownership, bounds, effect and capability admission policy that applies to a
   source-text proposal. Never grant a decoder a privileged execution entry point.
4. Runtime checks and the actual receiver's capabilities remain authoritative at
   execution. Validate the resulting behavior with independent task acceptance
   checks, not a second assertion by the producer or decoder.

The current native compiler's host words use ambient process permissions
(`seed/host/README.md`); compiling a host operation does **not** establish capability
confinement. This supplement does not invent or claim such confinement. Its
implemented codebook emits only pure scalar operations, no host/device words,
capability handles, resources or effects. Consequently no effectful request is
admissible in this implementation. A future effectful consumer must use its
existing checked capability/effect executor; absent that executor, it must refuse.

Contracts use `om2 debug`, not release erasure. This is not a sandbox for arbitrary
source, and the test runner executes only the closed, hand-authored codebook.

## Implemented bounded reference format: NLE1

This is an intentionally small discrete representation, **not a latent vector**.
All fields are unsigned bytes. Byte offsets and lengths below are bookkeeping.
The complete frame is exactly 78 bytes; a 79-byte read buffer detects oversize
without allocating from an untrusted length. The reader handles short reads and
requires EOF. Read/write errors are failures, not successful empty artifacts.

| Offset | Bytes | Meaning and required value |
| --- | --- | --- |
| 0 | 4 | ASCII `NLE1` |
| 4 | 1 | Producer registry id `1`: `hand-authored-codebook-fixture/v1`, no model |
| 5 | 1 | Decoder registry id `1`: `omega-neuralese-codebook/v1`, source `decode.om` |
| 6 | 1 | Dimension `4` (four payload bytes, not a learned feature dimension) |
| 7 | 1 | Dtype id `1`: unsigned 8-bit integer |
| 8 | 1 | Encoding id `1`: raw bytes, byte order not applicable to one-byte scalars |
| 9 | 1 | Payload length `4` |
| 10 | 1 | Opcode: `1` increment, `2` double, `3` identity |
| 11 | 1 | Operand, unsigned byte; ordinary decoded contract accepts at most `16` |
| 12 | 2 | Reserved, must both be zero |
| 14 | 64 | SHA-256 of bytes `[0,14)`, canonical lowercase hexadecimal in FIPS 180-4 digest order |

The explicit registry names above bind to the candidate source revision and its
recorded SHA-256 in `host-results.json`; numeric ids are not globally portable
model names. Any decoder or codebook change requires a new reviewed registry
version, not reinterpretation of an existing envelope. NLE1 is proposed until
review/merge and is not a public or production wire protocol.

Each opcode emits the complete source for `apply(x: uint)` with `requires le x 16`
and `ensures le result 32`, followed by `main`. The operand is emitted as canonical
decimal text, never copied as source bytes. The compiler emits the normal type,
arithmetic and contract checks. An input operand of `17` is a syntactically valid
proposal but the ordinary runtime precondition refuses it (exit `109`). Validation
is not a certificate that a task was solved.

Refusal statuses: `10` oversize; `11` magic/version; `12` unknown producer/decoder
pair; `13` dimension; `14` dtype; `15` byte encoding; `16` short frame; `17`
checksum; `18` reserved field; `19` unknown opcode; `20` payload length. `80` is
input I/O failure and `81` output I/O failure. All validation refusals emit no
source. SHA-256 reuses `om/tls/sha2.om`; there is no host decoder fallback.

## Host reproduction and verification scope

From the repository root on Linux AArch64:

```sh
sha256sum seed/out/omc seed/omc/omc.om
mkdir -p build
if seed/out/omc < seed/qualification/om2/gate.om > build/neuralese-gate; then
  chmod +x build/neuralese-gate &&
  build/neuralese-gate seed/out/omc \
    d6290c7663c2b4bafb59eb3fdf812476e977f4c144def71095c6248ef9b96b84 \
    neuralese/host.cases build/neuralese-host
else
  exit 1
fi
```

The scratch directory must not exist, and its exact relative name is part of
`host.cases`: later cases compile the earlier decoder's actual `.out` artifact.
Do not run the gate executable if compiling it returned nonzero. The existing
native Omega conductor checks the compiler hash, concatenates the decoder and
existing program-library SHA implementation, then sends each emitted source
through **that same unchanged compiler** and executes it. No stale output from a
failed compile is run.
The conductor provides bounded child deadlines, exact stdout/stderr/status checks,
exclusive scratch creation and source substitution for mutants. No new host-
language conductor, production library or runtime dependency is introduced.

The committed `.wire` fixtures are independent SHA-256 test data constructed
with Python 3.12.3 standard-library `hashlib` as a read-only verification oracle;
their metadata/payload bytes and exact decoded-source oracles are committed.
The implementation and test conductor remain `.om`. SHA-256 of the first
fourteen bytes can also be compared with the fixture's trailing lowercase digest
using an independent `sha256sum` byte checker. No Python implementation participates
in decoding, compilation or the repeatable host suite.

The suite covers every codebook operation, contract boundary inputs, unknown
producer and decoder separately, dimension/dtype/encoding/length/checksum/magic,
both reserved bytes, both opcode bounds, empty/short/oversize input, and ordinary
compiler type/purity and runtime contract refusals. Every validation return has
an independent success-return mutant, exercised against each applicable refusal
fixture; the mutant must exit successfully and emit source instead of the empty
refusal oracle. A wrong-codebook mutant must change exact emitted bytes and the
subsequently compiled result. An erased precondition must change the ordinary
contract refusal into the operand result. A false postcondition must trap.
SHA digests are independent byte-checker results, not mathematical predictions.

Independent expected arithmetic: WolframLanguageEvaluator query
`{7+1,7*2,7,16+1,16*2}` returned `{8,14,7,17,32}` on 2026-10-28.
The Wolfram mount was verified with `Integrate[x^2,{x,0,1}] -> 1/3` and
WolframAlpha `sqrt(27)/2 -> (3 sqrt(3))/2`. These establish expected arithmetic,
not execution evidence. Measured host results live in `host-results.json`.
No model inference, learned representation, GPU, hardware qualification,
production transport, merge or deployment is claimed.

## Concrete extension proposals and non-requirements

**Required new language features for this patch: none.** Byte framing, bounded
arrays, SHA-256, discrete decoding, source emission, contracts and host testing all
use existing syntax. A fixed-size learned CPU decoder could likewise be written
with existing bounded arrays and scalar arithmetic; its weights, implementation
and independent correctness evidence are missing work, not new keywords.

The following are concrete conditional proposals, not implemented syntax:

1. **Typed effect/capability admission for a general effectful decoder consumer.**
   Add a separately specified compiler/executor interface that accepts explicit
   operations plus receiver-owned capability ids, checks declared effects against
   those ids, and refuses unknown, expired or insufficient capabilities before any
   host/device effect. It must not accept authority from envelope metadata. The
   smallest gate uses an explicit denied file-write operation and proves no file
   exists, then grants only the exact resource and proves a write; a gate-removal
   mutant must be caught. This requires an executor/policy design decision and
   operator review, not a decoder privilege or a change in this patch.
2. **Typed bounded accelerator submission if the later decoder requires it.**
   Specify input/output pool handles, element types/shapes and maximum work before
   adding any host word. Refuse unknown handles, incompatible shape/dtype,
   out-of-range regions and unauthorized submissions. The first test is a host
   simulator plus refusal mutants; only a separately authorized physical run can
   qualify device behavior. Existing low-level device words are not evidence that
   such a typed inference interface or GPU model path exists.

No heap, dynamic tensor sizes, implicit imports, vector coercion, new latent
primitive or arbitrary foreign runtime is required or proposed for the bounded
experiment. Do not add them as a prerequisite by assumption. Protocol framing,
model registry, decoder implementations and task adapters are ordinary Omega
application work, not necessarily language extensions.

## Later bounded experiment: PROPOSED, NOT_RUN

Preregister a task corpus of small pure integer/state-transform programs expressible
in the current compiler. Specify input bounds, outputs, forbidden effects and an
independent reference checker before running a producer. Use the same tasks and
held-out cases for all arms:

- Source-text producer: emits explicit Omega directly.
- Structured-operations producer: emits a fixed operation schema/codebook decoded
  deterministically into explicit Omega.
- Learned-representation producer: emits a model-bound representation consumed by
  an explicitly pinned compatible decoder, then the same explicit Omega boundary.

Pin task corpus, producer/model artifacts, tokenizer, decoder/configuration,
representation location/dimension/dtype/encoding, seeds, prompts, retry budget,
compiler and acceptance checker. Separate codebook fitting/training tasks from
held-out tasks. Do not compare vectors from incompatible producers.

Apply identical compiler checks, kept contracts, receiver capability/effect
policy and independent semantic acceptance checks to every arm. Preserve all
attempts, decoded source and refusals; malformed envelopes, compiler refusals,
contract violations, wrong results and missing evidence remain failures or
NOT_RUN, never silently dropped. Preregister comparison definitions for task
success, checked refusal rate, proposal bytes, total producer plus decoder cost,
latency and reproducibility. Require independent replays and guard-removal/wrong-
result controls. Do not infer private latent reasoning from either success or
compression, and do not claim efficiency before measuring the full path.

No models have been run for this supplement. A later run needs its own authority,
resource reservation where applicable, immutable evidence and separate review.
