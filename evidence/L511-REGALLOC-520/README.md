# Omega #520 host lifetime oracle

Source `08600cfa52f0eec2fc8a5ccc0fe0175c5e341cc4`, Spark Linux aarch64. Built and ran `quiet-build make OUT_DIR=build/L511 test-gpu-regalloc-lifetime` with exit 0. Binary SHA-256 `e462d618b6508ada6ed7389c2c60810be0981feda8c34e95b0270f9618a5dcfc`. Raw build and run logs: handoffs/omega-issue-sweep-20261022/L511-regalloc-make.log and L511-regalloc-final.log.

The independent oracle enumerated all 27,000 combinations of three live intervals on four instruction positions with scalar, pair and quad register widths. Each production allocation was checked for span overlap, reserved-register range, alignment and reported peak. A deliberately forced overlapping physical assignment was rejected, and an over-capacity all-live set was refused by the production allocator. Result: 27,000/27,000 checks, one behavioral mutant killed, exhaustion refused.

This proves only the bounded synthetic host cases. It does not compare a candidate allocation policy, report two real kernels, run golden-word/simulator suites, or measure chip occupancy/latency. The existing linear-scan allocator remains the default. All remaining #520 gates are NOT_RUN.
