# x86-64 Linux backend (FR-9)

Status: **DRAFT; native x86 qualification NOT_RUN**, 2026-10-28.
The canonical integration base is `aien-dev/omega-mv` commit
`d94a503f7aae57d09697046405da2e9a5895be6d`, with compiler batch 3 integrated
at `7bce2a4fddc43268ac2af09cddc4a5a67d78312d`; the historical backend is
`7eca2ac3fbd1f35a7d65471065408ce4bea07fd4`. Current implementation lives in
`seed/omc/omc.om`. Source anchors describe implementation, not by themselves
execution evidence. The dated baseline below is immutable history; resumed
qualification records supersede it only for their exact source/tool pins.

## Selection and implementation boundary

The required opt-in is the exact first line `target x86linux`. Inputs without
that line retain the AArch64 default. Target selection belongs in `xlead`,
before `prescan` and normal parsing; it must preserve diagnostic line numbers.
Whitespace variants, truncated/extended target names, a later target line, and
combining the target with another output mode must not accidentally select a
backend. The historical `xlead` token comparison is not itself sufficient proof
of the exact-line contract: the tokenizer packs only the initial name bytes.

The backend is handwritten Omega in `seed/omc/omc.om`, not a C, Python or
assembly implementation behind an Omega facade. Shared semantic operations
select x86 emission before the AArch64 `emit` path. `emit` refuses with error 25
when a nondefault target reaches it. This is a refusal boundary, not a substitute
for implementing a reachable language operation. The historical commit also
contained `target x86efi` and a firmware boundary; those are removed from this
Linux-only integration. FR-9 does not qualify PE32+, OVMF or physical firmware.

## Instruction selection

The backend lowers language operations directly; it does not decode AArch64
words. `xb`, `xword`, `ximm`, `xrr`, and `xmem` emit bytes, little-endian
immediates, REX prefixes, ModRM and (for a stack base) SIB. Hardware register
numbers are private emitter operands, distinct from the language value IDs.
The instruction references are the Intel SDM volume 2 entries cited beside the
backend in the source.

| Operation | Source anchors and selection |
|---|---|
| Integer constants and moves | `matconst`, `force`, `ximm`, `xload`, `xstore`: materialize a full-width immediate or copy a frame value through a hardware register |
| Add/subtract/multiply and bitwise operations | `bo_add`, `bo_mul`, `bo_log`, `xarith`: ADD, SUB, two-operand IMUL, AND, OR, XOR; signed checks branch before later operations overwrite flags |
| Division/remainder | `bo_div`, `xdiv`: test zero first, DIV for unsigned, CQO/IDIV for signed; handle the signed divisor-minus-one case explicitly so remainder does not raise a hardware divide exception |
| Shifts | `xshift`: check count against the language width before CL-based SHL/SHR/SAR; signed left shifts must retain the overflow check |
| Unary/conversion | `xunary`, `xconv`, `xcanrax`, `xext`: NEG/NOT, shift-based width canonicalization and checked narrowing; bool conversions retain language restrictions |
| Comparison | `xcmp`, `xcc`, `cset`: CMP and SETcc followed by zero extension; signed/unsigned predicates must not be interchanged |
| Branch/call/return | `xbranch`, `xpatch`, `ca_bl`, `resolve`, `epi`: rel32 Jcc, JMP, CALL, then LEAVE/RET; patch byte displacements, not AArch64 word displacements |
| Arrays and cells | `xelem`, `xmemval`, `st_ld`, `st_put`: bounds check, scaled index, width-specific MOV/MOVZX/MOVSX/MOVSXD; internal addresses are not source scalar pointers |

`xbranch` reserves six bytes for every branch site. An unconditional JMP uses
its relative displacement and a padding NOP; conditional branches use the long
Jcc form. `xpatch` distinguishes their displacement positions. Calls have a
separate rel32 fixup in `resolve`.

