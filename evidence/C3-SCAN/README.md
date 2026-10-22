# C3 receipt, part 1: sums, maximum, minimum and running totals of any length

Row C3 of `docs/OMEGA.md` asks for reductions and prefix sums (running totals) over every length from 1 to 2^28,
bit-exact against a host reference whose order of additions is frozen. The kernels come from Omega's own code
generator (`src/omega_gpu_scan.c`), with no CUDA. The order is written down in `src/omega_gpu_scan.h`; the reduction
order is the E1 contract (`docs/numeric/E1_REDUCTION_CONTRACT.md`).

| Log | What it checks | Result |
|---|---|---|
| 01-host-only | bad calls refused; all five kernels (and each planted fault) go through our code generator and encoder; the host reference gives exact answers on integer inputs and equals the E1 reduction reference bit for bit on mixed, signed-zero, NaN and infinite inputs; the frozen tree differs from a left-to-right sum | PASS, 133 checks |
| 02-sim | the real kernels in the warp simulator, both scheduling orders, every length 1 to 70 and the tile, block and grid-stride edges up to 270,000; three planted faults (a skipped tree step, a skipped scan step, a carry from the wrong tile) each caught | PASS, 139 checks |
| (next) chip | every n from 1 to 2048, 2^k - 1, 2^k, 2^k + 1 up to 2^28, 64 random lengths, the three faults caught on the GB10 | waiting for a chip window |

Command: `make test-gpu-scan` (host and simulator). The chip run is `build/gpu_scan_test` with no flag, under the GB10 lock.
