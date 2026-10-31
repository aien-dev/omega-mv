# Recovered R3 portability qualification (L636, omega issue596)

The original author delivered `runtime/r3-portability` at `92cd9145766ef9ef93baa15ad4e7a044b7865f67` (L091). It was not in the authoritative local READY queue and had no matching PR. This branch reuses that work; no second runtime/compiler/checker implementation was built. Original `evidence/RUNTIME-R3-PORT` receipts are untouched.

## Fresh host replay

On an ARM64 Mac running macOS26.6.2, `TMPDIR=$HOME/.hermes/cache/scratch make test-runtime-portability` exited0. `host-test.log.gz` and `manifest.json` pin the original source and raw-log hash. These are row/include checks, not GPU qualification.

## Independent finding and parent correction

L637 (`deleg_a3c13f6f`) independently read the issue, source, portability reasons and controls. It counted the original15 PORTABLE rows, replayed the102-row check and the eight controls, and found an acceptance-blocking overclaim: several headers expose chip assumptions despite PORTABLE labels. Its transcript was checked by the parent. No independent final re-review is claimed.

The parent read the actual headers rather than treating hardware names alone as proof. Stronger evidence supports correction:

- matmul header: Blackwell16/8/16 tile macros, CTA envelope and GPU-uncached resident memory;
- MoE header: tile-major packing, column-modulo8 alignment and Blackwell shape limits;
- platform header: GPFIFO queue semantics and NVIDIA host-polled cache policy, not only its ordinal example;
- session header: QMD fields/default1024 and two-marker/L2-flush launch protocol;
- serving header: GB10-specific reservation/cache contract;
- sibling serving implementation: calls the chip-specific matmul/attention reserve APIs, not an independent generic accounting layer.

All six table rows and their reasons were corrected, and section3/counts were updated at `160ee31803d68e3ef9bfb819bfa1235b365ea0f8`. Counts computed from the table are now93 CHIP_SPECIFIC and9 PORTABLE. OS-binding labels and production source are unchanged. Mere chip names in historical examples do not make the remaining generic engine/wait/math helpers chip-specific; their portability is not a whole-backend support claim.

`check-review-claims.py` is a named external documentary oracle, not runtime or a replacement checker. It verifies real source anchors against the corrected row/reason claims. Before edits it exited1 (`classification-red.json`); after edits it exited0 (`classification-green.json`). The complete original canonical host target was rerun on the corrected table and all controls behaved (`corrected-host-test.log.gz`). Source-review remains necessary: neither this case-specific oracle nor the include scanner can mechanically prove all future prose correct.

## Scope and delivery

No runtime/compiler/driver/gate implementation was changed, no GPU/model was loaded and no performance result is claimed. Shell/awk are the original checker; Python here is only an external documentary test oracle. WSL/native/other-GPU readiness remains unqualified. Required pinned crumb preparation, exact local queue ACK, Renda PR/judge/merge and issue closure are separate steps; an author receipt or README READY line is not remote acceptance.