The x86 byte-output pool is 16 MiB; default AArch64 emission retains its existing
8 MiB ceiling. This target-specific capacity accommodates the larger variable-
length instruction stream without changing the default target's refusal gate.
Dead then-arms do not emit an unreachable skip branch past the final bare-unit
instruction. Numeric and value `for` loops both check invariant obligations at
the loop head, including final false heads and `continue` backedges.

The historical backend refused floating-point paths. The current draft adds
baseline SSE2 scalar lowering through `xfmov`, `xfsse`, `xfucom`, `xfbin`,
`xfminmax`, `xfcmp`, `xfint`, `xftoint` and `xfconv`, routed from `fp_*`.
Float bits retain the same frame slots; f32 extraction zero-extends its bits.
Comparisons normalize unordered/parity handling to the existing boolean flag
contract rather than adding a public condition code. The source implements
signaling-NaN priority and quieting, numeric selection for quiet NaNs in min/max,
signed-zero min/max, and default positive NaN for invalid arithmetic.
Float-to-integer conversion truncates and saturates to the destination bounds,
with NaN producing zero, rather than raising the checked integer-conversion
trap. Unsigned 64-bit conversion has dedicated high-half handling. These are
IMPLEMENTED source paths, not exercised IEEE parity claims; full fixtures must
still cover arithmetic, comparisons, conversions, arrays, pools and structs.

## Sixteen-value register model and frame map

The parser's allocation model is retained; frame storage is not general spilling
and does not remove error 12. `poolc` allocates these sixteen operand IDs:

| Pool positions (source `poolc`) | Stored operand IDs |
|---|---|
| Before the first branch in `poolc` | `0, 1, 2, 3, 4, 5, 6` |
| Before the second branch in `poolc` | `8, 9, 10, 11, 12, 13, 14, 15` |
| Final return in `poolc` | `17` |

The historical operand-ID holes `7` (runtime) and `16` (scratch) are retained.
For **each** operand ID `s`, `xload` and `xstore` use exactly the source expression
`RBP - ((s + 1) * 8)` for its slot. This is an operand-ID-indexed frame, not a
new dense renumbering of the sixteen values. `xpro` reserves 256 bytes below RBP;
`xsave` and `xrestore` copy storage indices `0..17`, including the holes. RBP
addresses values; RSP moves for local arrays and call reservations. R15 retains
the owned-stack floor. RAX, RCX and RDX are commonly temporary integer operands;
R11 temporarily retains the caller's frame pointer during prologue entry.

The source constants for Linux startup are stack floor `268435456` and initial
RSP `276824064`. `xcheck` compares prospective RSP against R15. `xsave` checks
space for a callee return address, saved frame pointer and frame before CALL;
`xsp` checks before stack subtraction. The owned stack is not Linux's original
argument/environment stack.

## Internal ABI versus Linux SysV

This is a private Omega calling convention, **not** the SysV C function ABI.
`xsave` snapshots caller storage into a 256-byte reservation. `xarg` installs
arguments in caller frame slots, and `xpro` copies parameters from the old RBP
into the new frame. `epi` loads result slot zero into RAX and emits LEAVE/RET.
`xrestore` preserves that result while restoring other saved storage and RSP.
The existing six-parameter/slot accounting is a front-end contract; OM-2 unsized
arrays additionally require their pointer/count slot accounting (`pwid`,
`pslot`), not a scalar-only parameter loop.

Bare-function output is now implemented by `xunit` using this same private
frame ABI. Each exported entry occupies a six-byte JMP/NOP slot in source
order; without exports, the unit enters its last function. `writeout 512`
removes the image reservation, so the result is raw code, not ELF or an
independently executable process. The caller must establish the private frame
and owned-stack convention. Existing `fnstore` restrictions on globals/text,
call-cycle checks and refusal boundaries remain in force.

