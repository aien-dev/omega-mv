# MXFP4 weight tiles in Omega (qwen38/w4-gemv, cascade/w4-gemv, 2026-10-19)

One storage contract, shared by the host converter, the host oracle, the decode GEMV kernel and any later multi-row
kernel (the cascade tree-verify GEMM on cascade/tree-verify-gemm). Source of truth for the macros: `src/omega_blackwell_codegen_w4.h`.
This note is the plain-English and byte-level description a second kernel needs to read the same tensors unchanged.

## 1. What a W4 tensor is

A projection `W` of shape `[n rows][k columns]` (y = W x, k along the input, the FP8 tensors' orientation) becomes:

| part | shape | bytes | meaning |
|---|---|---|---|
| nibbles | `[n][k/2]` bytes, row-major | n * k / 2 | weight `(r, c)` is e2m1 code `OMEGA_W4_NIBBLE(row_bytes, c)` = low nibble of byte `c/2` when `c` is even, high nibble when `c` is odd |
| scales | `[n][k/32]` bytes, row-major | n * k / 32 | group `g` of row `r` (columns `32 g .. 32 g + 31`) is multiplied by `2^(E - 127)`, `E` the byte at `r * (k/32) + g` (E8M0, OCP MX) |

`value(r, c) = e2m1(nibble) * 2^(E[r][c / 32] - 127)`.

e2m1 code: bit 3 sign, bits 2..1 exponent (bias 1), bit 0 mantissa. Magnitudes by code 0..7: `0, 0.5, 1, 1.5, 2, 3, 4, 6`.
Codes 1 and 9 (0.5 and -0.5) are the format's subnormals; the kernel decodes them exactly (no flush to zero).

Constraints the upload enforces (`omega_gpu_w4_tensor_upload`): `k % 512 == 0`, `k <= 32768`, every `E <= 192`
(`OMEGA_W4_E_MAX`; the GEMV's folded scale `2^(E - 65)` must stay a normal f32). The converter from the checkpoint
(`omega_gpu_w4_from_fp8`) produces `E` in a narrow band around 127 for Qwen3.8-27B, so the cap never fires on real
weights; it is there so a corrupt scale byte is refused at upload instead of becoming NaN on the chip.

## 2. Device layout: one buffer per tensor

Since ae900541 a tensor is one device allocation (the 1022 buffer-slot cap of the session made two per tensor fatal
at 64 layers):

```
w_va                      : nibbles, n * k / 2 bytes
s_va = w_va + round_up(n * k / 2, 4096) : scales, n * k / 32 bytes
```

`omega_gpu_w4_tensor_entry(t, &w_va, &s_va)` gives both addresses. The decode graph writes them into the launch's
group table as `{ w_va u64, s_va u64 }` (16 bytes per group, the FP8 kernel's table shape), so a kernel that already
takes an FP8 group table can take a W4 table with the same plumbing: the second word is a scale base instead of the
FP8 `[n/128][k/128]` bf16 block-scale base.

Row `r` nibbles start at `w_va + r * (k / 2)`; row `r` scales at `s_va + r * (k / 32)`. Rows are contiguous, so a
tile of `R` consecutive rows by one 32-column group is `R` runs of 16 bytes (stride `k/2`) plus `R` scale bytes
(stride `k/32`). There is no padding inside a row and no interleaving between rows.

## 3. The tile a 32-wide k-step sees

The natural tile for both the GEMV and a tensor-core GEMM is **R rows x 32 columns**:

- 16 bytes of nibbles per row (one 128-bit load per row per lane in the GEMV at `lane_bytes 16`: lane L reads bytes
  `L * 16 .. L * 16 + 15` of each of its rows per K step, which is exactly one scale group, so one scale byte per lane per row per step).
- 1 scale byte per row. Four consecutive groups of a row share one 32-bit word; the GEMV reads the word and extracts byte `g & 3`
  (mutant 4 in the tests swaps that byte and must be caught).
- The matching 32 inputs `x[32 g .. 32 g + 31]`.

Per 32 weights this is 17 bytes against 32 for FP8 (plus FP8's amortised block scale), the 1.88x that sets the
on-paper decode gain (MEASURED in-graph: W4 gate_up 963 us vs FP8 1682 at 2 layers, ab-fp8head-4bit handoff).

## 4. Reading W4 tiles from a tensor-core tree-verify GEMM

For N activation rows (the candidate tree) against the same weights, the weight read is identical; only the
arithmetic per tile changes. Two exact routes, both using the layout above unchanged:

1. **e2m1 to e4m3 is exact.** Every e2m1 magnitude (0, 0.5, 1, 1.5, 2, 3, 4, 6) is an e4m3 normal (0.5 = 2^-1 is
   normal in e4m3, min normal 2^-6). So a tile's 16 bytes of nibbles expand to 32 bytes of e4m3 with a 16-entry
   table (or `prmt`-style byte shuffles), and an FP8 tensor-core MMA (e4m3 `m16n8k32`, k = 32 per instruction; Omega's
   tensor-core codegen is `omega_gpu_tcgemm_api`; the CUDA-core `omega_gpu_fp8gemm_api` is bit-identical to the GEMV and
   is not the fast path) runs on them. The E8M0 scale is applied per row per 32-wide k-step on the accumulator:
   `acc[r][*] += 2^(E[r][g] - 127) * mma_partial[r][*]`, which is exact because an MMA instruction's k extent equals the
   scale group (32). The FP8 GEMM's bf16 block scale stage becomes a per-row per-step power-of-two scale instead
   of a per-128x128 block scale; same place in the pipeline, different source. Do NOT fold the scale into the e4m3
   bytes: `e4m3 * 2^(E - 127)` is not representable in general.
2. **Native MX MMA.** sm_100 class tensor cores have the `kind::mxf4` / `mxf8f6f4` MMA with E8M0 scale factors in
   exactly this grouping (32 along k, one scale per row per group). If the Blackwell codegen gains that instruction,
   the nibbles and scales above are the operand layout it expects after the usual swizzle into shared memory; nothing
   about the global layout needs to change. Not attempted on this branch (the decode GEMV decodes nibbles on CUDA cores,
   4 int ops + FFMA per weight, same as the FP8 GEMV).

Either way the kernel's K loop walks `g = 0 .. k/32 - 1`, loads the R x 16-byte nibble tile and the R scale bytes for
group `g`, and the x tile `x[32 g ..]` for all N rows. The decode GEMV's `x' = x * 2^64` fold is private to the GEMV's
f32 bit trick (section 5) and must NOT be applied to inputs of a tensor-core path; feed the GEMM plain activations.

## 5. How the decode GEMV decodes a nibble (why the fold exists)

f32 bits `((n & 7) << 22) | ((n & 8) << 28)` equal `e2m1(n) * 2^-126` exactly for all 16 codes (checked exhaustively
in `tests/gpu_w4_api_test.c --host-only`). The input is pre-scaled `x' = x * 2^64` on the host (exact, the FP8 kernel's
fold), so every product is `q x 2^-62`, and a lane's dot over its chunk is multiplied once by `S'` with f32 bits
`(E + 62) << 23 = 2^(E - 65)`: `q x 2^-62 * 2^(E - 65) = q x 2^(E - 127)`. No table lookup, no shared memory, 4 integer
ops plus one FFMA per weight.

## 6. Checks that pin the contract

- `tests/gpu_w4_api_test.c`: host (16 codes, RNE rounding to e2m1, converter round trip), warp simulator (parity over
  shapes and knobs, mutants 1..6 red), chip (parity against the host oracle at every 27B shape incl. the lm_head,
  one-hot codes incl. the subnormals, mutants 1, 2, 4, 5 red, upload refuses E > 192), then W4 vs FP8 timing in one session.
- `tests/gpu_q38_graph_test.c --w4 --w4-head`: the whole decode graph on MXFP4 against the one-call-at-a-time
  baseline, with the FP8 control (layer 0 back on FP8 must fail), GPU-stamped timeline per launch kind.
- `tools/run_gpu_w4_chip.sh EVID`: the gate, verdict line `GPU_W4_CHIP: PASS|FAIL`.

A tree-verify kernel that reads these tensors should reuse `omega_w4_gemv_oracle` (host, double) row by row as its
reference, and add the three mutants that matter for a multi-row reader: wrong scale byte, swapped nibble order,
scale pointer not advancing with the k-step.
