# CUDA++: literature-backed performance opportunities

## Scope and evidence

This is a targeted review of four primary papers, checked against the 35B model configuration, safetensors header samples, and pinned implementation source. Calculations were executed with Wolfram Language through the Wolfram tool. No new GPU benchmark was run; calculated bounds are not achieved throughput.

Code baseline: omega `serve/q36-prefill-v2@60ba305108a92bdc5d625d7cada524c563b238f9`. Existing full-model timing: sovereign-core `origin/serve/q36-prefill-v2:evidence/q36-prefill-20261021/full-time-v3.json` (receipt names the build `6127db50`). This is a checked branch baseline, not a claim about the currently deployed server.

Model configuration: 40 decoder layers, 30 Gated DeltaNet and 10 full attention; hidden width 2048; 256 routed experts, top 8 per token, expert width 512; 16 query heads and 2 KV heads, attention head dimension 256; GDN has 32 value heads, state dimensions 128 x 128. The sampled header inventory excludes MTP from decoder-layer arithmetic. All rates below are **tokens/s**, not words/s. Decimal GB and MB are used unless explicitly labelled GiB.

## 1. First priority: larger chunks AND larger expert row tiles

### What the literature contributes

ScatterMoE avoids padding and excessive copies, fusing expert linear transformations with reordering. The useful principle is to organize routed rows so weights serve more real tokens without paying for padded rows or repeated activation copies. Its published performance is not a prediction for our GB10.[4]

### What our code actually does

`src/omega_gpu_q36_graph.h:153` caps prefill at 128 tokens because 128 x 8 routed rows reaches the existing GEMM's 1024-row limit. `src/omega_gpu_q36_graph.c:914-920` explicitly prepares dense GEMMs with eight rows per pass and expert GEMMs with four rows per pass. Raising a launch limit alone is insufficient: buffer sizing, index ranges, maps, tail handling and register/shared-memory usage must be audited together.

### Wolfram calculation

Under a deliberately labelled uniform-routing model, one expert's rows in a chunk are distributed as Binomial(chunk, 8/256). Tokens choose distinct experts within each token, independently across tokens. Expected distinct experts = 256*(1-(31/32)^chunk). At chunk 128, that is **251.601146** experts, with **4.069934** rows per used expert.

Thus a 64-row tile at chunk 128 has only **6.3593% useful row slots**, not a free 16x gain. The model accounts for variable expert load and the final partial tile, rather than dividing all bytes by a nominal tile width.

| Chunk tokens | Expert tile rows | Expected raw expert-weight MB/token | Expert-weights-only roof at 220 GB/s | Useful row fraction |
|---|---:|---:|---:|---:|
| 128 | 4 | 345.696332 | 636.396686 tokens/s | 72.7975% |
| 512 | 32 | 62.920704 | 3496.464382 tokens/s | 49.9951% |
| 1024 | 64 | 31.457284 | 6993.610844 tokens/s | 50.0000% |
| 2048 | 64 | 23.071035 | 9535.766345 tokens/s | 68.1748% |

These are conditional expert-weight-only bounds. They exclude trunk, scales, attention, state, activations and launch costs; they assume weights are reread per row tile without cache reuse across tiles. Real routing is correlated and nonuniform. Replace this model with the actual per-layer routing histogram before selecting a kernel.

**Experiment:** sweep chunk size and row tile together, including ragged segmented work and fused row mapping. Record actual tile counts, bytes, padding, spills and full-model tokens/s. Keep the current reduction order on the strict-exact path. This belongs with existing L403 kernels and L369 chunking, not a duplicate worker.

## 2. DeltaNet: remove serial dependence, not a memory transfer already removed

Gated Delta Networks develops a chunkwise formulation using matrix products and a WY/triangular representation, enabling tensor-core implementations. The real opportunity is parallel work across tokens rather than merely bundling sequential steps into one launch.[2]

Our `src/omega_blackwell_codegen_gdn.c:835-839` already keeps the state in registers across the token loop, loads it once, and stores it once. Lines 1037-1048 show the sequential loop and final write. Therefore, a claimed new saving from eliminating per-token state DRAM traffic would double-count an optimization already present.

**Experiment:** implement the paper's gated chunk algebra behind a separate experimental path, compare complete recurrent state and logits over long sequences, and measure triangular-solve, matrix-product and boundary-state costs individually. Keep the existing on-chip recurrent kernel as the exact reference.

Wolfram checked a nontrivial four-step rational gated-delta recurrence against its expanded affine chunk composition: every entry difference was zero. A deliberately reversed matrix-product order was rejected. This validates that small algebraic example, not the full WY kernel or floating-point equivalence.

**Numerical contract:** mathematically exact attention or recurrence is not synonymous with bit-identical floating-point evaluation. Reordered sums and lower-precision tensor-core inputs can change outputs. Do not weaken the existing exactness gate or call chunked and single-token paths bit-identical merely because they both use tensor cores. Test chunk boundaries and final states explicitly. An accuracy-qualified alternative is a separately reported result, not a pass under the old contract.

## 3. Attention: IO-aware tiling and grouped-head reuse