For bare units, physical R12 holds the wrapper's recovery-stack anchor, R14
points to an immutable table of 32 runtime entry pointers, and R15 holds the
owned-stack floor. These physical registers are distinct from language operand
IDs. The native host-extension helpers overwrite R12/R14 in program mode and
remain forbidden in bare units. Legacy byte `read`/`write` use the separate
`xio` syscall path, preserve these registers and remain allowed in bare units.
Before CALL, the wrapper supplies a separate 256-byte old-RBP slot area,
including pointer/count argument slots, and at least 272 writable bytes below
pre-CALL RSP for the return address, saved RBP and callee frame. The first
prologue does not check this caller-owned initial allowance.

A bare trap places its code in ESI and jumps through runtime table entry 2
(`JMP [R14 + 16]`). This is a private nonlocal landing pad, not a C function call:
there is no new return address and the current frame may be incomplete. The
landing pad restores RSP from R12 and the wrapper's saved SysV callee-preserved
registers and RBP, then returns the status without storing a result. It must
validate trap codes and restore SysV alignment before any foreign CALL. Normal
unit returns still produce their value in RAX through LEAVE/RET. Trap recovery
does not roll back earlier writes or I/O.

The raw compiler output itself has no SysV C-callable export, shared-object
relocation contract, PLT/GOT, dynamic linker, libc startup or general foreign-call
boundary. The separate omega-bridge x86 wrapper supplies the C boundary; its
qualification is separate from the private compiler ABI. Linux syscall register
conventions alone do not establish FR-7.

## Linux syscalls and cross-architecture records

The imported Linux implementation covers `read` and `write` through `xio`, and
process termination through `xleave`. `xio` supplies fd in RDI, the checked byte
slice address in RSI, count in RDX and the syscall number in RAX; SYSCALL returns
in RAX. Its source selects `read`/`write` numbers with `c - 53`. `xleave` moves
the status to RDI and uses syscall number 60. Caller frame storage must survive
the architectural RCX/R11 clobbers. Raw kernel error returns must retain the
existing host-word result convention, rather than silently becoming success.

**Historical host gap:** imported `hostval` refused every nondefault target with
error 25. The draft now contains an `xhost` dispatcher and native host helpers,
including socket, polling and device/mapping routes; wiring and coverage are
still being integrated. Their presence alone does not establish support for
every existing host fixture. Reusing AArch64 emitters is not an implementation.

`execprog` (word 124) in `xhpack` calls `close_range(3, ~0U, CLOSE_RANGE_UNSHARE)` (syscall 436)
before `execve`, and a nonzero result skips the exec and returns that kernel error, matching the
AArch64 descriptor boundary. `execpure` (word 178) has no x86 strict-profile lowering (Landlock,
seccomp and limits are AArch64-only); `xhost` refuses it with error 25.

A complete host lowering needs a per-word x86 Linux syscall-number and
argument-order mapping, checked against the x86 UAPI rather than
`asm-generic/unistd.h`. Record layouts need a separate audit: `stat` fields and
offsets, `timespec`, polling records, socket addresses/options, ancillary data,
`iovec`/`msghdr`, wait results and architecture-dependent flags cannot be assumed
portable by changing a syscall number. Private kernel records must be translated
to the existing language-visible layout where those layouts differ. No kernel
pointer may escape into a source scalar; slice bounds and refusal allowlists
must remain enforced. Startup argument/environment support must preserve the
initial Linux stack before replacing RSP. This paragraph is a design obligation,
not a claim that these translations are present.

## Static ELF64 and address fixups

`xelf` reuses the static `elfhdr` layout, replacing the executable/machine word
for x86-64 and the entry address. The two PT_LOAD mappings remain code at
`0x400000` (read/execute) and owned pools/stack at `0x10000000` (read/write).
The output is ELF64 little-endian ET_EXEC, without a PT_INTERP or dynamic
linker. `xeof` emits startup after the functions, installs the owned stack,
calls parameterless `main`, then exits. The Linux code reservation starts at
byte 512 in the historical path.

