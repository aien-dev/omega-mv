# OMEGA

Omega is the reaction runtime and the compiler for Omega Systems Core, written in C. It defines what computation means without committing that meaning to one machine representation: every program has a content-addressed semantic identity, and realizations (CPU, GB10 GPU) are derived from it and verified against it.

```text
SILICON -> ATLAS -> PHYSICS/FORGE -> OMEGA -> AIEN        (full picture: aien-architecture)
```

Omega was decided by Drake on 2026-09-29 to be both the reaction runtime and the compiler for Omega Systems Core ([decision record](docs/adr/OMEGA-SYSTEMS-CORE-0000.md), frozen). This retires the earlier "not a compiler" wording.

## Current state

Research-grade and pre-alpha. Plainly:

- **Omega Systems Core compiler:** OSC-0 is frozen as a design. OSC-1 and OSC-2 are implemented with host receipts and are NOT QUALIFIED. OSC-3 is in progress ([OSC-3-DESIGN](docs/osc/OSC-3-DESIGN.md)). It is not self-hosting and there is no general Omega compiler yet; the old Milestone 6 "self-host" result was a fixed-output self-copy check, not compilation ([note](docs/osc/OSC-1-SELF-HOST-STATEMENT.md)).
- **Reaction runtime:** the R1 to R16 qualification ladder passed on builds that predate the composition modules (omega#126), a historical result. Requalification at `3108fc2` recorded R15 FAIL (14 of 16 gates: G1, G15) and R16 FAIL (`evidence/REQUAL-3108fc2/`); the R16 receipt `22d7a79a` at `850fc54` is invalid as a current claim because its script was later found defective; none of R1 to R16 is requalified on the current candidate, and the ladder rerun is in progress. The R16 loop-site inventory was reconciled on the candidate tree (502 sites, 0 unclassified, 0 stale, PR #279); that is the inventory gate only, not R16. The current living build is IMPLEMENTED / NOT QUALIFIED until re-qualified. Composition, Capability Graph, Skill Router, J-Space and Cortex run on the host; the Fabric interface is a host-only loopback with no network; none of this is on silicon as a qualified system.
- **Numerics (E1):** closed 2026-10-02 on the GB10 chip campaign (Gate 5 26/26 with 47 parity lines, reductions SUM/MAX/MIN/MEAN, nine transcendental ops over all 2^32 inputs each, 148 load/store kernels), consolidated receipt `evidence/E1-CLOSURE/e7851c69d34ac777a9436af16d0bdd5d69264c8528c62d562b600f5d89153061.json`; exclusions (natural-base EXP/LOG have no GB10 kernel) are listed in [E1_GAP_TABLE](docs/numeric/E1_GAP_TABLE.md).
- **Qwen3.8-27B kernels (GB10, 2026-10-18):** each one is checked on the chip against a double-precision reference with deliberately broken variants that must fail, and none is joined into a serving engine yet. FP8 matrix-vector and the tensor-core prefill GEMM now take k up to 32768, which covers the 27B down projection at k = 17408 (`evidence/fp8-k32768-20261018/`, omega#541). The Gated DeltaNet decode step (the recurrent layer used by 48 of the 64 layers) runs as one launch per layer and matches the reference to about 2e-7; it takes 125.7 us per layer, which moves its 6.29 MB of state at 50.1 GB/s against 273 GB/s nominal (`evidence/gdn-decode-20261018/`, omega#542). The DeltaNet prefill reads a whole prompt in three launches per layer and hands its state to the decode step, which carries on exactly; at 2048 tokens it takes 3.80 us per token per layer, and the recurrent scan in it does about 2.4 trillion operations a second, against a chip peak for that kind of math that has not been measured yet (`evidence/gdn-prefill-20261018/`, omega#543). The full-attention decode step for the other 16 layers (24 query heads over 4 key-value heads, 256 numbers per head, rotation on the first 64 only, done by reordering the numbers once at load) runs as one launch per layer, matches the reference at each tested context (1, 7, 256, 301, 1024, 4096, 8192, 32768 and 131072 tokens) with all five broken variants caught, and at 32768 tokens of context takes 2.10 ms per layer, streaming its 134 MB of cached keys and values at 63.8 GB/s against 273 GB/s nominal (`evidence/QWEN38-DATTN256-8bb4494e-20261018/`, omega#544). The prefill attention for those 16 layers reads a 2048-token chunk of a prompt in two launches per layer and leaves cache rows bit-equal to the ones the decode step would have written; it matches the reference at every tested point up to 129024 + 2048 tokens with all four broken variants caught, and a 2048-token chunk takes 10.0 ms per layer at the start of a prompt and 591 ms at the end of a 128k one, streaming cached keys and values at 230.6 GB/s against 273 GB/s nominal (`evidence/pattn256-chip-20261018T1517Z/`, omega#545). Getting it to run found a bug every kernel shared: the encoder wrote the integer add, one compare and the exit instruction with a fixed timing word, so the waits the code generator scheduled on them never reached the chip. The encoder now keeps them, `make test-bw-encode-control` checks every instruction type, and 18 of 19 kernel families pass their chip tests with the fix (the ternary family fails only its speed gates, which the old encoder fails the same way on the same load; `evidence/encoder-fix-sweep-20261018T1512Z/`).
- **Hardware:** results on the DGX Spark GB10 are tied to the exact commit in each receipt. Host and emulator passes are never reported as hardware qualification.

The live status is owned by [aien-architecture](https://github.com/aien-spark/aien-architecture): [CURRENT_EXECUTION_PLAN.md](https://github.com/aien-spark/aien-architecture/blob/main/CURRENT_EXECUTION_PLAN.md) and [doctrine/ROADMAP.md](https://github.com/aien-spark/aien-architecture/blob/main/doctrine/ROADMAP.md). This file does not restate numbers that drift. Historical milestone receipts (for example Milestone 4, `OMEGA_SEMANTICS`, 12 of 12 gates) stay in `evidence/` unchanged as history.

## Core ideas

- **SMART is not TRUSTED, INTELLIGENCE is not AUTHORITY.** Trust comes from verified invariants, never from capability.
- **Meaning is permanent; representation is disposable.** Equivalent programs collapse to one `SEMANTIC_ID` (SHA-256 of the canonical encoding); different programs get different ones.
- **Pure computation is separate from effects**, and effects need capability-bounded, receipt-producing transactions.

## How this fits with the other repositories

[aienos](https://github.com/aien-spark/aienos) is the kernel Omega is meant to run on. [physics](https://github.com/aien-spark/physics) provides FORGE machine realization and the GB10 native path (pinned in `physics.lock`). [aien-architecture](https://github.com/aien-spark/aien-architecture) owns status and sequencing. ARGUS-0/1 (a defensive plane) is implemented in aienos (`native/argus`, pinned by `argus.lock`); Omega only carries the opt-in `RX_ARGUS` hooks (`src/runtime/rx_argus.c`) and receipts in `evidence/ARGUS/`. Physics Zero exists here as a host benchmark apparatus and learner (`src/physics0/`, `tests/physics0/`, `mk/physics0-learner.mk`, `docs/physics0/`, receipts in `evidence/physics0/`), with its specifications in the physics repository. DIRAC-0 is a specification in aien-architecture plus an oracle source in `research/dirac-oracle/` that has never been built or run. The Evolution Arena is a plan only.

## Standing rules

Language rule: Rust is scaffolding, Omega is the destination, and C or assembly stay only where hardware, boot, ABI or measurement justifies them (aien-architecture ADR 0024, which supersedes the old "C is the target, no new Rust" rule). No Python anywhere (tools are C or shell). No CUDA toolkit or CUDA library dependence: the GB10 is driven natively. No systemd. Offline, in-house builds. Never overwrite failed experiments or old receipts. Verdict words: PASS, FAIL, NOT_RUN, BLOCKED_HARDWARE, BLOCKED_OPERATOR, MISSING_IMPLEMENTATION.

## Build and verify

Needs gcc and make on an AArch64 Linux host: CI runs the host suites on `ubuntu-24.04-arm`, and `test-realize` compiles with `-march=armv8.6-a+dotprod+i8mm+sve`. Host suites need no GPU and no physics checkout:

```bash
make PHYSICS_DIR=/nonexistent PHYSICS_LOCK_CHECK=0 test-compiler-quick   # compiler, quick suite
make PHYSICS_DIR=/nonexistent PHYSICS_LOCK_CHECK=0 test-numeric-cpu      # E1 CPU numerics
make PHYSICS_DIR=/nonexistent PHYSICS_LOCK_CHECK=0 test-realize
```

The full omegatool build (`make`) links the `physics` repository at the commit in `physics.lock`. Some older milestone gate scripts rewrite their receipts in place, so do not run them on a clean checkout you mean to keep; prefer the `make test-*` targets above. Targets that touch the GB10 need the DGX Spark, run one at a time, and are never killed mid-run. CI definitions in `.github/workflows/` list the suites that run on every push.

## Repository layout

`src/` core and C compiler (`src/compiler/`), `src/runtime/` reaction runtime, `src/fabric/` host-only fabric interface, `src/physics0/` Physics Zero apparatus, `spec/` and `docs/` specifications, `tests/` gate suites, `evidence/` content-addressed receipts, `tools/` harnesses, `mk/` make fragments included by the `Makefile`. Also `bench/prime_race/` (Prime Drag Race implementation), `calibration/` (Turing calibration experiments, preregistration and blinding protocol), `research/` (experiments and prototypes) and `doctrine/` (the Milestone 4 doctrine).

## Contributing

Open a pull request with the exact command you ran and its output. Receipts are content-addressed, name the exact commit and refuse a dirty tree. Do not edit existing evidence in place. External review is expected before merge. See [CONTRIBUTORS.md](CONTRIBUTORS.md). Contact: aien@aienos.com.

## Crumbs

This repo uses the Crumb Protocol (RFC-0001). Each directory carries a `.crumb` file (what the directory is for, its rules, and a backfilled history). Live coordination between agents goes in `.crumb.local`, which is never committed.
Before editing a directory, read its `.crumb`. Rules and the `crumb` tool: https://github.com/aien-spark/aien-architecture/blob/main/docs/CRUMB_PROTOCOL.md
Backfilled history (marked as such, not live whispers) is in `docs/crumbs/BACKFILL.md`.
