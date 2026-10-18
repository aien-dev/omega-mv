# OMEGA

Omega is the reaction runtime and the compiler for Omega Systems Core, written in C, and it carries AIEN's own GPU engine for the NVIDIA GB10 chip. It defines what computation means without committing that meaning to one machine representation: every program has a content-addressed semantic identity, and realizations (CPU, GB10 GPU) are derived from it and verified against it.

Part of [Aienix](https://github.com/aien-dev/aienix), the core tree of AIEN. Start there for how the pieces fit together.

**Status 2026-10-18: research-grade, pre-alpha.** Some results are proven on the real GB10 chip, and each of those names its receipt below. Everything else runs on a host computer only or is not qualified, and is labelled that way.

```text
SILICON -> ATLAS -> PHYSICS/FORGE -> OMEGA -> AIEN        (full picture: aien-architecture)
```

Omega was decided by Drake on 2026-09-29 to be both the reaction runtime and the compiler for Omega Systems Core ([decision record](docs/adr/OMEGA-SYSTEMS-CORE-0000.md), frozen). This retires the earlier "not a compiler" wording.

## Current state

Research-grade and pre-alpha. Plainly:

- **Omega Systems Core compiler:** OSC-0 is frozen as a design. OSC-1 and OSC-2 are implemented with host receipts and are NOT QUALIFIED. OSC-3 is in progress ([OSC-3-DESIGN](docs/osc/OSC-3-DESIGN.md)). OSC-4 (floating point, slices, calls into C, system calls) is a design only, not yet implemented ([OSC-4-DESIGN](docs/osc/OSC-4-DESIGN.md)). It is not self-hosting and there is no general Omega compiler yet; the old Milestone 6 "self-host" result was a fixed-output self-copy check, not compilation ([note](docs/osc/OSC-1-SELF-HOST-STATEMENT.md)).
- **Reaction runtime:** on the frozen candidate CAND-3 (omega `f816473`), R16 passed all eight gates G1 to G8 (receipt `evidence/R16/9dfb29957368bcc0acd8ac600f55efdd4ec31221d44ea4e52fbcf987b3cc4adc.json`, merged in #319) and R15 passed 16 of 16 gates on the GB10, recorded as a CONDITIONAL PASS because Secure Boot was off for that run (declared exception 0002). The verdict of record is aien-architecture `qualification/candidates/CAND-3.gates.md` section 8: full-candidate QUALIFIED is not asserted. Earlier history stays on file: requalification at `3108fc2` recorded R15 FAIL and R16 FAIL (`evidence/REQUAL-3108fc2/`), and the older R16 receipt `22d7a79a` is invalid as a current claim. Changes merged after the CAND-3 freeze are not covered and need a later named candidate. The current living build is IMPLEMENTED / NOT QUALIFIED until re-qualified. Composition, Capability Graph, Skill Router, J-Space and Cortex run on the host; the Fabric interface is a host-only loopback with no network; none of this is on silicon as a qualified system.
- **Numerics (E1):** closed 2026-10-02 on the GB10 chip campaign (Gate 5 26/26 with 47 parity lines, reductions SUM/MAX/MIN/MEAN, nine transcendental ops over all 2^32 inputs each, 148 load/store kernels), consolidated receipt `evidence/E1-CLOSURE/e7851c69d34ac777a9436af16d0bdd5d69264c8528c62d562b600f5d89153061.json`; exclusions (natural-base EXP/LOG have no GB10 kernel) are listed in [E1_GAP_TABLE](docs/numeric/E1_GAP_TABLE.md).
- **GPU engine:** Omega drives the GB10 directly with its own code generator and launcher (`src/omega_blackwell_codegen*.c`, `src/omega_gpu_*.c`), with no CUDA toolkit. Matrix multiply, attention and ternary kernels have chip evidence folders under `evidence/` that name the exact commit and keep the raw logs, for example `evidence/WIDEPF-460-CHIP-7e041401/`. These are engineering measurements, not a qualified serving product.
- **Omegese:** a program language designed for an AI writer, so a model can write Omega programs without bookkeeping. v1, phase 1 done, phase 2 started ([OMEGESE.md](docs/one_layer/OMEGESE.md), `make test-omegese`). Host only.
- **Hardware:** results on the DGX Spark GB10 are tied to the exact commit in each receipt. Host and emulator passes are never reported as hardware qualification.

The live status is owned by [aien-architecture](https://github.com/aien-dev/aien-architecture): [CURRENT_EXECUTION_PLAN.md](https://github.com/aien-dev/aien-architecture/blob/main/CURRENT_EXECUTION_PLAN.md) and [doctrine/ROADMAP.md](https://github.com/aien-dev/aien-architecture/blob/main/doctrine/ROADMAP.md). This file does not restate numbers that drift. Historical milestone receipts (for example Milestone 4, `OMEGA_SEMANTICS`, 12 of 12 gates) stay in `evidence/` unchanged as history.

## Core ideas

- **SMART is not TRUSTED, INTELLIGENCE is not AUTHORITY.** Trust comes from verified invariants, never from capability.
- **Meaning is permanent; representation is disposable.** Equivalent programs collapse to one `SEMANTIC_ID` (SHA-256 of the canonical encoding); different programs get different ones.
- **Pure computation is separate from effects**, and effects need capability-bounded, receipt-producing transactions.

## How this fits with the other repositories

[aienos](https://github.com/aien-dev/aienos) is the kernel Omega is meant to run on. [physics](https://github.com/aien-dev/physics) provides FORGE machine realization and the GB10 native path (pinned in `physics.lock`). [aien-architecture](https://github.com/aien-dev/aien-architecture) owns status and sequencing. ARGUS-0/1 (a defensive plane) is implemented in aienos (`native/argus`, pinned by `argus.lock`); Omega only carries the opt-in `RX_ARGUS` hooks (`src/runtime/rx_argus.c`) and receipts in `evidence/ARGUS/`. Physics Zero exists here as a host benchmark apparatus and learner (`src/physics0/`, `tests/physics0/`, `mk/physics0-learner.mk`, `docs/physics0/`, receipts in `evidence/physics0/`), with its specifications in the physics repository. DIRAC-0 is a specification in aien-architecture plus an oracle source in `research/dirac-oracle/` that has never been built or run. The Evolution Arena is a plan only.

## Standing rules

Language rule: Rust is scaffolding, Omega is the destination, and C or assembly stay only where hardware, boot, ABI or measurement justifies them (aien-architecture ADR 0024, which supersedes the old "C is the target, no new Rust" rule). No Python anywhere (tools are C or shell). No CUDA toolkit or CUDA library dependence: the GB10 is driven natively. No systemd. Offline, in-house builds. Never overwrite failed experiments or old receipts. Verdict words: PASS, FAIL, NOT_RUN, BLOCKED_HARDWARE, BLOCKED_OPERATOR, MISSING_IMPLEMENTATION.

## Build and verify

Needs gcc and make on an AArch64 Linux host: CI runs the host suites on `ubuntu-24.04-arm`, and `test-realize` compiles with `-march=armv8.6-a+dotprod+i8mm+sve`. Host suites need no GPU and no physics checkout:

```bash
make PHYSICS_DIR=/nonexistent PHYSICS_LOCK_CHECK=0 test-compiler-quick   # compiler, quick suite
make PHYSICS_DIR=/nonexistent PHYSICS_LOCK_CHECK=0 test-numeric-cpu      # E1 CPU numerics
make PHYSICS_DIR=/nonexistent PHYSICS_LOCK_CHECK=0 test-realize
```

The full omegatool build (`make`) links the `physics` repository at the commit in `physics.lock`. Some older milestone gate scripts rewrite their receipts in place, so do not run them on a clean checkout you mean to keep; prefer the `make test-*` targets above. Targets that touch the GB10 need the DGX Spark, run one at a time, and are never killed mid-run.

Since 2026-10-16 the DGX Spark is the only test judge for pull requests (aien-architecture ADR 0033). The gates it runs live in `tests/gates/`; [docs/PIPELINE.md](docs/PIPELINE.md) explains how a pull request is tested, how the signed evidence bundle is published, and how the `ready-to-merge` label leads to a merge. The verdict of record is the commit status "AIEN Verification (Spark)".

## Repository layout

`src/` core and C compiler (`src/compiler/`), `src/runtime/` reaction runtime, `src/fabric/` host-only fabric interface, `src/physics0/` Physics Zero apparatus, `spec/` and `docs/` specifications, `tests/` gate suites, `evidence/` content-addressed receipts, `tools/` harnesses, `mk/` make fragments included by the `Makefile`. Also `bench/prime_race/` (Prime Drag Race implementation), `calibration/` (Turing calibration experiments, preregistration and blinding protocol), `research/` (experiments and prototypes) and `doctrine/` (the Milestone 4 doctrine).

## Contributing

Open a pull request with the exact command you ran and its output. Receipts are content-addressed, name the exact commit and refuse a dirty tree. Do not edit existing evidence in place. A pull request merges only after the Spark judge passes it and a maintainer adds the `ready-to-merge` label ([docs/PIPELINE.md](docs/PIPELINE.md)). See [CONTRIBUTORS.md](CONTRIBUTORS.md). Contact: aien@aienos.com.

## Crumbs

This repo uses the Crumb Protocol (RFC-0001). Each directory carries a `.crumb` file (what the directory is for, its rules, and a backfilled history). Live coordination between agents goes in `.crumb.local`, which is never committed.
Before editing a directory, read its `.crumb`. Rules and the `crumb` tool: https://github.com/aien-dev/aien-architecture/blob/main/docs/CRUMB_PROTOCOL.md
Backfilled history (marked as such, not live whispers) is in `docs/crumbs/BACKFILL.md`.