FlashAttention demonstrates exact attention with IO-aware tiling that reduces traffic between GPU memory and SRAM.[1] FlashInfer describes head-group fusion: all query heads associated with a KV head share its staged data; its appendix specifically says this is most useful at short query lengths.[3]

Our model's 16 query heads / 2 KV heads gives a reuse group of 8. For BF16 KV at context 8192 across 10 attention layers, Wolfram gives:

- KV payload: **167,772,160 bytes**.
- Eight independent full rereads: **1,342,177,280 bytes**.
- Maximum difference under that no-cache-reuse assumption: **1,174,405,120 bytes**, or **5.338205 ms at 220 GB/s**.

This is a hypothesis bound, **not an explanation of the missing 7 ms**. Our current attention mapping and actual cache behavior must be inspected before claiming this saving exists. At long query lengths, query tiling itself supplies reuse, so head fusion is not automatically the best prefill schedule.

A hypothetical materialized FP32 score tensor for one 8192-token, 16-head layer is **4 GiB**. The report does not claim our code materializes that tensor. Check the allocation and access pattern before attaching the paper's avoided-traffic benefit to our implementation.

**Experiment:** compare query tiling, head-group fusion and context splitting at short and long context; separately measure KV traffic and occupancy. Preserve causal masks, gates, RoPE, normalization, accumulation requirements and recurrent/attention state contracts.

## 4. The measured baseline changes which optimization can win

The full-model 8192-token receipt reports **41.631609445 s**, or **196.773560 tokens/s**, and **4,120,537,726,976 software-accounted FP8 bytes**. This is an instruction-level traffic model, not a hardware DRAM counter.

Wolfram, using the unrounded receipt:

| Quantity | Value |
|---|---:|
| Accounted FP8 bytes per token | 502,995,328 |
| Weight-only roof assuming those bytes cross DRAM at 220 GB/s | 437.379808 tokens/s |
| Corresponding weight-streaming time | 18.729716941 s |
| Difference from observed time | 22.901892504 s |

That difference is **unattributed**, not a measured attention/DeltaNet/kernel-launch bucket. Cache reuse, bandwidth efficiency, compute-memory overlap and scheduling invalidate treating it as a literal decomposition.

As an explicitly hypothetical additive model, halving only the accounted weight time gives **1.290232x**, quartering it **1.509249x**, and deleting it entirely **1.817824x**. This is why a whole-model profile is necessary before promising a large end-to-end gain from GEMM traffic alone.

### Where the 7 ms actually goes

The code already has GPU command-list stamps: `src/omega_gpu_cmdlist.h:65,104` and `.c:210-212` describe timestamps before the first launch and after each launch's WAIT_FOR_IDLE. Reuse this mechanism; do not build a second profiler.

The discriminating run is the same fixed prompt and decode position with stamps off and on, in one exclusive measurement window. Preserve operation labels, layer index, context length, chunk/tile settings and exact binary hash. Aggregate attention, GDN, routed/shared GEMM, dense projections, head, norms and gaps. Stamp intervals include ordering/wait/report effects, so report the instrumentation slowdown and compare totals with the unstamped wall time. Until that run exists, **the 7 ms remains unassigned**.

## 5. Correction to the earlier decode inventory

Using the sampled decoder-layer safetensors payload sizes, including their listed scales, Wolfram obtained:

| Inventory component | Bytes |
|---|---:|
| Non-routed trunk, including shared experts | 1,457,550,080 |
| Eight selected routed experts per layer | 1,006,755,840 |
| BF16 output head | 1,017,118,720 |
| Total | **3,481,424,640** |

Conditional weight-streaming times: **16.818477 ms at 207 GB/s**, **15.824657 ms at 220 GB/s**, **14.689555 ms at 237 GB/s**.

This is **3.4814 GB**, not the earlier 3.537 GB estimate. Scope matters: 40 decoder layers, no MTP, no recurrent state/KV/activation traffic, and an assumption that layers share the sampled layout for their type. Runtime packing and exact-head shortcuts may change actual bytes. Treat this as a checkpoint inventory cross-check, not a measured decode minimum or a byte-for-byte audit of the live binary.

## Decisions and remaining work

1. Prioritize joint chunk/tile growth with routing-aware scheduling. The existing four-row reread is directly evidenced.
2. Keep strict-exact and changed-arithmetic experiments distinct; no silent gate relaxation.
3. Reuse the already implemented on-chip GDN state retention and GPU stamps.
4. Measure head-group reuse before crediting a KV saving. Do not assign the 7 ms from a bandwidth calculation.
5. Retain 15,000 tokens/s as an aspiration, not a demonstrated physical full-model ceiling. The prior 30,000-token/s estimate from one tensor throughput number is withdrawn as a whole-model bound. Different operators use different execution units and precisions, and transfer costs remain.
6. Paper results identify mechanisms, not wins over CUDA on this model. Same-model head-to-head measurements remain necessary.

No vendor inference engine or CUDA library was added. No live server or GPU job was changed in this review.

## Sources

[1] https://arxiv.org/html/2205.14135v2
[2] https://arxiv.org/html/2412.06464v3
[3] https://arxiv.org/html/2501.01005v2
[4] https://arxiv.org/html/2403.08245v2