`mataddr`, `xaddr`, and `xfixaddr` retain address origin separately from integer
constants and patch RIP-relative LEA displacements after layout. A numeric
constant that happens to resemble a pool address must not relocate. Calls and
branches are resolved against code byte offsets. Static fixed-address segments
are not a claim of PIE or a shared-object ABI.

## Traps and refusal paths

`xtrap` records a branch site and language trap code; `xfinish` resolves each
site to process termination with status `100 + code` in program mode, or the
runtime-table landing pad described above in bare mode. Trap numbers and
source-level boundaries match AArch64, including signed overflow (1), division
by zero (2), bounds (3), loop bound (4), conversion (5), stack (6) and shift count
(7). Explicit and contract-related traps retain their existing definitions.
Linux termination is not a host signal oracle.

Compiler refusal is distinct from runtime trapping: error 25 identifies an
unsupported target/emission path, while existing parse, type, capacity and
bounds refusals remain compiler errors. The historical program-only restriction
has been replaced by the private bare-unit output described above. No unsupported
construct may be represented by empty emission or a fallback AArch64 instruction.

## Integration gaps and acceptance evidence

The historical import lacked host words beyond byte I/O/exit, floating-point
lowering, OM-2 unsized-array checks, pointer/count parameter accounting and newer
semantic operations. The current draft adds SSE2 lowering, an `xhost` dispatcher,
`nm_lr` checks in `xelem`, parameter-slot integration, and core/layout/rotate
lowering (`xrot` among the native helpers). These are source integration changes,
not execution evidence. The guard against AArch64 emission remains required.

The historical target-name prefix comparison is now length checked in `xlead`;
the exact-first-line contract still needs its complete negative controls.
Bare-function and multi-entry output boundaries, all host records and every
current language path must be accounted for by the final differential. No gap
listed here is an approved reduction of FR-9 scope. Final source pins and
exercised coverage must supersede draft observations before FR-9 is complete.

Acceptance requires the **full existing fixture differential**, not a selected
integer subset: default AArch64 bytes/refusals/diagnostics unchanged against the
independently pinned baseline; x86 results, stdout/stderr, side effects and trap
statuses compared to the corresponding AArch64 executions; documented treatment
of architecture-specific fixtures without silently omitting them. An independent
`objdump` instruction oracle must inspect emitted instructions and fixups, and
wrong-encoding mutants must demonstrably fail that oracle or the behavioral
checks. Record exact commands, source/compiler pins, baseline hashes and both
expected and observed results. An x86 QEMU run is **EMULATED**; it is not a
physical x86 execution receipt. Full acceptance remains pending for this draft.

## Seed impact

The compiler source and its generated seed compiler are separate artifacts.
Adding x86 output does not replace the AArch64 seed ladder or authorize editing
handwritten bootstrap rungs. The source remains within om4's unchanged
512-function table: two one-use ELF/relocation helpers were inlined after a
514-function candidate correctly failed bootstrap error 14. No bootstrap gate,
table capacity, seed0, hex1, asm2 or om4 was weakened or replaced.

### Historical baseline at `becf5c817bcb7e0846d12ebfc2be91042d8ad89a`

- `sh seed/host/check.sh`: PASS, including seed ladder/fixpoint, IR C-oracle
  comparisons, gates 7 through 18, 44 contract cases, enforcement/prover
  negative controls, native host checks, 53 negative controls and 62 unchanged
  historical fixture outputs.
- `sh seed/omc/gate5.sh`: GATE5 PASS, including 153 corpus inputs, 4,860
  cut/changed inputs and the existing wrong-compiler mutants.
- Baseline source: 15,525 lines, 512 functions, SHA256
  `3a40637bfc11eb4b0c0af80a73bd228a3975c25ef805859f06cb1f6d7fc089f4`.
- AArch64 compiler: 473,348 bytes, SHA256
  `5f0faab7cd4fc12158cf8e099418098d278df644efc10e4e7e2e5ca6141fca86`.
