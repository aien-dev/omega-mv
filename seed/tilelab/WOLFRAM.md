# TileLab: independent Wolfram references

Every number in `expected.txt` was reproduced by Wolfram Language. The program never reads these numbers.
The evaluator ran on the hosted service `https://agenttools.wolfram.com/mcp` (server "Wolfram" 2026.09.15).
On 2026-10-28 it was called over direct MCP HTTP, because this session's `mcp.json` does not mount it.

## Planner (calibrated ranking, not a time prediction)

### Why the model changed

The first model counted CTA-tile reuse as if the kernel staged tiles in shared memory. CUDApp's `omega_gpu_gemm`
has no shared memory (`src/omega_gpu_gemm.h`): each warp loads its own 16rt x 8ct operands and `band` orders CTAs in
column bands so packed B stays in L2. A GB10 sweep (35 configurations x 4 shapes, 9 rounds, 0 wrong elements,
3/3 chip mutants caught per run; CUDApp `evidence/TILELAB-20261028/sweep/`) measured what the chip does instead.

### The fit

Model, with `BM = 16 rt wm`, `BN = 8 ct wn`, `Kp = 32 ceil(K/32)`, `mt = ceil(M/BM)`, `nt = ceil(N/BN)`:
`l2 = 2 Kp (M nt + N mt)`; `dram = 2 M Kp ceil(nt/band) + 2 Kp N` when `band >= 2` and `2 Kp min(N, band BN) <= cap`,
else `2 M Kp nt + 2 Kp N mt`; `cost_ns = l2/c + 2 M N Kp/p + dram/bw`.
`FindFit` of `Log[cost]` to `Log[p50]` on 96 sweep medians (the three shapes above 1024^3; 1024^3 timings are
quantized at 0.204/0.306 ms and carry no ranking signal), with `cap` chosen by grid search over
{8, 12, 16, 20, 24, 28, 32, 40, 48, 64} MiB: log-RMSE 0.281 (8-12 MiB), 0.262 (16-28), 0.253 (32-48), 0.260 (64).
`cap = 32 MiB` (the smallest best), giving `p = 37.1660364`, `bw = 1890.5139368`, `c = 1827.5440363` (bytes/ns, flop/ns).
These are fitted ranking constants, not physical bandwidths. An occupancy term `rw/(rw+h)` fitted `h -> 0` and was dropped.

Leave-one-shape-out (refit on two shapes, `cap` re-chosen on those two, rank the third). Regret is the best
measured p50 among the model's top k plus the CUDApp default, divided by the best of all 32 configurations:

| held-out shape | default/best | top-1 + default | top-2 + default | top-3 + default |
|---|---|---|---|---|
| 4096^3 | 1.0340 | 1.0340 | 1.0000 | 1.0000 |
| 2048x8192x2048 | 1.0184 | 1.0184 | 1.0184 | 1.0184 |
| 8192^3 | 1.3312 | 1.0000 | 1.0000 | 1.0000 |

The same fit without the L2 budget reached top-2 + default regrets 1.0000, 1.0184 and 1.0000, but its top-1 at
8192^3 was 1.3248 of the best. Parameter-free ranking (dram bytes, then l2 bytes) reached top-3 regrets of
1.159, 1.074 and 1.325. The 8192^3 default cell is noisy: one cell's nine samples ran from 30.75 to 44.86 ms.
So the planner emits its top 2 and the default as `measure` lines; the chip picks.

### Program output reproduced

The query rebuilds the 405-configuration search from the definitions above (`cfg`, `regs = 4(4 rt + 2 ct + rt ct)`,
budget 100, first minimum on ties) with the program's constants (`1827.5440`, `37166.0364`, `1890.5139`, cap 33554432).

| shape | Wolfram pick | l2_bytes | dram_bytes | floor(cost_ns) |
|---|---|---|---|---|
| 1024^3 | rt2 ct4 wm4 wn4 band 8 | 33554432 | 4194304 | 78359 |
| 4096^3 | rt2 ct4 wm4 wn4 band 32 | 2147483648 | 67108864 | 4908534 |
| 2048x8192x2048 | rt2 ct4 wm4 wn4 band 32 | 1073741824 | 50331648 | 2463141 |
| 100^3 | rt2 ct4 wm4 wn4 band 0 | 51200 | 51200 | 123 |
| 8192^3 | rt2 ct4 wm4 wn4 band 16 | 17179869184 | 671088640 | 39339273 |

The top-2 `measure` lines match Wolfram's `Ordering[cost, 2]` for these shapes and for 3072^3, 4096x1024x4096 and
1024x8192x8192. With a register budget of 20, zero configurations fit, so the program refuses with rc 2.

## Streaming softmax (m, s, p)

The fixture has 32 scores: `x_i = (mod(37 i + 11, 64) - 32)/4` and `v_i = (mod(53 i + 7, 41) - 20)/8`, for i = 0..31.
It is split into four tiles of 8. The merge rule is
`(m1,s1,p1) + (m2,s2,p2) = (m, s1 e^(m1-m) + s2 e^(m2-m), p1 e^(m1-m) + p2 e^(m2-m))` with `m = max(m1, m2)`.

- Exact identity check: `FullSimplify[p/s - direct] === 0` is True for the sequential fold and for the balanced tree.
  The two folds give identical (s, p). The check used exact rationals and `Exp`.
- `Floor[(direct + 100) 10^9]` gives 99264610583, which is the program's `direct_plus100_e9`. The direct value is -0.735389416339536.
- Mutant 1 averages tile-local normalisations: `Floor[|rel err| 10^12]` gives 158400527060, the program's `one_err_ppt`.
- Mutant 2 merges without the rescale: the same measure gives 108396641107, the program's `seq_err_ppt` and `tree_err_ppt`.
- Mutant 3 uses the wrong tree order, so only tiles 0 and 1 reach the result: the same measure gives 1235283119784, the program's `tree_err_ppt`.

The tolerance is 2e-6 relative (2000000 ppt), and it applies to the f64 host evaluation only.
Exact real associativity does not make float merges bitwise associative.
The GB10 tolerance is a separate figure, owned by CUDApp#153.

### Held-out chip check (not used in the fit)

GB10, 29 configurations per shape, 9 rounds (CUDApp `evidence/TILELAB-20261028/holdout/`). The best of the planner's
three `measure` lines equalled the best of all 29 on every shape: 3072^3 (2.043 ms, default 1.099x slower),
4096x1024x4096 (0.918 ms, default 1.225x), 1024x8192x8192 (3.581 ms, default 1.039x). One session, one chip.
