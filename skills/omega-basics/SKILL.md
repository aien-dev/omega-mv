---
name: omega-basics
description: How to build and test omega (the reaction runtime and the Omega Systems Core compiler, in C), the gates the Spark judge runs, the invariants that must not move, where receipts live, and what agents must not touch. Read before editing anything in this repository.
---

# omega basics

Facts from this repository's `README.md`, `Makefile`, `tests/gates/` and `docs/PIPELINE.md`.
Status is owned by aien-architecture (`CURRENT_EXECUTION_PLAN.md`, `doctrine/ROADMAP.md`); this
file does not restate status numbers.

## Build

- Needs gcc and make on an AArch64 Linux host. `make` builds `build/omegatool` and links the
  physics repository at the commit in `physics.lock` (`PHYSICS_DIR` defaults to `../physics`;
  `check-physics-lock` refuses a physics checkout at any other commit).
- Host suites that need no GPU and no physics checkout:
  `make PHYSICS_DIR=/nonexistent PHYSICS_LOCK_CHECK=0 test-compiler-quick`,
  `... test-numeric-cpu`, `... test-realize`.
- Make fragments live in `mk/` and are included by the `Makefile`.

## Full host test target and gates

- `make test` is the base host target: it builds `omegatool` and runs `--run-gates` plus the two
  reference demonstrations. Pass means the log line `TOTAL GATES: N | PASSED: N | FAILED: 0`.
- `tests/gates/run.sh <step>` is the one entry point for every gate; each `tests/gates/*.gate`
  manifest names this script and one step word. Example: `tests/gates/run.sh base` is the
  OMEGA-BASE gate (`make test` with physics materialised at `physics.lock` under `build/pins/`
  from the sibling clone `../physics`). Steps that need a sibling exit 3 when it is missing.
- `.aien/always` lists the gates that run on every commit: CRUMB-VERIFY,
  OMEGA-EVIDENCE-IMMUTABLE, OMEGA-ZERO-DISK-SECRETS.
- The DGX Spark is the only test judge for pull requests (`docs/PIPELINE.md`). Its verdict is the
  commit status "AIEN Verification (Spark)".
- Targets that touch the GB10 need the DGX Spark, run one at a time under `/tmp/aien-gb10.lock`,
  and are never killed mid-run. Do not take that lock yourself.
- Some older milestone gate scripts rewrite their receipts in place: do not run them on a
  checkout you mean to keep; prefer the `make test-*` targets.

## Invariants that must not move

- Equivalent programs collapse to one `SEMANTIC_ID` (SHA-256 of the canonical encoding);
  different programs get different ones.
- Pure computation is separate from effects; effects need capability-bounded,
  receipt-producing transactions.
- Language rule (aien-architecture ADR 0024): Rust is scaffolding, Omega is the destination, C
  or assembly only where hardware, boot, ABI or measurement justifies it. No Python anywhere
  (tools are C or shell). No CUDA toolkit or CUDA library dependence. No systemd. Offline,
  in-house builds.
- Verdict words: PASS, FAIL, NOT_RUN, BLOCKED_HARDWARE, BLOCKED_OPERATOR,
  MISSING_IMPLEMENTATION. Host and emulator passes are never reported as hardware
  qualification.
- `physics.lock`, `aienos.lock` and `argus.lock` pin sibling repositories by commit.

## Receipts and evidence

- `evidence/` holds content-addressed receipts (`<name>.<sha256>.json`). They name the exact
  commit and refuse a dirty tree.
- `evidence/` is additions only: the evidence-immutable gate fails a change that modifies,
  renames or deletes an existing file there. Never overwrite failed experiments or old receipts.
- Signed Spark results are published to aien-dev/aien-receipts at `omega/<sha>/bundle.json`.

## Do not touch

- `.crumb`, `crumb.lock` (Crumb Protocol files; do not run `crumb compile`).
- `.aien/`, `tests/gates/`, `.github/workflows/`, `.forgejo/`, `tools/aien-test/`: a head that changes these is
  BLOCKED_OPERATOR and only the operator runs it (`docs/PIPELINE.md`).
- Existing files under `evidence/`.
