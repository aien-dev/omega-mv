# OMEGA (.om or .omg)

**Omega is a systems language and compute platform being implemented in handwritten, seeded `.om`.** Its goal is one inspectable path from a small seed through compiler, runtime and applications to machine execution. Pure computation and authorized effects have distinct contracts; semantic identities identify declared computations, while validation and receipts record what was actually checked.

**Migration is not finished.** This repository's default branch still contains substantial C implementation and build/test tooling. The seeded `.om` compiler, its native host and device/mapping words, the OM-2 profile and the WebAssembly back end are on `main`; an existing C implementation is an outstanding replacement task, not the final language of Omega.

- [The Omega programme: seed to model](https://github.com/aien-dev/omega-mv/issues/599)
- [Omega 1.0 roadmap: readiness audit, milestones and acceptance gates](docs/ROADMAP-1.0.md)
- [Remaining C source/header inventory](docs/migration/C-TO-OM-INVENTORY.md)
- [Experimental `.om` deterministic-stream successor](seed/physics0/README.md): source/host checks passed; native compilation and execution NOT_RUN.
- [Alpha model/platform qualification](https://github.com/aien-dev/aien-architecture-mv/issues/305)

### Why GitHub still shows C

GitHub's language bar measures recognized source bytes on the default branch; it is not the project's language specification or a migration-completion verdict. GitHub Linguist does not currently recognize Omega/`.om`, and the active C files here have not yet been replaced and removed. We do not label handwritten C as generated/vendor code, rename it to `.om`, or assign an unrelated language to change that statistic. See the inventory above for the actual remaining work. Native GitHub recognition requires support in [Linguist](https://github.com/github-linguist/linguist/blob/main/CONTRIBUTING.md#adding-a-language).

```text
SILICON -> ATLAS -> PHYSICS/FORGE -> OMEGA -> AIEN        (full picture: aien-architecture)
```

Omega was decided by Drake on 2026-09-29 to be both the reaction runtime and the compiler for Omega Systems Core ([decision record](docs/adr/OMEGA-SYSTEMS-CORE-0000.md), frozen). This retires the earlier "not a compiler" wording.

## Current state

Research-grade, with a staged Alpha qualification programme in progress. An Alpha plan or an open PR is not a qualified release. Plainly:

- **Seeded `.om` compiler (on `main` from PR #696):** one compiler, `seed/out/omc`, written in `.om` and rebuilt from the 408-byte loader by `sh seed/omega-from-seed.sh` (`seed0 -> hex1 -> asm2 -> om4 -> omc`). Its one supported profile is AArch64 Linux, static ELF output, with the whole language in `docs/one_layer/OMEGESE.md`: native host words (ids 100..165) and the checked device/mapping words (ids 166..177). Host qualification is `tests/gates/run.sh seed` (the `OMEGA-SEED` gate: stage fixpoint, `seed/host/check.sh` and the IR comparison against the C oracles, no gate skipped). What it does not prove: the fixpoint is a same-lineage rebuild check, not diverse double compilation; the reference interpreter on `seed/omega-ref` predates this compiler source (it refuses a call to a function defined later, first at `fexp`, `seed/omc/omc.om` line 935). The synced interpreter on `seed/omega-ref-sync` (190130b7, not merged yet) runs `omc.om` on itself and reproduces this compiler byte for byte (`tools/omega_ref/gate_ddc.sh`: GATE_DDC PASS, omc sha256 734f0a39, both negative controls caught, author-run 2026-10-24; [omega#650](https://github.com/aien-dev/omega-mv/issues/650)). GPU launch and model execution through these device words are not qualified. The language has bounded native recursion; a restricted importer does not describe the whole language. See [conformance and evidence limits](https://github.com/aien-dev/omega-mv/issues/624).
- **Legacy OSC/C implementation on `main`:** OSC-0/1/2 and OSC-3 material describes the earlier implementation and unfinished migration, not the forward source-language path. The old Milestone 6 "self-host" result was a fixed-output self-copy check, not compilation ([historical note](docs/osc/OSC-1-SELF-HOST-STATEMENT.md)); do not confuse it with the later seeded compiler work.
- **Reaction runtime:** the R1 to R16 qualification ladder passed on builds that predate the composition modules (omega#126), a historical result. Requalification at `3108fc2` recorded R15 FAIL (14 of 16 gates: G1, G15) and R16 FAIL (`evidence/REQUAL-3108fc2/`); the R16 receipt `22d7a79a` at `850fc54` is invalid as a current claim because its script was later found defective; none of R1 to R16 is requalified on the current candidate, and the ladder rerun is in progress. The R16 loop-site inventory was reconciled on the candidate tree (502 sites, 0 unclassified, 0 stale, PR #279); that is the inventory gate only, not R16. The current living build is IMPLEMENTED / NOT QUALIFIED until re-qualified. Composition, Capability Graph, Skill Router, J-Space and Cortex run on the host; the Fabric interface is a host-only loopback with no network; none of this is on silicon as a qualified system.
- **Numerics (E1):** closed 2026-10-02 on the GB10 chip campaign (Gate 5 26/26 with 47 parity lines, reductions SUM/MAX/MIN/MEAN, nine transcendental ops over all 2^32 inputs each, 148 load/store kernels), consolidated receipt `evidence/E1-CLOSURE/e7851c69d34ac777a9436af16d0bdd5d69264c8528c62d562b600f5d89153061.json`; exclusions (natural-base EXP/LOG have no GB10 kernel) are listed in [E1_GAP_TABLE](docs/numeric/E1_GAP_TABLE.md).
- **Qwen3.8-27B kernels (GB10, 2026-10-18):** each one is checked on the chip against a double-precision reference with deliberately broken variants that must fail, and none is joined into a serving engine yet. FP8 matrix-vector and the tensor-core prefill GEMM now take k up to 32768, which covers the 27B down projection at k = 17408 (`evidence/fp8-k32768-20261018/`, omega#541). The Gated DeltaNet decode step (the recurrent layer used by 48 of the 64 layers) runs as one launch per layer and matches the reference to about 2e-7; it takes 125.7 us per layer, which moves its 6.29 MB of state at 50.1 GB/s against 273 GB/s nominal (`evidence/gdn-decode-20261018/`, omega#542). The DeltaNet prefill reads a whole prompt in three launches per layer and hands its state to the decode step, which carries on exactly; at 2048 tokens it takes 3.80 us per token per layer, and the recurrent scan in it does about 2.4 trillion operations a second, against a chip peak for that kind of math that has not been measured yet (`evidence/gdn-prefill-20261018/`, omega#543). The full-attention decode step for the other 16 layers (24 query heads over 4 key-value heads, 256 numbers per head, rotation on the first 64 only, done by reordering the numbers once at load) runs as one launch per layer, matches the reference at each tested context (1, 7, 256, 301, 1024, 4096, 8192, 32768 and 131072 tokens) with all five broken variants caught, and at 32768 tokens of context takes 2.10 ms per layer, streaming its 134 MB of cached keys and values at 63.8 GB/s against 273 GB/s nominal (`evidence/QWEN38-DATTN256-8bb4494e-20261018/`, omega#544). The prefill attention for those 16 layers reads a 2048-token chunk of a prompt in two launches per layer and leaves cache rows bit-equal to the ones the decode step would have written; it matches the reference at every tested point up to 129024 + 2048 tokens with all four broken variants caught, and a 2048-token chunk takes 10.0 ms per layer at the start of a prompt and 591 ms at the end of a 128k one, streaming cached keys and values at 230.6 GB/s against 273 GB/s nominal (`evidence/pattn256-chip-20261018T1517Z/`, omega#545). Getting it to run found a bug every kernel shared: the encoder wrote the integer add, one compare and the exit instruction with a fixed timing word, so the waits the code generator scheduled on them never reached the chip. The encoder now keeps them, `make test-bw-encode-control` checks every instruction type, and 18 of 19 kernel families pass their chip tests with the fix (the ternary family fails only its speed gates, which the old encoder fails the same way on the same load; `evidence/encoder-fix-sweep-20261018T1512Z/`).
- **Hardware:** results on the DGX Spark GB10 are tied to the exact commit in each receipt. Host and emulator passes are never reported as hardware qualification.

The live status is owned by [aien-architecture](https://github.com/aien-spark/aien-architecture): [CURRENT_EXECUTION_PLAN.md](https://github.com/aien-spark/aien-architecture/blob/main/CURRENT_EXECUTION_PLAN.md) and [doctrine/ROADMAP.md](https://github.com/aien-spark/aien-architecture/blob/main/doctrine/ROADMAP.md). This file does not restate numbers that drift. Historical milestone receipts (for example Milestone 4, `OMEGA_SEMANTICS`, 12 of 12 gates) stay in `evidence/` unchanged as history.

- **FP8 expert launch in one wave (2026-10-24, L310, measured on the real chip):** the grouped expert GEMV used 6 CTAs per group, a value tuned on 8 groups; a 9-group model (the 35B: 8 routed experts + the shared one) therefore launched 54 CTAs on 48 SMs, two waves, in each of its 80 expert launches per token. `AIEN_OMEGA_FP8_GROUPED_WAVE=1` sizes the launch to one wave (min(6, 48 / groups); 9 groups -> 5) and the 35B 1-row step measured 20.498 -> 19.347 ms, expert stream 160.5 -> 193.3 GB/s (N=3, chip alone; receipts in aien-sovereign-core `docs/speed/Q36_CEILING.md` section 11). Off by default until the merge train has judged it; `AIEN_OMEGA_FP8_GROUPED_CTAS=n` forces an exact value. `gpu_fp8_api_test --sim` checks the rule table without the chip; the server prints the value it runs with at load.

## Core ideas

- **SMART is not TRUSTED, INTELLIGENCE is not AUTHORITY.** Trust comes from verified invariants, never from capability.
- **Meaning is permanent; representation is disposable.** Equivalent programs collapse to one `SEMANTIC_ID` (SHA-256 of the canonical encoding); different programs get different ones.
- **Pure computation is separate from effects**, and effects need capability-bounded, receipt-producing transactions.

## How this fits with the other repositories

[aienux](https://github.com/aien-dev/aienux) holds Aienux, the kernel of AienOS, which Omega is meant to run on. [physics](https://github.com/aien-spark/physics) provides FORGE machine realization and the GB10 native path (pinned in `physics.lock`). [aien-architecture](https://github.com/aien-spark/aien-architecture) owns status and sequencing. ARGUS-0/1 (a defensive plane) is implemented in aienux (`native/argus`, pinned by `argus.lock`); Omega only carries the opt-in `RX_ARGUS` hooks (`src/runtime/rx_argus.c`) and receipts in `evidence/ARGUS/`. Physics Zero exists here as a host benchmark apparatus and learner (`src/physics0/`, `tests/physics0/`, `mk/physics0-learner.mk`, `docs/physics0/`, receipts in `evidence/physics0/`), with its specifications in the physics repository. DIRAC-0 is a specification in aien-architecture plus an oracle source in `research/dirac-oracle/` that has never been built or run. The Evolution Arena is a plan only.

### Canonical native GPU sources

`cudapp.lock` pins the CUDApp source consumed by this tree. Native GPU implementation, driver closure, generated native GPU units and their public headers come from that revision, not from a second Omega copy. `tools/cudapp-stage.sh` authenticates the checkout, materializes its committed tree, checks native-unit regeneration, builds the canonical archive and publishes an immutable stage under `OUT_DIR/cudapp`. Every native build authenticates that stage again, including a build without `make clean`. A missing or different pin, missing source, changed source or changed archive refuses the build. There is no lock-check bypass.

Set `CUDAPP_DIR` to a checkout whose HEAD is exactly `cudapp.lock`, `OMEGA_OMC` to the seeded compiler with SHA-256 `2c82480596bbdc50cda9626866209ef662d5834f278f839ff4ce0250c495029f` (Omega `e5b32567a7aff3ce594e3254a622a846a6cd190b`), and `OMEGA_BRIDGE` to omega-bridge `0c4cb9ff980af278890b231b994eaede4dc7621c`. The canonical library is linked after Omega's retained CPU objects. Omega's CPU SHA-256/BLAKE3 binding retains its separately recorded compiler and bridge pins in `om/units.sh` and `om/omega-bridge.lock`; those are not the native GPU binding.

`tests/cudapp_retirement_sources.json` records all 123 removed source/header paths, their exact historical and canonical object identities, and the SHA-256 of the preserved original-source archive. Omega retains its unique Blackwell qualification adapter and resident runtime adapter. Historical receipts and unmerged branches, including #618, are not rewritten or represented as newly qualified. The remaining CPU implementation overlap is explicit: Omega's CPU companion objects precede the canonical archive; native GPU implementation comes only from CUDApp.

Visor and program host callers also link the canonical archive after retained CPU
objects; they do not require retired local Blackwell object rules. Tensor mutation
and GB10-host controls authenticate/stage before invoking their standalone runner,
including parallel builds. `test-numeric-cpu`, `test-program-fp32`,
`test-program-realize`, `test-program-id`, `test-tensor`, `test-tensor-store` and
`test-tensor-mutations` exercise host behavior only.

Standalone chip collectors use the same authenticated stage or disposable canonical overlay and retain their existing device-authorization requirements. Their `canonical_source` field embeds the existing `.om` stage-verifier object unchanged; it is source provenance, not qualification authority. M17 reruns refuse an existing receipt or corpus manifest: set `M17_EVIDENCE_DIR` to a fresh evidence directory rather than replacing historical evidence. A build-only check of a chip collector is not a chip run.

This ownership cutover does not activate new native candidates. Existing artifact admission remains authoritative; new candidates have no production qualification record and remain refused. Host fixtures use explicitly synthetic handles. GPU execution, exact-artifact independent hardware qualification and full-model parity/performance are separate authorized gates, not consequences of staging, compilation or host tests.

#### Existing 35B candidate acceptance boundary

The first whole-model target is the existing Q36 35B inference path on Spark,
not a new operator, adapter or hardware backend. Before an authorized device run,
freeze one manifest containing the Core source and executable hashes, this Omega
source, `cudapp.lock`, staged source/archive hashes, all generated-unit identities,
compiler and bridge pins, model/config/tokenizer/weight/scale content identities,
exact command/environment, admission records and workload inputs. The capacity
repair CUDApp #173 and command-lifetime repair #174 are candidate prerequisites;
pending source is not merged source and historical component receipts are not
qualification of that manifest.
This isolated host-integration branch pins local, unmerged CUDApp
`685f6c38924383667469fa32d1f6495490b94bc9`: combines the prior candidate
`6ed24d1618e06b6e8f92e307f731a38c0d515a4b` with MMIO instruction repair #172
at `885e32017eaae6208719eefcfa516ceaa853a846`. The prior candidate combines
`491da3ce11c74fe66b49d679aafb0334915b7ab4` (#175 on accepted #173
`ca441c5a92f5a951a67de00aeddae0e077f10216`) and
`8cd1c912e8c7a68b7adcca75c6f71d1652401a54` (#174 evidence head;
corrected code `05be6d98211d7fdc043d173da8f661bbb51e0138`).
This is not canonical main, merged source, device qualification or production admission.
The original #924 draft pin and all earlier component receipts remain unchanged.

Build the canonical archive through `check-cudapp`, then build the existing Core
35B consumer against that exact archive without an alternate GPU implementation.
Retain the link map and verify the consumed archive and generated-unit identities.
Run host/refusal controls first, including wrong pin, changed source/archive,
shape/grouped-MoE admission, capacity exhaustion, command disposal and recovery.
Production resolver `NULL` and record count zero remain unchanged; a build-only
candidate does not acquire production admission.

Actual inference requires a current authorized GPU reservation and an isolated
endpoint, never an unapproved replacement or second large model beside the resident
service. Compare the candidate with the saved qualified incumbent using identical
model bytes, prompts, decoding settings, context and slot configuration. Record
correctness and numerical/model-quality results, complete prefill/decode timings,
peak and steady memory, repeated-run reliability, timeout/refusal and recovery
behavior, and effects on neighboring processes. Cover one-user and existing
multi-user workloads, ragged/large-M prefill and grouped experts; bind every actual
recorded code/launch occurrence before submission. A partial run, a mismatched
artifact or any unaccepted qualification refuses activation. GPU, full-model,
memory, speed, recovery and neighboring-process outcomes remain NOT_RUN until
measured against the frozen candidate; publish new receipts without changing old ones.

This path still depends on Linux and NVIDIA's kernel driver/firmware; the vendored
driver interface headers are version `580.173.02`. Record the actual running driver
and firmware versions in the candidate manifest. Removing CUDA userspace imports
does not establish an independent driver, generic CUDA compatibility or a drop-in
replacement for arbitrary applications.

## Standing rules

Language rule: `.om` is the only forward first-party implementation language. Implement and qualify each replacement, switch its actual consumers, then remove the superseded first-party source and obsolete build/runtime dependencies from the active tree. This includes compiler/runtime code, services, tools, automation and project-owned tests; C/Rust/shell remaining today are unfinished migration, not permanent exemptions. Preserve Git history and immutable evidence. External protocol formats, generated machine code, device firmware and independently sourced qualification oracles have separately declared roles; they must not conceal a foreign implementation of Omega. See the [replacement/deletion contract](https://github.com/aien-dev/aien-architecture-mv/issues/300).

Preserve the native GPU work and its explicitly recorded OS/driver dependencies; do not infer hardware or OS independence from removal of a userspace library. No systemd. Offline, in-house builds. Never overwrite failed experiments or old receipts. Verdict words: PASS, FAIL, NOT_RUN, BLOCKED_HARDWARE, BLOCKED_OPERATOR, MISSING_IMPLEMENTATION.

## Build and verify the existing default-branch implementation

These are **C/legacy compatibility checks**, not commands for the seeded `.om` compiler. The seeded compiler is rebuilt and qualified with `sh seed/omega-from-seed.sh` and `tests/gates/run.sh seed` (see `seed/README.md`); do not mix compiler binaries or relabel old receipts.

Needs gcc and make on an AArch64 Linux host: CI runs the host suites on `ubuntu-24.04-arm`, and `test-realize` compiles with `-march=armv8.6-a+dotprod+i8mm+sve`. Host suites need no GPU and no physics checkout:

```bash
make PHYSICS_DIR=/nonexistent PHYSICS_LOCK_CHECK=0 test-compiler-quick   # compiler, quick suite
make PHYSICS_DIR=/nonexistent PHYSICS_LOCK_CHECK=0 test-numeric-cpu      # E1 CPU numerics
make PHYSICS_DIR=/nonexistent PHYSICS_LOCK_CHECK=0 test-realize
```

The full omegatool build (`make`) links the `physics` repository at the commit in `physics.lock`. Some older milestone gate scripts rewrite their receipts in place, so do not run them on a clean checkout you mean to keep; prefer the `make test-*` targets above. Targets that touch the GB10 need the DGX Spark, run one at a time, and are never killed mid-run. The M19 clean-clone gate rebuilds the tree in a temporary directory and runs the milestone gates there; when that nested build or nested run fails, it now prints the tail of its own log to stderr, so a failure names its cause instead of reporting a bare FAIL. CI definitions in `.github/workflows/` list the suites that run on every push.

## Repository layout

`src/` existing core and C compiler awaiting replacement (`src/compiler/`), `src/runtime/` reaction runtime, `src/fabric/` host-only fabric interface, `src/physics0/` Physics Zero apparatus, `spec/` and `docs/` specifications, `tests/` gate suites, `evidence/` content-addressed receipts, `tools/` harnesses, `mk/` make fragments included by the `Makefile`. Also `bench/prime_race/` (Prime Drag Race implementation), `calibration/` (Turing calibration experiments, preregistration and blinding protocol), `research/` (experiments and prototypes) and `doctrine/` (the Milestone 4 doctrine).

## Contributing

Open a pull request with the exact command you ran and its output. Receipts are content-addressed, name the exact commit and refuse a dirty tree. Do not edit existing evidence in place. External review is expected before merge. See [CONTRIBUTORS.md](CONTRIBUTORS.md). Contact: aien@aienos.com.

## Crumbs

This repo uses the Crumb Protocol (RFC-0001). Each directory carries a `.crumb` file (what the directory is for, its rules, and a backfilled history). Live coordination between agents goes in `.crumb.local`, which is never committed.
Before editing a directory, read its `.crumb`. Rules and the `crumb` tool: https://github.com/aien-spark/aien-architecture/blob/main/docs/CRUMB_PROTOCOL.md
Backfilled history (marked as such, not live whispers) is in `docs/crumbs/BACKFILL.md`.
