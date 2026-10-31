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

## Active reproduction pin (not historical run evidence)

Current pin after the `peername` reseed (#918, 2026-10-31), merged as
`2d848d939b102e53049d66ead554a18f56791abb`: committed
`seed/out/omc` SHA-256
`3874844226c7f29a9b015d30098030ad7a9fcd769eccc2395511de57f7a37a28`, compiler
source SHA-256 `fb095990d0e6a8459bac89024b13f0ad5f8495a2c2ccceb72bae13440a52d637`.
An earlier pin, after merging main `62d1d0d668f897386a0e311374f8dbcc5c32dde8`,
was `62775d2a391cfef7cde64e387655caa9809a267093112910ec76bfa568f3712b` (source
`96d51b63c8f451661ac715395078e04f39195e50c9e8fe37746cda121e4d7a1e`).
The conductor compares the supplied hash to the compiler it actually reads and
refuses a mismatch before case execution. The reproduction commands below use
the active pin; dated observations below describe earlier runs, not this tree.

On future reseeds, update this active binary/source pair and the reproduction
commands only. Dated observations below retain the compiler actually used; add a
new dated result for a new run instead of globally replacing hashes. An active
pin is a reproduction input, not evidence that either Neuralese suite was rerun.

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
    3874844226c7f29a9b015d30098030ad7a9fcd769eccc2395511de57f7a37a28 \
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
not execution evidence. The clean implementation pin exercised was
`39e89fb1c2aaed99ddc05ec565274dd05792b70f`. The host conductor returned
`OM2GATE PASS cases=54 pass=54 fail=0 invalid=0 not_run=0`, including seventeen
guard-removal mutant kills, the wrong-codebook control, and ordinary type,
purity, precondition and postcondition checks. Measured source/artifact hashes,
commands and every verdict are in
[`host-results.287fdc263f7af93107c378ba63c92202a0e6c1479cb1ff6971706b423b0b15fb.json`](host-results.287fdc263f7af93107c378ba63c92202a0e6c1479cb1ff6971706b423b0b15fb.json);
the unedited conductor transcript is
[`host-3163bdc1f56d02803df5061439d0a51156443b339e8e08870a93647894e99222.log`](host-3163bdc1f56d02803df5061439d0a51156443b339e8e08870a93647894e99222.log).
Those files record the baseline-compiler run and are kept unedited. After merging
main `62d1d0d668f897386a0e311374f8dbcc5c32dde8`, the same unchanged `host.cases`
against compiler `62775d2a391cfef7cde64e387655caa9809a267093112910ec76bfa568f3712b`
returned `OM2GATE PASS cases=54 pass=54 fail=0 invalid=0 not_run=0` on
2026-10-30 (host CPU, Linux AArch64).
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

## NL4: canonical numerical payload to closed pure execution

This additive boundary addresses [#920](https://github.com/aien-dev/omega-mv/issues/920)
on draft #919, without replacing its historical NLE1 fixtures or receipts.
It consumes the canonical LTP0 schema and `lpvalid` from
[aien-protocols-mv #69](https://github.com/aien-dev/aien-protocols-mv/issues/69),
not another envelope. Exact immutable native dependency commits, hashes, licenses
and staging paths are in [`nl4-dependencies.json`](nl4-dependencies.json).
The reused NL1 Hadamard unit is handwritten `.om`, licensed AGPL-3.0-only.
Composed qualification executables retain that license; it is not silently
reimplemented in a host language. No permanent foreign decoder/runtime is added.

`proposal.om` implements a bounded native IEEE32 big-endian payload encoder and
a tensor-to-typed-proposal decoder. Rank-one length-eight contiguous f32 and
signed affine i8 payloads are admitted by LTP0; quantization scale and signed
zero-point are checked against the receiver template. The codec itself does not
authenticate a model or grant authority. Native-model profile2 is deliberately
refused by this synthetic-only consumer.

The fixed G0 projection uses Hadamard columns {2,3,5,8}/sqrt(8), producing eight
coordinates from four features. It is not the learned three-coordinate channel.
The first two decoded bits propose opcode1 increment,2 double or3 identity;
opcode0 is refused. The other two propose explicit uint operand0..3.
Scores within1/16 of exactly0 or1 are accepted; everything ambiguous refuses.
These thresholds are frozen correctness fixtures, not tuned held-out results.
The receiver independently supplies the expected opcode and operand on the CLI;
the message cannot provide its own objective oracle.

The proposed `uint[16]` descriptor is explicit:

| Index | Meaning |
|---|---|
|0|opcode1/2/3|
|1|left operand uint0..3|
|2|right operand1 increment,2 double,0 identity|
|3,4,5|rank1, sole dimension1, element count1|
|6,7,8,9|result byte bound4, operation step bound1, required effects0, closed uint type id1|
|10..15|reserved zero|

`nlproposal(frame,proposal)` requires prior successful canonical `lpvalid`;
it proposes, never executes or reserves. All refusal paths preserve the caller's
descriptor. `nlmatch` compares all sixteen fields with a receiver-owned descriptor.
The host harness first validates LTP0 against a local admitted fixture context,
then proposes, compares, and calls ordinary compiled `om2 debug` functions with
`requires x<=16` and `ensures result<=32`. Vectors are never instructions or
dynamically executed source. All nonzero producer effect classes are refused,
even if the synthetic template admits them.

The receipt stream is `NL4R1 decision op lhs rhs rank dim0 dim1 elements effects result`.
Success decision0 is stdout after executing the closed pure function. Refusals
write stderr with a nonzero decision and every other numeric field zero, stdout
empty. These are **HOST_SYNTHETIC** observations, not signed/authorized production
receipts. The fixture's issued time is the receiver's synthetic clock and its
high-water is0. This one-shot pure process has no durable replay ledger.
Shape/type/resource admission and capability-reference equality are necessary
checks, not capability authorization.

LTP0 field101 is the producer-node effect class; fields192/224 preserve existing
producer MJR1 DAG/input-binding semantics. A production caller must verify a
completed producer MJR1.output_digest equals SHA256(entire LTP0), equals the
independently admitted consumer CONTENT digest, and matches generation plus
objective/tenant/subject. Consumer capability/verifier admission is independent.
`lpvalid` does not verify this signed relation. Current native host words have
ambient process permissions, not a capability sandbox. The absent canonical
bindings are concrete owner proposals:
[AEGIS #79](https://github.com/aien-dev/aegis-runtime-mv/issues/79) and
[microjob reservation #70](https://github.com/aien-dev/aien-protocols-mv/issues/70).
They must bind existing authority and lifecycle, never invent a Neuralese grant.
Production admission, durable replay protection and all effectful execution are
**NOT_RUN**.

### Independent math and replay

Raw actual Wolfram tool-call/result events are preserved in `nl4-wolfram-*.jsonl`;
no private model activations or inference traces are included.
Verification calls were `Integrate[x^2,{x,0,1}] -> 1/3` and Alpha
`sqrt(27)/2 -> 3 sqrt(3)/2`. The exact query in `nl4-wolfram-oracle.jsonl`
checks `Transpose[E].E == IdentityMatrix[4]`, all sixteen `X.E^T.E-X` zero,
and all sixteen exact real decodes after IEEE32 rounding. Its exact maximum
feature error is `1-47453131/(33554432*Sqrt[2])`.
That models quantized input decoded in exact arithmetic, not a claim that native
floating-point execution is exact. The native encoder is separately compared
byte-for-byte against independently Wolfram-exported IEEE32 payloads.
`nl4-wolfram-int8.jsonl` queries `Q=Round[16 H]`, `Q.E/16`,
returning maximum exact feature error `-1+3/(2*Sqrt[2])` and all sixteen bits
recovered within the frozen tolerance. Independent ambiguity, wrong-sign and
wrong-scale controls are in `nl4-wolfram-ambiguity.jsonl`.

The scoped compiler hash `d6290c76...` is not a substitute for older pins.
It is exactly the committed `seed/out/omc` introduced by
`1fe5976d398646b17acabbf6d9dfae96b017e439`, with the full binary/source hashes in
the lock. Read-only `git show <revision>:seed/out/omc | sha256sum` resolved the
older `2c824805...` at82a399b9 and `49923639...` at ec07d25d as different
historical binaries. No compiler change is part of NL4. After the reseed
recorded at the top of this file, the committed compiler is `38748442...`; the
lock and receipt below keep the `d6290c76...` run unedited, and the command
below uses the current pin because the conductor refuses any other hash.

From this checkout with separately fetched sibling source repositories:

```sh
mkdir -p build/nl4-deps
git -C ../aien-train show 0e01c58a41f1a2fb976efa75c1cc554503aa2ee4:native/neuralese/hadamard.om > build/nl4-deps/h.om
git -C ../aien-protocols-mv show 6fb728a14781eb4e5262ffbdc9c2e6be64974f36:specs/latent-payload/validator.om > build/nl4-deps/v.om
sha256sum build/nl4-deps/h.om build/nl4-deps/v.om
# Refuse to continue unless both hashes equal nl4-dependencies.json.
seed/out/omc < seed/qualification/om2/gate.om > build/nl4-gate
chmod +x build/nl4-gate
build/nl4-gate seed/out/omc 3874844226c7f29a9b015d30098030ad7a9fcd769eccc2395511de57f7a37a28 neuralese/nl4-host.cases build/nl4-host
```

The conductor hashes the compiler itself and requires a previously absent scratch
directory. `codec-check.om` checks all sixteen independent encoder payloads and
refusal output preservation; the end-to-end cases use independent expected
results, explicit wrong objective/operand, quantization ambiguity and malformed/
stale/foreign/withdrawn-context refusals. Guard-removal and wrong-operation
mutants must be killed, and native pre/postcondition violations must trap.
These are deterministic correctness cases, not independent statistical trials
or the preregistered learned-arm campaign (benchmarks draft #56).

Observed at clean implementation `09b1394fe68287eaa10b0d90cf07a9dd00e6cb60`:
`OM2GATE PASS cases=63 pass=63 fail=0 invalid=0 not_run=0`
(24 pure executions,27 deliberate refusals,9 killed mutants,2 native contract
traps,1 sixteen-payload independent codec oracle).
The unchanged inherited suite separately returned54/54PASS.
The actual receipt is
[`nl4-host-results.03b624d6ad393604d958f554fc55844885712ca49f425f98459974846e9e5280.json`](nl4-host-results.03b624d6ad393604d958f554fc55844885712ca49f425f98459974846e9e5280.json).
`nl4-host-pass.log` and `nl4-inherited-pass.log` preserve every verdict;
the initial compiler/name and lexical-order failures are retained separately,
not rewritten as success. Representative actual channels are
`nl4-success.stdout` and `nl4-refusal.stderr`.

Re-run on 2026-10-30 after merging main `62d1d0d668f897386a0e311374f8dbcc5c32dde8`,
with both staged dependencies matching the lock hashes and compiler
`62775d2a391cfef7cde64e387655caa9809a267093112910ec76bfa568f3712b`: the unchanged
`nl4-host.cases` returned `OM2GATE PASS cases=63 pass=63 fail=0 invalid=0 not_run=0`
and the inherited `host.cases` returned `OM2GATE PASS cases=54 pass=54 fail=0
invalid=0 not_run=0` (host CPU, Linux AArch64).

Re-run on 2026-10-30 after the #946 reseed, with the same staged dependencies and
compiler `1578b35781f8f4eed7bc7fe27eeb333fadba08941d8d014f521684b5a5bcdbe1`:
`nl4-host.cases` returned `OM2GATE PASS cases=63 pass=63 fail=0 invalid=0 not_run=0`
and `host.cases` returned `OM2GATE PASS cases=54 pass=54 fail=0 invalid=0 not_run=0`;
the same conductor given the previous pin `62775d2a...` returned `OM2GATE INVALID
cases=54 pass=0 fail=0 invalid=0 not_run=54` (host CPU, Linux AArch64).

### Historical attribution correction (2026-10-31)

Successive reseeds changed the compiler hash in the dated #946 paragraph above;
at #948 merge `dc0d06e953e4e6729a6ea9d8a18a6fe2e18a4324` it incorrectly
named `e4d84bc3...`; #875 merge
`2367874399001efdc7f1ff345fb327469bc1a135` subsequently named `38626559...`,
and #918 merge `2d848d939b102e53049d66ead554a18f56791abb` named `38748442...`.
The paragraph is restored to the compiler recorded in
[#946 merge `d9461717721856602626c3202751d7653e1aa5f7`](https://github.com/aien-dev/omega-mv/blob/d9461717721856602626c3202751d7653e1aa5f7/neuralese/README.md).
Read-only `git show d9461717721856602626c3202751d7653e1aa5f7:seed/out/omc | sha256sum`
independently matches `1578b357...`; the compiler source at that revision hashes
to `b25079742d1157cd614fe92eeb2dd5e9a5e3b19e978007c7ecb7a6105ab6c65d`.
The active pin above now names #918's actual binary/source pair separately.

This corrects documentation provenance, not execution: no historical receipt,
log or fixture has been replaced, and no new Neuralese run is claimed. The
Oct-30 observations do not establish the same results under later compilers.
