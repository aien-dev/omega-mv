# L717 evidence index

Status: **TESTED** on the Spark Linux AArch64 host with the pinned static Omega compiler; not a GPU, cross-platform, kernel, service, or performance qualification.

## Frozen behavior

`omega/native/universal/buffers/CONTRACT.md` defines version 1: 16 slots, 256 bytes per slot, CPU-host `uchar` only, rights `READ=1` / `WRITE=2`, 64-bit index+generation handles, stale-generation refusal, max-generation slot retirement, deterministic release/clear, distinct source/destination, 16-byte copy checkpoints, and named statuses. `ubnew` returns zero for refusal. The pool uses statically bounded Omega globals; no heap, raw addresses, async queues, GPU, model, or service code.

The accepted `ubcopy` path copies actual bytes in the Omega backing pool. `ubcsched` is a deterministic cancellation-delivery seam for tests and bounded callers: the first 16-byte chunk is copied, cancellation is observed at the checkpoint, and the untouched suffix retains its sentinel. Metrics are updated from actual byte writes and descriptor allocations, not estimated transfer counters.

## Source and test

- `omega/native/universal/buffers/buffers.om`: implementation.
- `omega/native/universal/buffers/tests/test_globals.om`, `test_buffers.om`: Omega-native behavioral checks, concatenated after the implementation for seeded-compiler execution.
- `omega/native/universal/buffers/run.sh`: host orchestration only; pins/checks compiler before each compile, builds a fresh static AArch64 EXEC, runs the candidate and compiled mutants, and emits hashes. It writes only to the caller-selected fresh output directory.
- `mutants/`: compiled behavioral negatives for skipped generation, rights, and cancellation checks.
- `red/`: valid compiled stub plus final tests, retained to show behavioral RED rather than compiler/setup failure.
- `author-run/`: replay outputs, machine-readable raw status summary, ELF metadata, and content hashes.

## Verification

Run from the repository root on the Spark host:

```sh
omega/native/universal/buffers/run.sh build/L717/fresh-replay
```

The output directory must not exist. Required compiler: `/home/drakestapleton/handoffs/universal-adapter-L707/shared/omc`, SHA256 `191b776ef98821597c3e30655da6d680a9b66edbc9ceaca3c39c61ea6df098e9`.

The author replay in `author-run/RESULT.txt` records the exact command outcomes. Every mutant compiled to nonempty AArch64 EXEC and exited nonzero under the same test corpus; the candidate compiled and exited zero. No timings were collected. This module does not claim zero-copy, speedup, GPU residency, native OS services, application integration, or production cutover. Parent source review and broader integration remain open.