- x86 compiler: 3,652,870 bytes, SHA256
  `a95de640177954118482f4fab45945e6cdefc8935afe3eb4e0ac0ec314dc78fc`.
  Under QEMU it compiled the complete compiler source for both targets to
  bytes identical to the AArch64-hosted compiler, including its own x86 image.
  This is **EMULATED**, not a native x86 seed chain.
- Independent GNU x86-64 objdump 2.42 decoded all 517,049 instructions of that
  x86 compiler image without invalid opcodes or undecoded bytes.
- Exit-42 ELF behavior matched native AArch64 and QEMU x86. All four actual
  CUDA++ units (`bw_enc_sm121`, `cubin_elf`, `ampere_enc_sm86`, `amp_runtime`)
  compiled for both targets; their bare ABI behavior is not established by
  compilation.

The full cross-target runner completed with verdict **FAIL**: 7,537 PASS,
152 FAIL, 399 NOT_RUN, five MISSING_IMPLEMENTATION, 570 EXCLUDED and
257 MISSING_INPUT records. Both encoding mutants were caught. Exact pins,
command, counts and unresolved cases are recorded in
`seed/qualification/x86-linux/20261028.json`; the unchanged complete JSONL
is preserved at the path and SHA256 recorded there. This is not stage-C PASS.

Failures include contract `forfinal`/`forcontinue` refusal, x86 output capacity
on C2 case 05712, four bare-unit direct-branch boundary failures, structured
diagnostic positions, explicit-refusal oracle mismatches and device fixtures.
Uncomposed bare callers, unreviewed host drivers and missing SHA fixture inputs
also remain. These are not silently excluded from the overall failing verdict.

For the invalid-fd `devcall` discrepancy, a minimal trace returned EBADF (9)
on AArch64, while QEMU translated the same x86
`ioctl(-1,0xc020462b,...)` to ENOTTY (25). No backend errno shim or weakened
gate was introduced. This dated baseline did not satisfy full stage-C
acceptance; the resumed run must retain the original cases and explain each
remaining deviation.
Native x86 execution and a native x86 seed0 chain are **NOT_RUN**.

Word-count bookkeeping was checked through WolframLanguageEvaluator:
`(473348-176)/4 -> 118293`. QEMU version was 8.2.2
(`1:8.2.2+ds-0ubuntu1.18`); Python was 3.12.3. These are verification
oracles only, not production dependencies of the emitted static programs.

## Resumed differential and operator wrap-up

The complete resumed run at `66807b62914461b88df5ac73b827a8c809a854b4`
used QEMU 10.0.14 and the original/private/bridge callers. It remained **FAIL**:
8,243 PASS, 476 FAIL, 211 EXCLUDED, six NOT_RUN and one
MISSING_IMPLEMENTATION. These counts are not directly comparable as a fixed
denominator: previously excluded metadata/conductor arms and missing inputs
were expanded into exercised cases. All 570 original exclusions and all
remaining exclusions are individually recorded in
`seed/qualification/x86-linux/20261028-resumed.json`, alongside unresolved
records, hashes, commands and the exact counting rule. The historical baseline
has 152 top-level FAIL records; its summary is not another failing case.

At `4f14b2d8f1eeb470836fcd2adf1e6ad5b7608c16`, the source additionally repaired
OM-2 constant-index temporary allocation and missing x86 trap-site diagnostics.
The SHA unit compiled for both targets
and the contract diagnostic fixture emits matching source-site records after
target-prefix line translation. The seed ladder rebuilds to compiler SHA256
`7d1739cc46c04bb8b585c5a255690ae58e30e9ffaa329defa19da3375c5fa316`.
Additional entryless callers and trap authorities are implemented in the oracle.
The full differential after those changes was **NOT_RUN** at that operator
wrap-up. Do not attribute the earlier counts or host gates to that unrerun patch.

