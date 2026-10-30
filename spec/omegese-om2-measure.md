# OM-2 SHA-256 measurement protocol (coordinator L835; frozen before any cutover measurement)

Arms:
- **A**, the baseline: main `acb3f15c` (OM-1 Omega SHA through the v1 adapter).
- **B**, the candidate: the head of `om2/sha-cutover` (compact OM-2 SHA through the ABI v2 binding).
- **C**, a reference only: the historical C `src/sha256.c` at `acb3f15c`. It has no gate role.

The two arms are built from clean clones with their own pinned omc. The record holds the source sha256, omc sha256, artifact sha256 and toolchain versions.

Machine: the GB10 host CPU (Cortex-X925 cores), pinned with `taskset` to one core, under `nice -n 19`. There is no GPU, and ports 18080/18091 are left alone. Concurrent load is recorded (`uptime` before and after each block). The class is HOST.

Design: paired and interleaved. Each round runs A, B and C in a random order, seeded and recorded. There are 31 rounds per size, after 3 discarded warm-up rounds. Cold is the first call after process start, recorded separately over 31 fresh processes per arm.

Sizes: 0, 1, 31, 32, 55, 56, 63, 64, 65, 127, 128, 1 KiB, 4 KiB, 64 KiB, 1 MiB, 16 MiB, plus a streaming case with 1 MiB fed in 1/7/64/4096-byte chunks. The output of every timed call is checked against sha256sum; a wrong output voids the run.

Metrics:
- per size: median ns/call (min..max) and p10/p90;
- bulk: MB/s;
- per size: the paired ratio B/A (median of the per-round ratios, with min..max);
- code size: text bytes of the SHA code in each arm;
- static and observed stack;
- peak RSS of the driver;
- compile time of the SHA unit (median of 41).

Regression policy, decided before measurement:
- **PASS:** for every size, the median paired ratio B/A is ≤ 1.05, and the bulk ratio (≥ 64 KiB) is ≤ 1.02.
- **REGRESSION:** any size above those limits, by its median paired ratio. It is reported as such, with no re-run selection. A re-run is allowed only for a documented machine-load fault, and both runs are reported.
- Tiny inputs (0–64 B) are reported separately, because call overhead (ABI v2 view setup ≈7.4 ns) dominates there.
- C is reported for context only and is never used to decide PASS.
- Raw per-round samples are committed. Only MEASURED numbers are reported; anything else is NOT_RUN.
