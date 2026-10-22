# L403 prefill roof on the 35B: ceilings (2026-10-22)

Model shapes from ~/models/qwen3.6-35b-a3b-fp8-95a723d0 (safetensors headers): hidden 2048, 40 layers (30 DeltaNet + 10
full attention), 256 experts top 8 + 1 shared, expert 512, GDN in_proj_qkv 8192x2048 + in_proj_z 4096x2048 + a/b 32x2048
each, out_proj 2048x4096; attention q 8192x2048 (gated), k/v 512x2048, o 2048x4096, head_dim 256, 16 q heads, 2 kv heads.
Every number below: command and receipt in ceilings.txt (aien-calc eval).

| item | value | receipt |
|---|---|---|
| linear MACs per token (all GEMMs) | 2,436,710,400 | calc:74b3167a |
| attention QK+PV MACs per token, mean over an 8192 prompt | 335,585,280 | calc:ce2fa027 |
| DeltaNet recurrent MACs per token | 62,914,560 | calc:4a6de256 |
| FLOP per token at 8k | 5.670 G | calc:2175fcce |
| roof at FP8 QMMA 237.52 TFLOPS (L151 measured) | 41,888 tok/s | calc:c298b6fd |
| 15,000 tok/s target as a fraction of that roof | 0.358 | calc:efc0ab9c |
| roof at BF16 HMMA 118.81 TFLOPS | 20,953 tok/s | calc:44b8d637 |
| roof of the EXACT path (FFMA, maker 31 TFLOPS) | 5,467 tok/s | calc:6a7108ae |
| roof of the EXACT path at FFMA measured 17.7 TFLOPS | 3,121 tok/s | calc:779c057a |
| exact path: least time to read 8192 tokens | 1.498 s | calc:87960908 |
| FP8 path: least time to read 8192 tokens | 0.196 s | calc:5ed677da |
| FLOP rate a first word under 1 s at 8192 needs | 46.45 TFLOPS | calc:6abd4b1a |

## The physical fork (why two tracks)
The current path (the decode GEMV, omega_blackwell_codegen_moe.c, and codegen_gemm.c which copies it) multiplies f32
activations (scaled by 2^64) by decoded e4m3 weights with one FFMA per weight in a fixed per-lane order, then a SHFL.DOWN
tree. A tensor-core MMA sums 16 to 32 products in one fused step with its own alignment and truncation (bw_qmma_model.h:
group 32 keep 26 RZ, chip-exact) and takes bf16 or e4m3 inputs only: it cannot produce the FFMA chain's bits.
- Track E (exact): bit-identical to the current path, FFMA only. Roof 5,467 tok/s; first word at 8k needs at least 1.50 s
  (46.45 TFLOPS needed, FFMA maker peak 31). Beats the published llama.cpp 2,000-2,500 on this Spark at about 0.5 of FFMA peak.
- Track T (tensor cores): the only way to 15,000 tok/s and a first word under 1 s at 8k. Exactness kept in the form physics
  allows: prompt chunks and the token-by-token step run the SAME tensor-core arithmetic (bit-identical to each other by
  construction), plus an accuracy gate against the current path. Recorded as a decision, not asked.

## The current lever chain (from L369's receipts, sc serve/q36-prefill-v2 f6f341ef)
At chunk 128 every expert gets about 4 rows (128 x 8 / 256), and the exact GEMM streams an expert's weights once per 4 rows
(mt 4): 504.5 MB of weights per prompt token, roof 436 tok/s at 220 GB/s. Two things must change together: the kernel must
take many rows per weight pass (big row tile, M well past 1024 rows per launch), and the door's chunk must grow (2048-8192)
so an expert sees 64-256 rows. The first is this item (kernels), the second is L369's (door), on the same interfaces.