The recovered source `efd15c234afc584e1e67492e736cbb42806c2a7d` (source
SHA256 `e9d70e7cbe1b7026b2fcac5bbf61a64bde931b50a632c954355ba5299954a61c`,
AArch64 compiler `f7752bc5c800e85c2393a08153786d0a6bc4516c9cea955fe27624228d6d1409`)
passed `sh seed/host/check.sh` and `sh seed/omc/gate5.sh` on the AArch64 host.
Its complete differential exited 1: **14,930 PASS, 60 FAIL, 10 NOT_RUN,
216 EXCLUDED**. Verdict **FAIL**. The remaining failures are structured
diagnostic mismatches, unclean expansion-growth refusals, OM-2 fold/unroll
mutant oracle mismatches, A5 bare-unit fixture exits, and frozen C2 mismatches
`c2-00015`, `c2-00033`, `c2-00304`, `c2-00306`, `c2-00675`, `c2-00728` and
`c2-05713` through `c2-05719` except `c2-05718`. Two QEMU ioctl
EMULATOR_ARTIFACT cases and eight entryless callers remain NOT_RUN. Exact case
names and receipts are in `seed/qualification/x86-linux/20261028-recovered-corpus.json`
and `20261028-recovered-host.json`.

Two invalid-fd ioctl cases in the resumed run have concrete emulator evidence:
QEMU's own trace records request `0xc020462b`, fd `-1`, errno ENOTTY, while the
native fixture requires EBADF. QEMU 10.0.14
[`linux-user/syscall.c:5713–5717`](https://github.com/qemu/qemu/blob/v10.0.14/linux-user/syscall.c#L5713-L5717)
returns `-TARGET_ENOTTY` for an unknown request before checking the descriptor.
The receipt pins the source, source archive, emulator binary and trace. These
are NOT_RUN/EMULATOR_ARTIFACT records, not backend PASS or native-x86 evidence.

## Corpus authority and compatibility

The runner distinguishes current-candidate checks from historical authorities:

- Frozen `sec/*/pinned.cases` assertions execute through the original Omega
  conductor with the independently checked compiler SHA256
  `734f0a39319fbfa930bcc62cb30dbf06d47d78e126372b74f5baf478e6126b61`.
  Separate current-candidate differential records do not replace those assertions.
- C2 still requires the current candidate to reproduce its frozen compile bytes
  and diagnostics. A separate `:frozen-provenance` replay checks the historical
  authority; its PASS cannot satisfy a failing current-candidate C2 row.
- Historical candidate manifests receive the compiler source at their recorded
  stage revision. Compiler substitutions, builds, controls and mutant executions
  run through their original conductor, rather than testing a healthy compiler
  against a mutant-only expected output.
- Standalone mutant specifications must map to executable mutation blocks.
  Actual build/control/kill transcripts are filed once in the JSONL as
  `original-conductor-evidence`; per-row records reference their hashes.
  Adequacy controls must fail specifically because the mutant survived, not
  because its compiler failed to build.

The original C2 compatibility contract conflicts with intentional later native
changes, including corrected `getppid`, the socket-option safety allowlist and
expanded name/function capacities. Those failures remain visible. Revising the
contract requires an explicit decision; neither reverting safety fixes nor
silently replacing current-candidate checks with historical replay is allowed.

### Recovered OM-2 growth bound

The recovered x86 byte emitter now enforces the same per-function emitted-storage
budget as the AArch64 word emitter: 262144 bytes in an OM-2 function, including
unrolled and inlined output. This is a storage bound, not an x86 instruction
count. The total output-pool ceiling and expansion-fuel bounds remain unchanged.
The existing growth fixtures must refuse with error 14 and empty stdout;
their diagnostic twins must retain `obligation=growth`. Rolled and OM-1 controls
remain accepted. Removing only the new guard is the negative control.

Different instruction sizes reach that storage ceiling at different source
locations. The corpus deliberately retains its literal cross-target refusal
and diagnostic equality assertions: a clean refusal does not turn a different
source location into PASS. Likewise, x86 bare-unit stack checks retain every
`omc-site` record required by the recovery ABI; they are not suppressed to
imitate AArch64's program-mode-only stack checks.

The interrupted current-compiler mutant repairs re-anchor the S2 cycle and S3
error-16 substitutions to their intended guards and distinguish the x86
constant-index guard from the unchanged AArch64 occurrence. Historical mutant
sources and frozen receipts remain unchanged. A5 kept-constructor and rotate
bare-unit callers use the independently recorded exits in
`candidates/s4/export/sec5.cases`, rather than incorrectly requiring exit zero.

Source `3e66b6744590cc72c623d8e09413b9072bcab36e` (compiler source SHA256
`4b5392fffd25a3a37844ad8bb5138fbaada690be7dae95132614524bdb78a1e0`)
rebuilds to seeded compiler
`badedbb0c6287887641fcd7e97301b43b5f82f2d7a2838bbe0d17547873108be`.
The seed ladder, `sh seed/host/check.sh` and `sh seed/omc/gate5.sh` passed.
The complete corpus exited 1: **14,936 PASS, 54 FAIL, 10 NOT_RUN,
216 EXCLUDED**, verdict **FAIL**. All 15,216 outcome-row case names and their
multiplicities match the preceding complete corpus; no failing case was removed.
Both independent encoding mutants were caught.

The 54 remaining FAIL records are 17 literal successful-diagnostic mismatches,
8 growth-refusal coordinate mismatches, 16 historical S2 mutant obligations,
12 frozen current-C2 obligations, and one pre-OM-2 refusal expectation in
`leak.cases`. Eight formerly failing mutation-anchor/A5 caller records now pass.
Two formerly passing S2 growth records now fail literal coordinate parity:
both targets cleanly refuse error 14, but their emitted-storage ceilings cross
at different source tokens. These are not relabelled PASS.

The ten NOT_RUN records remain two concrete QEMU ioctl artifacts and eight
entryless diagnostic-site callers. Native x86 execution remains NOT_RUN.
The source-bound guide examples ran on both targets: FNV input `abc` emitted
`0be9471a` with exit 0, narrowing trapped with exit 105, and removing only the
unmasked store changed that exit to 44. x86 execution is EMULATED; AArch64
execution is HOST_TESTED.

Additive receipts are `seed/qualification/x86-linux/20261028-orphan-host.json`
and `20261028-orphan-corpus.json`. They record exact commands, binary/source
digests, the guard-removal negative control, every remaining obligation, and the
complete JSONL SHA256. Earlier failed and interrupted receipts are preserved.
Literal parity, current-C2 compatibility and historical kill assertions remain
unchanged; an explicit contract decision is required before any revised gate
scope. This repair is IMPLEMENTED and EXERCISED, not QUALIFIED, MERGED or DEPLOYED.

Disassembly uses bounded chunks starting at decoded instruction boundaries,
with lookahead for crossing instructions. Every code byte and direct-branch
target is still checked, including large generated images; the child output
file limit is not treated as malformed compiler output or waived.

## Standalone qualification oracle

### Current acceptance contracts (2026-10-28 integration)

At source `7ab5fdd067884fd3044155084b39163da1be5c8a`, the complete runner
reported 15179 PASS, 4 NOT_RUN and 222 EXCLUDED, with no FAIL cases. Its aggregate
verdict remains FAIL because NOT_RUN is not PASS. All three original large-input
SHA cases passed. Source `00ebdee8461467f26e7ec7b56289ab6549f2c0a2` then added
only the reviewed text-write driver: both previously unrun text-write cases and
the exit/encoding controls passed (5 PASS). The remaining unrun cases are the
two invalid-fd ioctl fixtures affected by QEMU's ENOTTY versus native EBADF.
The compiler binary is identical for both runs. Native x86 qualification and
independent review remain outstanding. The additive acceptance receipt records
both revisions and the original full-run verdict.

The current integration preserves every frozen manifest and historical receipt.
Current acceptance applies the already declared `manifest-c2-capacity.txt` and
`manifest-c2-batch2.txt` deltas to their named rows, while separately replaying
the original rows against their pinned historical compiler. The old OM-1
`s1-rot-refused` assertion likewise remains a historical replay; the current
assertion comes from `leak/candidate.cases` and requires OM-2 acceptance.

`seed/omc/x86-diagnostic-contracts.json` declares exact diagnostics for specific,
source-hash-pinned inputs on each target. It retains all stack checks, source
coordinates and unrolled iteration records. Changed, missing or extra records
fail. Growth refusals require the declared error and empty output. For the
frozen branch-range cases that x86 accepts, the checker additionally decodes the
artifact and requires an explicit execution result; a default-target refusal
alone cannot qualify that target artifact. Inputs outside this table retain
the exact translated diagnostic comparison.

The historical S2 mutant experiments include failed kills. Their original
expected PASS and observed FAIL remain in the replay record, with the exact
failure line. A successful reproduction means only that the old failed
experiment was reproduced; it is never evidence of a successful mutant kill or
current compiler acceptance. The current S1-S4 mutant suites remain separate.

Repeated conductor decoding may reuse a result only for identical artifact
and source hashes. It does not cache case verdicts or skip conductor execution.
Empty text emits no literal bytes, so the decoder does not carve out an empty
data region. Native x86 execution remains a separate qualification requirement.

`seed/omc/qualify-x86.py` is a read-only host verification oracle, not backend
implementation or a build/runtime dependency. The coordinator command
on AArch64 Linux is:

```sh
python3 seed/omc/qualify-x86.py --compiler /absolute/path/to/candidate/omc --objdump /usr/bin/x86_64-linux-gnu-objdump
```

It requires Python 3, `qemu-x86_64` and an x86-capable objdump. `--inventory`
categorizes source files and `.cases` rows without invoking the compiler.
The inventory also includes the frozen `seed/qualification/om2/manifest-c2*.txt`
corpus alongside live fixture roots and composed `.cases` inputs. A frozen
manifest is not a substitute for accounting for new fixture files.
Results go to JSONL stdout and isolated temporary storage, not historical
receipts. Mode snippets, conductor self-tests and alternate-compiler arms are
accounted for separately. Reviewed, SHA-pinned host fixtures have dedicated
drivers for temporary files, normalized clock/random results, invalid-fd device
calls, private Unix sockets and ephemeral loopback sockets. The two executions
use separate architecture-local directories with identical argv0/environment.
Unreviewed host effects remain NOT_RUN, not automatic PASS; unresolved
implementation/input/driver gaps must fail the overall verdict. Resource limits
and a temporary working directory are not a security sandbox, so only trusted
fixture sources belong in the runner.

Coverage distinctions must accompany any result:

- `directories` and `fchdir` adapt their pinned `O_DIRECTORY` literal for the
  target, with separate hashes; this is not identical-source parity.
- Entryless units are compiled/refused and fully decoded, but runtime is
  NOT_RUN without an explicit composed caller. Existing wasm `cases.tsv` and
  SPIR-V demo/regression tables supply full-width native wrapper callers.
- Embedded memory-check refusals and pread/pwrite view controls are included.
- The process fixture executes a native AArch64 child helper in both runs;
  it does not establish exec of an x86 child. `genlimit` uses an explicitly
  recorded fixture-only compiler variant with the fixture-required generation
  ceiling 3 on both targets; the production compiler's ceiling is unchanged.
- `psync` invokes syncfs on the scratch descriptor and therefore flushes its
  filesystem; a temporary directory does not isolate that kernel effect.

The disassembly check linearly decodes code bytes, including unreachable code,
while carving source-matched jump-protected literal spans. A mnemonic inventory
is not exhaustive instruction-template proof. The oracle includes an invalid
opcode control and a valid-but-wrong-immediate control; they must be rejected,
not merely disassembled. Both controls were exercised and caught. The complete
differential verdict was FAIL, not a qualification claim.
