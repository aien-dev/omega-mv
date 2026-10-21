# The third piston: the CPU in the motor (L321, 2026-10-21)

Drake's picture, in his words: "get the whole machine involved in this motor. Think of it like GPU back to memory, CPU back
to memory, and hard drive." And: "If they share the memory, then that's one point where we can load something and they can
both use it." And: "If you find the timing just right, I bet you anything that it's going to be a miracle."

This page is the test of that picture on the DGX Spark, with every number from a file in `evidence/engine/cpu-piston/`.
Model: the 35B (Qwen3.6-35B-A3B). Nothing here is compared with anyone else's system; only with our own GPU path and the
machine's own limits.

## The three pistons and the one shared point

```
        NVMe drive (10.5 GB/s)            CPU die: 20 cores                              GPU die
        +-------------+             +------------------------------+          +--------------------------+
        | weights,    |  ------>    | each core: L1 64 KB, L2      |  C2C     | 24 MB GPU cache          |
        | next head   |             | (2 MB on the fast cores)     | <======> | 6,144 GPU cores          |
        +-------------+             | two clusters, 16 MB L3 each  |  link    |                          |
               |                    | 16 MB SYSTEM CACHE  <-- the  |          |                          |
               |                    |  shared point NVIDIA names   |          |                          |
               |                    | memory controllers           |          |                          |
               +------------------> +---------------+--------------+          +--------------------------+
                                                    |
                                     128 GB memory, one pipe, 273 GB/s rated (228 measured for the GPU alone)
```

What NVIDIA itself says (Hot Chips 2025 slides on this chip, kept at `~/docs/gb10/`): "16MB System Level Cache: Serves as
L4 cache for CPU, enables power efficient data-sharing between engines"; the chip is two dies, the CPU die holds "CPU, memory
subsystem, etc." and the GPU die reaches memory over the chip-to-chip link; "Hardware-enabled bidirectional coherency between
CPU and GPU". So the shared point Drake described exists on paper: a 16 MB cache next to the memory controllers that both
sides pass through. The experiments below measure what it actually does.

## The bar we have to beat (already measured, L151)

* GPU alone reading memory: 228 GB/s. All 20 CPU cores alone: 71 GB/s. The drive: 10.5 GB/s.
* All three at once on different data: 222 GB/s in total, less than the GPU alone. The memory pipe is one shared wall: adding
  readers splits it, it does not widen it. Every byte the CPU pulls from memory cost the GPU about 1.17 bytes.
* So a CPU piston only helps if its work does NOT pull bytes through the pipe: work whose data already sits in a cache, or
  data that one side loads once and the other side picks up from the shared point.

## E1: the shared point (does one side's read leave the data where the other side gets it?)

PENDING the chip window (ticket l321-e1). Method: a buffer X of 256 KB to 128 MB; one side touches it (GPU read, CPU read or
write, or the drive landing it), then the other side reads it 0, 1, 10, 100 or 1000 microseconds later. A random walk through
X, one line at a time, tells where each line came from by how long it took (own cache, cluster cache, system cache, memory).
Every arm has a control that does the same work on a different buffer, so a busy box blurs both sides equally.

First host-only run (no GPU, load 41 to 66): not usable for the cache question. Other agents' compile jobs ran on the same
CPU cluster and pushed even freshly read data out of its 16 MB cluster cache (a 4 MB buffer the core had just read came back
from memory, 317 ns per line). The run is kept as a receipt (`e1-host-20261021T0028Z/`), labelled contaminated.

## E1b: a second road (can the GPU pull data out of the CPU's caches over the chip-to-chip link?)

PENDING the chip window. Method: (1) the GPU reads a buffer the CPU just touched vs the same buffer cold; (2) one GPU pass
reads a CPU-hot block plus 64 MB of cold memory, vs all cold: a second road shows as total speed above the pipe's; (3) the CPU
reads GPU-hot data while the GPU keeps streaming memory at full speed.

## E2: timing (together vs taking turns)

PENDING the chip window. Every beat the GPU reads 256 MB and nine fast CPU cores read 64 MB; the CPU's start is moved across
the beat from 1.4 ms early to 1.4 ms late, plus both orders fully one after the other. The timing diagram of every beat is in
`phase-timeline.txt`.

## E3: what the CPU piston can carry on the 35B

The coordinator's numbers point at one candidate first: each conversation's DeltaNet memory. One layer of it is 32 x 128 x 128
numbers = 2 MiB, exactly one fast core's L2 cache, and it is read and written every word. On the GPU those 4 MiB cross the
pipe every layer: 18.4 microseconds per layer at 228 GB/s (calc:b5d6074b), 0.55 ms per word for all 30 layers
(calc:54f93706).

The CPU state piston (`tests/cpu_piston_state_test.c`): pinned fast cores each own some of the 32 heads of every layer and
keep that memory in their caches; per layer only 40 KB of small vectors go in and out. The arithmetic is the 35B reference's,
in the same order, with no fused multiply-add, and every step is checked bit for bit against a one-core copy of the
reference: **0 wrong values in every cell**.

Measured (load 30 to 45, `e3a-20261021T0048Z/`):

| Cores | One layer (2 MiB, stays in cache) | One whole conversation (30 layers, 60 MiB) |
|---|---|---|
| 1 | 189 us per layer | 691 us per layer |
| 2 | 87 us | 352 us |
| 4 | 47 to 51 us | 208 us = 6.26 ms per word (calc:5434f112) |
| 8 | wrecked: the box preempted the waiting cores (some steps took a quarter second) | |

A register-blocked version (same arithmetic, sums kept in registers) did one layer on 4 cores in 34 us median, 17 us at
best, on a lighter box (load 17). That run was a smoke check; the clean rerun is inside the chip window.

What it means so far: one layer's memory held by the CPU is close to the GPU's cost for it (17 to 34 us vs 18.4 us) but not
below it, and a whole conversation does not fit in the CPU's caches (60 MiB vs 20 MB of fast-core L2), so it falls back to
the memory pipe and gets ten times slower than the GPU. The CPU's arithmetic, kept bit-exact, runs at about 22 billion
operations a second per core in the first version.

## E4: the verdict

PENDING E1, E1b, E2 and the clean E3 rerun.

## What is not done, said plainly

* The full three-piston loop on the 35B with identical words needs the GPU's own 35B forward pass serving (L306's gates,
  still failing gate 1 at the time of writing). This page measures each piston's part and the shared point; the end-to-end
  words-per-second run waits for that.
