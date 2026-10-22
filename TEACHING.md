# One program, two identity paths

I chose semantic identity so a program's meaning can be named separately from one machine's code. The decision to make Omega both reaction runtime and compiler is in [the frozen decision record](docs/adr/OMEGA-SYSTEMS-CORE-0000.md). Current `main` still has a boundary a newcomer should see: OSC source compilation and the older `OmegaProgram` semantic identity and realization path are separate formats. [The compiler audit](docs/osc/ZIG-LESSONS-COMPTIME-INCREMENTAL.md) explicitly says an `OscUnit` to Omega semantic ID mapping is unverified. I will not call an OSC IR hash a `SEMANTIC_ID`.

For a small OSC input, [`oscc`](src/compiler/oscc_main.c) calls [`osc_compile`](src/compiler/osc_front.c), hashes canonical OSC IR through [`osc_ir_digest`](src/compiler/osc_ir.c), emits AArch64 bytes through [`osc_cg_compile`](src/compiler/osc_cg.c), and prints `ir_sha256`, `code_sha256` and a function count. The existing [factorial fixture](tests/compiler/progs/factorial.osc) checks a positive result and an overflow trap. A malformed fixture in [neg](tests/compiler/neg) is refused by the front end. Those hashes describe this compiler path and are not semantic identity or an execution receipt.

The separate [program realization fixture](tests/realize/test_program_realize.c) constructs an `OmegaProgram`, obtains its canonical program identity, asks the [realizer](src/omega_realize_synth.c) for machine code, executes it on AArch64 and compares output to an independent evaluator. Its gate checks a triple binding of semantic ID, machine ID and code, and rejects rebinding or tampering. The fixture is a host experiment, not the same OSC factorial program and not a GB10 result. I retain the existing [seed-born compiler walkthrough](docs/SEED_BIRTH.md), copied unchanged from pending branch `docs/seed-birth-walkthrough` at `b30ba6cf` rather than claiming it is merged into `main` or reproducing its separate bootstrapping lesson here.

## Replay

Use AArch64 Linux, gcc and make, with no GPU, model or physics checkout. In a fresh checkout of `a8ffd2b912c84197d08f963b05f9c7d8b30a3f8a`, run:

```sh
git clone https://github.com/aien-dev/omega-mv.git omega-lesson
cd omega-lesson
git checkout --detach a8ffd2b912c84197d08f963b05f9c7d8b30a3f8a
make PHYSICS_DIR=/nonexistent PHYSICS_LOCK_CHECK=0 test-program-realize
```

The `OMEGA_PROGRAM_REALIZE_PASS`, `OMEGA_REALIZATION_DIFFERENTIAL_PASS` and `OMEGA_REALIZATION_TRIPLE_BIND_PASS` gates should pass with exit 0. [TEACHING-REPLAY.txt](TEACHING-REPLAY.txt) records what actually happened. The full source-to-semantic-ID-to-receipt chain requested in [issue 619](https://github.com/aien-dev/omega-mv/issues/619) is not a single implemented `main` path. This lesson therefore qualifies only the existing program fixture and names the missing integration explicitly. Chip work and any hardware receipts require their own pinned, independent check.
