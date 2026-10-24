# OMEGA — the .om language

**Omega is a systems language and compute platform being implemented in handwritten, seeded `.om`.** Its goal is one inspectable path from a small seed through compiler, runtime and applications to machine execution. Pure computation and authorized effects have distinct contracts; semantic identities identify declared computations, while validation and receipts record what was actually checked.

**Migration is not finished.** This repository's default branch still contains substantial C implementation and build/test tooling. The current seeded `.om` compiler and native-host/device work are in reviewed candidate branches, not all merged into `main`. An existing C implementation is an outstanding replacement task, not the final language of Omega.

- [The Omega programme: seed to model](https://github.com/aien-dev/omega-mv/issues/599)
- [Seeded compiler/native-host candidate](https://github.com/aien-dev/omega-mv/pull/678) and [checked device/mapping candidate](https://github.com/aien-dev/omega-mv/pull/676)
- [Remaining C source/header inventory](docs/migration/C-TO-OM-INVENTORY.md)
- [Alpha model/platform qualification](https://github.com/aien-dev/aien-architecture-mv/issues/305)

### Why GitHub still shows C

GitHub's language bar measures recognized source bytes on the default branch; it is not the project's language specification or a migration-completion verdict. GitHub Linguist does not currently recognize Omega/`.om`, and the active C files here have not yet been replaced and removed. We do not label handwritten C as generated/vendor code, rename it to `.om`, or assign an unrelated language to change that statistic. See the inventory above for the actual remaining work. Native GitHub recognition requires support in [Linguist](https://github.com/github-linguist/linguist/blob/main/CONTRIBUTING.md#adding-a-language).

```text
SILICON -> ATLAS -> PHYSICS/FORGE -> OMEGA -> AIEN        (full picture: aien-architecture)
```

Omega was decided by Drake on 2026-09-29 to be both the reaction runtime and the compiler for Omega Systems Core ([decision record](docs/adr/OMEGA-SYSTEMS-CORE-0000.md), frozen). This retires the earlier "not a compiler" wording.

## Current state

Research-grade, with a staged Alpha qualification programme in progress. An Alpha plan or an open PR is not a qualified release. Plainly:

- **Seeded `.om` compiler:** native compiler reconstruction, host operations and checked device/mapping work have source-bound evidence on the candidates linked above. Their supported profiles differ; full integration, consumer qualification and general translation correctness remain separate gates. The language already has bounded native recursion; a restricted importer does not describe the whole language. See [conformance and evidence limits](https://github.com/aien-dev/omega-mv/issues/624).
- **Legacy OSC/C implementation on `main`:** OSC-0/1/2 and OSC-3 material describes the earlier implementation and unfinished migration, not the forward source-language path. The old Milestone 6 "self-host" result was a fixed-output self-copy check, not compilation ([historical note](docs/osc/OSC-1-SELF-HOST-STATEMENT.md)); do not confuse it with the later seeded compiler work.
- **Reaction runtime:** the R1 to R16 qualification ladder passed on builds that predate the composition modules (omega#126), a historical result. Requalification at `3108fc2` recorded R15 FAIL (14 of 16 gates: G1, G15) and R16 FAIL (`evidence/REQUAL-3108fc2/`); the R16 receipt `22d7a79a` at `850fc54` is invalid as a current claim because its script was later found defective; none of R1 to R16 is requalified on the current candidate, and the ladder rerun is in progress. The R16 loop-site inventory was reconciled on the candidate tree (502 sites, 0 unclassified, 0 stale, PR #279); that is the inventory gate only, not R16. The current living build is IMPLEMENTED / NOT QUALIFIED until re-qualified. Composition, Capability Graph, Skill Router, J-Space and Cortex run on the host; the Fabric interface is a host-only loopback with no network; none of this is on silicon as a qualified system.
- **Numerics (E1):** closed 2026-10-02 on the GB10 chip campaign (Gate 5 26/26 with 47 parity lines, reductions SUM/MAX/MIN/MEAN, nine transcendental ops over all 2^32 inputs each, 148 load/store kernels), consolidated receipt `evidence/E1-CLOSURE/e7851c69d34ac777a9436af16d0bdd5d69264c8528c62d562b600f5d89153061.json`; exclusions (natural-base EXP/LOG have no GB10 kernel) are listed in [E1_GAP_TABLE](docs/numeric/E1_GAP_TABLE.md).
- **Expert slots (Expert Ribbon, omega#533):** `src/omega_gpu_fp8_slots.h` keeps a fixed pool of FP8 expert slots that can be refilled while a model runs. A slot is hidden while it is rewritten, gets a new version only after the copy is complete, and cannot be rewritten while any launch or recorded command list still reads it. Shown on the GB10 (`tools/run_gpu_fp8_slots_chip.sh`, receipts in `evidence/RIBBON-SLOTS-*`): a slot gives bit-identical results to the same expert held resident, and the negative control (pins ignored) is caught. The SSD side and the model wiring live in aien-sovereign-core.
- **Hardware:** results on the DGX Spark GB10 are tied to the exact commit in each receipt. Host and emulator passes are never reported as hardware qualification.

The live status is owned by [aien-architecture](https://github.com/aien-spark/aien-architecture): [CURRENT_EXECUTION_PLAN.md](https://github.com/aien-spark/aien-architecture/blob/main/CURRENT_EXECUTION_PLAN.md) and [doctrine/ROADMAP.md](https://github.com/aien-spark/aien-architecture/blob/main/doctrine/ROADMAP.md). This file does not restate numbers that drift. Historical milestone receipts (for example Milestone 4, `OMEGA_SEMANTICS`, 12 of 12 gates) stay in `evidence/` unchanged as history.

## Core ideas

- **SMART is not TRUSTED, INTELLIGENCE is not AUTHORITY.** Trust comes from verified invariants, never from capability.
- **Meaning is permanent; representation is disposable.** Equivalent programs collapse to one `SEMANTIC_ID` (SHA-256 of the canonical encoding); different programs get different ones.
- **Pure computation is separate from effects**, and effects need capability-bounded, receipt-producing transactions.

## How this fits with the other repositories

[aienos](https://github.com/aien-spark/aienos) is the kernel Omega is meant to run on. [physics](https://github.com/aien-spark/physics) provides FORGE machine realization and the GB10 native path (pinned in `physics.lock`). [aien-architecture](https://github.com/aien-spark/aien-architecture) owns status and sequencing. ARGUS-0/1 (a defensive plane) is implemented in aienos (`native/argus`, pinned by `argus.lock`); Omega only carries the opt-in `RX_ARGUS` hooks (`src/runtime/rx_argus.c`) and receipts in `evidence/ARGUS/`. Physics Zero exists here as a host benchmark apparatus and learner (`src/physics0/`, `tests/physics0/`, `mk/physics0-learner.mk`, `docs/physics0/`, receipts in `evidence/physics0/`), with its specifications in the physics repository. DIRAC-0 is a specification in aien-architecture plus an oracle source in `research/dirac-oracle/` that has never been built or run. The Evolution Arena is a plan only.

## Standing rules

Language rule: `.om` is the only forward first-party implementation language. Implement and qualify each replacement, switch its actual consumers, then remove the superseded first-party source and obsolete build/runtime dependencies from the active tree. This includes compiler/runtime code, services, tools, automation and project-owned tests; C/Rust/shell remaining today are unfinished migration, not permanent exemptions. Preserve Git history and immutable evidence. External protocol formats, generated machine code, device firmware and independently sourced qualification oracles have separately declared roles; they must not conceal a foreign implementation of Omega. See the [replacement/deletion contract](https://github.com/aien-dev/aien-architecture-mv/issues/300).

Preserve the native GPU work and its explicitly recorded OS/driver dependencies; do not infer hardware or OS independence from removal of a userspace library. No systemd. Offline, in-house builds. Never overwrite failed experiments or old receipts. Verdict words: PASS, FAIL, NOT_RUN, BLOCKED_HARDWARE, BLOCKED_OPERATOR, MISSING_IMPLEMENTATION.

## Build and verify the existing default-branch implementation

These are **C/legacy compatibility checks**, not commands for the seeded `.om` candidates. Use each candidate's pinned reconstruction and qualification instructions for that profile; do not mix compiler binaries or relabel old receipts.

Needs gcc and make on an AArch64 Linux host: CI runs the host suites on `ubuntu-24.04-arm`, and `test-realize` compiles with `-march=armv8.6-a+dotprod+i8mm+sve`. Host suites need no GPU and no physics checkout:

```bash
make PHYSICS_DIR=/nonexistent PHYSICS_LOCK_CHECK=0 test-compiler-quick   # compiler, quick suite
make PHYSICS_DIR=/nonexistent PHYSICS_LOCK_CHECK=0 test-numeric-cpu      # E1 CPU numerics
make PHYSICS_DIR=/nonexistent PHYSICS_LOCK_CHECK=0 test-realize
```

The full omegatool build (`make`) links the `physics` repository at the commit in `physics.lock`. Some older milestone gate scripts rewrite their receipts in place, so do not run them on a clean checkout you mean to keep; prefer the `make test-*` targets above. Targets that touch the GB10 need the DGX Spark, run one at a time, and are never killed mid-run. CI definitions in `.github/workflows/` list the suites that run on every push.

## Repository layout

`src/` existing core and C compiler awaiting replacement (`src/compiler/`), `src/runtime/` reaction runtime, `src/fabric/` host-only fabric interface, `src/physics0/` Physics Zero apparatus, `spec/` and `docs/` specifications, `tests/` gate suites, `evidence/` content-addressed receipts, `tools/` harnesses, `mk/` make fragments included by the `Makefile`. Also `bench/prime_race/` (Prime Drag Race implementation), `calibration/` (Turing calibration experiments, preregistration and blinding protocol), `research/` (experiments and prototypes) and `doctrine/` (the Milestone 4 doctrine).

## Contributing

Open a pull request with the exact command you ran and its output. Receipts are content-addressed, name the exact commit and refuse a dirty tree. Do not edit existing evidence in place. External review is expected before merge. See [CONTRIBUTORS.md](CONTRIBUTORS.md). Contact: aien@aienos.com.

## Crumbs

This repo uses the Crumb Protocol (RFC-0001). Each directory carries a `.crumb` file (what the directory is for, its rules, and a backfilled history). Live coordination between agents goes in `.crumb.local`, which is never committed.
Before editing a directory, read its `.crumb`. Rules and the `crumb` tool: https://github.com/aien-spark/aien-architecture/blob/main/docs/CRUMB_PROTOCOL.md
Backfilled history (marked as such, not live whispers) is in `docs/crumbs/BACKFILL.md`.
