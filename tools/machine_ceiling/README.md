# Machine ceiling probes (L151)

These tools measure how fast each part of this box can move data or do arithmetic. The table they feed is
`docs/MACHINE_CEILING.md`. Each tool checks its own results, so a fast wrong run counts as a failure, not a record.

| Tool | What it measures |
|---|---|
| `mc_cpu.c` | CPU cache and memory read, write and copy rates per core type (X925 cores 5-9 and 15-19, A725 cores 0-4 and 10-14, all 20 cores), and CPU vector math rates (32-bit and 16-bit float, 8-bit integer dot and matrix, bf16 matrix) |
| `mc_disk.c` | NVMe sequential write and read with the page cache bypassed; every block is checked |
| `../../src/omega_blackwell_codegen_ceil.c` + `../../tests/gpu_ceiling_test.c` | GPU 32-bit multiply-add rate, shared-memory read rate, memory store rate, and a 30 s memory read stream used in the all-at-once run. Our own kernels through the driver-free launcher |
| `session.sh` | One measurement phase inside a spark-window: `host`, `gpu` or `concurrent` (GPU, all CPU cores and NVMe streaming during the same 30 s) |
| `runner.sh` | Waits for a window, runs `host`, then `gpu` and `concurrent` once the GPU lock can be taken |

Build: `cc -O2 -march=armv9-a+sve2+i8mm+bf16+fp16 -D_GNU_SOURCE -o build/mc/mc_cpu tools/machine_ceiling/mc_cpu.c -lpthread`,
`cc -O2 -D_GNU_SOURCE -o build/mc/mc_disk tools/machine_ceiling/mc_disk.c -lpthread`, `make build/gpu_ceiling_test`.
`build/gpu_ceiling_test --host-only` runs the GPU kernels in the warp simulator (no chip). Chip runs need the GB10 lock.

Shown on real hardware: everything above runs on the DGX Spark itself; the GPU kernels were first checked in the simulator.
