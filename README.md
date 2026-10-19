# OMEGA

Omega is the reaction runtime and the compiler for Omega Systems Core, written in C. It also carries AIEN's own GPU engine for the NVIDIA GB10 chip in the DGX Spark. Omega defines what computation means without tying that meaning to one machine representation: every program has a content-addressed semantic identity (a name computed from its exact bytes), and realizations (CPU, GB10 GPU) are derived from it and checked against it.

**Status 2026-10-19: research-grade, pre-alpha.** Some results are proven on the real GB10 chip, and each of those names its receipt below. Everything else runs on a host computer only or is not qualified, and is labelled that way.

```text
SILICON -> ATLAS -> PHYSICS/FORGE -> OMEGA -> AIEN        (full picture: aien-architecture)
```

Omega was decided by Drake on 2026-09-29 to be both the reaction runtime and the compiler for Omega Systems Core ([decision record](docs/adr/OMEGA-SYSTEMS-CORE-0000.md), frozen). This retires the earlier "not a compiler" wording.

## Where Omega fits in AIEN

[Aienix](https://github.com/aien-dev/aienix) is the core tree of AIEN and is still in its planning stage; it lists Omega as the language, semantic substrate and GPU engine. Start there for how the pieces fit together. The sibling repositories:

- [aienos-mv](https://github.com/aien-dev/aienos-mv): the kernel Omega is meant to run on. Omega pins it in `aienos.lock`. ARGUS-0/1 (a defensive plane) is implemented there (`native/argus`, pinned by `argus.lock`); Omega only carries the opt-in `RX_ARGUS` hooks (`src/runtime/rx_argus.c`) and receipts in `evidence/ARGUS/`.
- [physics-mv](https://github.com/aien-dev/physics-mv): FORGE machine realization and the GB10 native path. Omega links it at the commit in `physics.lock`.
- [aien-architecture-mv](https://github.com/aien-dev/aien-architecture-mv): owns status, sequencing and the decision records (ADRs).
- [aien-sovereign-core-mv](https://github.com/aien-dev/aien-sovereign-core-mv): the Rust host runtime that links Omega's GPU engine, and the `aien-test` Spark judge that tests every pull request here.
- [aien-protocols-mv](https://github.com/aien-dev/aien-protocols-mv): specifications Omega implements, for example the OSH shell ABI.
- [benchmarks-mv](https://github.com/aien-dev/benchmarks-mv): measurement harnesses, including the Prime Drag Race contract that `bench/prime_race/` builds on.
- [aien-receipts](https://github.com/aien-dev/aien-receipts): the signed test bundles the Spark judge publishes for each Omega commit.

## Current state

Research-grade and pre-alpha. Plainly:

- **Omega Systems Core compiler:** OSC-0 is frozen as a design. OSC-1 and OSC-2 are implemented with host receipts and are NOT QUALIFIED. OSC-3 is in progress ([OSC-3-DESIGN](docs/osc/OSC-3-DESIGN.md)). OSC-4 (floating point, slices, calls into C, system calls) is a design only, not yet implemented ([OSC-4-DESIGN](docs/osc/OSC-4-DESIGN.md), merged in #472). It is not self-hosting and there is no general Omega compiler yet; the old Milestone 6 "self-host" result was a fixed-output self-copy check, not compilation ([note](docs/osc/OSC-1-SELF-HOST-STATEMENT.md)).
- **Reaction runtime:** on the frozen candidate CAND-3 (omega `f816473`), R16 passed all eight gates G1 to G8 (receipt `evidence/R16/9dfb29957368bcc0acd8ac600f55efdd4ec31221d44ea4e52fbcf987b3cc4adc.json`, merged in #319) and R15 passed on the GB10 as a CONDITIONAL PASS, because Secure Boot was off for that run (declared exception 0002). The verdict of record is [CAND-3.gates.md section 8](https://github.com/aien-dev/aien-architecture-mv/blob/main/qualification/candidates/CAND-3.gates.md) in aien-architecture: full-candidate QUALIFIED is not asserted. The later candidate CAND-4 (omega `c0369e6`) records build reproducibility, not a new R15 or R16 run. Earlier history stays on file: requalification at `3108fc2` recorded R15 FAIL and R16 FAIL (`evidence/REQUAL-3108fc2/`), and the older R16 receipt `22d7a79a` is invalid as a current claim. Changes merged after the candidate freezes are not covered. The current living build is IMPLEMENTED / NOT QUALIFIED until re-qualified. Composition, Capability Graph, Skill Router, J-Space and Cortex run on the host; the Fabric interface is a host-only loopback with no network; none of this is on silicon as a qualified system.
- **Numerics (E1):** closed 2026-10-02 on the GB10 chip campaign (Gate 5 26/26 with 47 parity lines, reductions SUM/MAX/MIN/MEAN, nine transcendental ops over all 2^32 inputs each, 148 load/store kernels), consolidated receipt `evidence/E1-CLOSURE/e7851c69d34ac777a9436af16d0bdd5d69264c8528c62d562b600f5d89153061.json`; exclusions (natural-base EXP/LOG have no GB10 kernel) are listed in [E1_GAP_TABLE](docs/numeric/E1_GAP_TABLE.md).
- **GPU engine:** Omega drives the GB10 directly with its own code generator and launcher (`src/omega_blackwell_codegen*.c`, `src/omega_gpu_*.c`), with no CUDA toolkit. Matrix multiply, attention and ternary kernels have chip evidence folders under `evidence/` that name the exact commit and keep the raw logs, for example `evidence/WIDEPF-460-CHIP-7e041401/`, `evidence/MM-STAGES-a5a077d8/` and `evidence/TERN-T0-KT1-18185404-20261016T0151Z/`. These are engineering measurements, not a qualified serving product. Kernel work for the Qwen3.8-27B model is in open pull requests and is not on main.
- **Omegese:** a program language designed for an AI writer, so a model can write Omega programs without bookkeeping. v1, phase 1 done, phase 2 started ([OMEGESE.md](docs/one_layer/OMEGESE.md), `make test-omegese`). Host only.
- **OSH:** an Omega-native shell (lexer, parser and expander in Omega, with C references), host only. Its conformance vectors are in [tests/osh/vectors](tests/osh/vectors/README.md).
- **Hardware:** results on the DGX Spark GB10 are tied to the exact commit in each receipt. Host and emulator passes are never reported as hardware qualification.

Known problems on main, tracked as open issues (state read from GitHub on 2026-10-19):

- [#523](https://github.com/aien-dev/omega-mv/issues/523): the chip gates M19 and COMPOSITION-GATE-GPU fail on every omega head since 2026-10-15.
- [#526](https://github.com/aien-dev/omega-mv/issues/526): the M19 clean-clone step is refused by the Spark judge sandbox, so M19 fails before it reaches the chip.
- [#527](https://github.com/aien-dev/omega-mv/issues/527): R13-TESTBUILD-SILICON fails intermittently.
- [#524](https://github.com/aien-dev/omega-mv/issues/524): almost any `src/` change selects about 56 of the 60 gates, including the chip gates.

The live status is owned by [aien-architecture](https://github.com/aien-dev/aien-architecture-mv): [CURRENT_EXECUTION_PLAN.md](https://github.com/aien-dev/aien-architecture-mv/blob/main/CURRENT_EXECUTION_PLAN.md) and [doctrine/ROADMAP.md](https://github.com/aien-dev/aien-architecture-mv/blob/main/doctrine/ROADMAP.md). This file does not restate numbers that drift. Historical milestone receipts (for example Milestone 4, `OMEGA_SEMANTICS`, 12 of 12 gates) stay in `evidence/` unchanged as history.

## Core ideas

- **SMART is not TRUSTED, INTELLIGENCE is not AUTHORITY.** Trust comes from verified invariants, never from capability.
- **Meaning is permanent; representation is disposable.** Equivalent programs collapse to one `SEMANTIC_ID` (SHA-256 of the canonical encoding); different programs get different ones.
- **Pure computation is separate from effects**, and effects need capability-bounded, receipt-producing transactions.

## Other work in this repository

Physics Zero exists here as a host benchmark apparatus, learner and verifier (`src/physics0/`, `tests/physics0/`, `mk/physics0*.mk`, [docs/physics0](docs/physics0/README.md), receipts in `evidence/physics0/`), with its specifications in the physics repository. DIRAC-0 is a specification in aien-architecture plus an oracle source in `research/dirac-oracle/` that has never been built or run. The Evolution Arena is a plan only.

## Standing rules

Language rule: Rust is scaffolding, Omega is the destination, and C or assembly stay only where hardware, boot, ABI or measurement justifies them (aien-architecture ADR 0024, which supersedes the old "C is the target, no new Rust" rule). No Python anywhere (tools are C or shell). No CUDA toolkit or CUDA library dependence: the GB10 is driven natively. No systemd. Offline, in-house builds. Never overwrite failed experiments or old receipts. Verdict words: PASS, FAIL, NOT_RUN, BLOCKED_HARDWARE, BLOCKED_OPERATOR, MISSING_IMPLEMENTATION.

## Build and test

Needs gcc and make on an AArch64 Linux host (`test-realize` compiles with `-march=armv8.6-a+dotprod+i8mm+sve`). These host suites need no GPU and no physics checkout:

```bash
make PHYSICS_DIR=/nonexistent PHYSICS_LOCK_CHECK=0 test-compiler-quick   # compiler, quick suite
make PHYSICS_DIR=/nonexistent PHYSICS_LOCK_CHECK=0 test-numeric-cpu      # E1 CPU numerics
make PHYSICS_DIR=/nonexistent PHYSICS_LOCK_CHECK=0 test-realize          # algebra realizations
make PHYSICS_DIR=/nonexistent PHYSICS_LOCK_CHECK=0 test-omegese          # Omegese language
```

Each prints a final verdict line. On a fresh clone of main at `a8ffd2b9` on the DGX Spark (2026-10-19) they printed `test-compiler-quick: PASS`, `Gate 5 Verdict: PASS_EXCEPT_DECLARED_CHIP_ONLY` (the 5 skipped checks are declared chip-only), `MA2 test-realize PASS: 179746 checks, 0 failures, 10 realizations` and `test-omegese: PASS`.

The full omegatool build (`make`) links the `physics` repository at the commit in `physics.lock`. Some older milestone gate scripts rewrite their receipts in place, so do not run them on a clean checkout you mean to keep; prefer the `make test-*` targets above. Targets that touch the GB10 need the DGX Spark, run one at a time, and are never killed mid-run.

## How pull requests are tested

Since 2026-10-16 the DGX Spark is the only test judge for pull requests (aien-architecture [ADR 0033](https://github.com/aien-dev/aien-architecture-mv/blob/main/docs/adr/0033-aien-verification-pipeline.md)). Since 2026-10-18 (#564) no GitHub-hosted workflow builds or tests Omega's code; the two left in `.github/workflows/` only check the Spark's signed bundle (`aien-verification.yml`) and that existing evidence is never edited (`evidence-immutable.yml`).

- The gates the Spark runs are the 60 manifests in `tests/gates/`. A change runs the gates whose inputs it touches, plus the three in `.aien/always` (CRUMB-VERIFY, OMEGA-EVIDENCE-IMMUTABLE, OMEGA-ZERO-DISK-SECRETS).
- The verdict of record is the commit status "AIEN Verification (Spark)". The signed bundle is published to aien-receipts at `omega/<sha>/bundle.json`.
- [docs/PIPELINE.md](docs/PIPELINE.md) explains the whole path, including how the `ready-to-merge` label leads to a merge.

## Repository layout

- `src/`: core, the C compiler (`src/compiler/`), the reaction runtime (`src/runtime/`), the host-only fabric interface (`src/fabric/`), the GPU engine (`src/omega_blackwell_codegen*.c`, `src/omega_gpu_*.c`), the OSH shell (`src/osh/`) and Physics Zero (`src/physics0/`).
- `spec/` and `docs/`: specifications and design notes.
- `tests/`: test suites; `tests/gates/` holds the Spark gate manifests.
- `evidence/`: content-addressed receipts and chip evidence. Never edited in place.
- `tools/`: harnesses and helper tools. `mk/`: make fragments included by the `Makefile`.
- `bench/prime_race/`: the Prime Drag Race implementation side.
- `calibration/`: Turing calibration experiments, preregistration and blinding protocol.
- `research/`: experiments and prototypes. `doctrine/`: the Milestone 4 doctrine.
- `physics.lock`, `aienos.lock`, `argus.lock`, `crumb.lock`: the exact commits of the repositories and tools this one builds against.

## Contributing

Open a pull request with the exact command you ran and its output. Receipts are content-addressed, name the exact commit and refuse a dirty tree. Do not edit existing evidence in place. A pull request merges only after the Spark judge passes it and a maintainer adds the `ready-to-merge` label ([docs/PIPELINE.md](docs/PIPELINE.md)). See [CONTRIBUTORS.md](CONTRIBUTORS.md). Contact: aien@aienos.com.

## Crumbs

This repo uses the Crumb Protocol (RFC-0001). Each directory carries a `.crumb` file (what the directory is for, its rules, and a backfilled history). Live coordination between agents goes in `.crumb.local`, which is never committed.
Before editing a directory, read its `.crumb`, and run `crumb verify .` before you push. Rules and the `crumb` tool: [CRUMB_PROTOCOL.md](https://github.com/aien-dev/aien-architecture-mv/blob/main/docs/CRUMB_PROTOCOL.md) in aien-architecture.
Backfilled history (marked as such, not live whispers) is in `docs/crumbs/BACKFILL.md`.

## License

GNU Affero General Public License, version 3 ([LICENSE](LICENSE)).
