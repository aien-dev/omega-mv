# UAP1: bounded application-profile preflight, revision 1

Issue architecture298 / Omega642; NOT A MASTER PLAN. This module is a **read-only plan validator**, not a launcher, receipt signer, installed-adapter registry or replacement for `omega_resolve` / checker2. It neither authenticates caller-supplied hashes nor proves the environment. A valid plan MUST be revalidated against actual source/artifact/host/evidence at execution. A successful preflight is not a qualified run.

## First executable path

The already-published Omega native exact-math request-stream application. Reuse public archive `eaf37ff32d17166de403372069b52aeb98b9523ce154b8f065ae9e4c161c8480` at omega-mv asset commit `fd4c12224773253c063f732c7d9e64a5395104e1`. Compiler `191b776ef98821597c3e30655da6d680a9b66edbc9ceaca3c39c61ea6df098e9`; math core `b70391a7a38464cd63ff796748565baf3dc2f73afaed90cbc2b94774dba02db6`; original stream driver `145db7e5184efbc5076ce27e93ddc7822e3f509c793f04f2413e0fd29f3343d0`. No private source is required. Source/tool/compiler pins identify inputs, not equivalence proof.

Computation: exact bounded Q[x], 128 decimal digits per magnitude including intermediates, degree<=16, explicit refusal/recovery. Interface: borrowed ordered byte streams and process status, **not FFI**. Application production computation is native Omega; Linux read/write/exit, POSIX shell dispatcher and GNU integrity/file utilities are retained. No foreign math runtime, remote service or GPU in this profile.

Target1: physical Spark GB10 CPU, Linux/AArch64. Target2: physical Raspberry Pi5 BCM2712 Cortex-A76 CPU, Linux/AArch64. These are different CPU machine profiles, **not different ISAs**. This table names candidate environments, not fresh hardware measurements. macOS, x86, RV64, native-Aienux and GPU are unsupported by this module until separately qualified.

## UAP1 wire (exactly 288 bytes, no trailing bytes)

| Offset | Meaning |
|---|---|
|0..3|ASCII `UAP1`|
|4|protocol version1|
|5|semantics1, bounded exact-math contract|
|6|component protocol1, synchronous byte-stream/process-status (not C ABI)|
|7|precision1, exact bounded rational/polynomial|
|8|requested target1 or2|
|9|declared observed target, must equal requested; execution must independently establish it|
|10|substrate1=Linux-hosted,2=native-Aienux|
|11|required foreign runtime0=none,1=Python compatibility|
|12|declared runtime availability0/1|
|13|stream rights bit0 read, bit1 write; exactly3 required; unknown bits malformed|
|14|evidence selector1=inherited published test evidence;0=missing. Not a receipt proof or authority token|
|15|requested realization1=native,2=foreign-runtime,3=emulation,4=unsupported|
|16|remote request0/1|
|17|explicit remote policy0/1; never grants implementation or network permission|
|18..19|adapter major1/minor0|
|20..23|little-endian request capacity4096|
|24..27|little-endian response capacity8192|
|28..31|reserved zeros|
|32..95|64 lowercase ASCII hex compiler SHA256|
|96..159|64 lowercase ASCII hex public source archive SHA256|
|160..223|64 lowercase ASCII hex core SHA256|
|224..287|64 lowercase ASCII hex original stream driver SHA256|

Pins are exact byte comparisons to the immutable table above. Structural preflight only checks the declaration; execution must hash actual files and verify the selected authority/evidence separately. Adapter version and any source/environment change invalidate equality with this record. No cache is implemented; repeated identical inputs produce identical plan bytes. No metadata-only claim of a new hardware run.

## Refusal precedence / statuses

First failure wins. 64 malformed length/magic/reserved/enum bits;65 protocol version;66 semantics;67 ABI;68 precision;69 required runtime unavailable;70 adapter version/source pin mismatch;71 rights;72 wrong/mismatched CPU target;75 requested remote without explicit policy;73 unavailable native-substrate/foreign-runtime/emulated/remote implementation;74 missing or unknown evidence selector;76 resource capacities;77 transport I/O. All nonzero outcomes write no stdout and produce a named stderr diagnostic. No output, network, file/installation mutation or device launch before successful validation. Read/write of the plan command's own standard streams is its only I/O.

A valid plan has `execution_authorized:false`, `qualification:"inherited-tests-only"`, `fresh_execution:"NOT_RUN"`, explicit runtime/OS dependencies, selected target and exact pins. Unknown/runtime/emulation paths do not silently become native. Runtime-required plans are not implemented by pretending the native math profile supports Python objects.

## Verification and boundaries

Direct Omega tests call `upcheck(n)` against the full input buffer and require byte-for-byte unchanged input on all paths. CLI tests use actual files/pipes, short/truncated/extra-byte/malformed records, wrong target and all pin bytes. Compiled behavior mutants must compile to a real nonempty AArch64 executable before classification. A fresh source export must reproduce the tested binary. Independent final source review, normal local judge and application execution evidence remain separate gates.

This first slice does not satisfy the whole execution-plan issue: installed adapter lookup/authentication, actual launch revalidation, lifecycle cache invalidation, runtime compatibility and final command integration remain required. It may not be presented as a trust boundary for hostile input or as a complete universal adapter.
