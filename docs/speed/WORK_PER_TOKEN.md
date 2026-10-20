# Work per decoded token: Qwen3.8-27B on the live Omega path (L274)

Question (Drake, 2026-10-20): "I want most of this machine to be mathematics, with as little compute as possible."
This file counts the work one decoded word costs, then lists the exact identities that remove some of it, each
confirmed or refuted with numbers. Rule from [ADR 0002](../adr/0002-mathematics-before-compute.md): a change counts
only if the tokens stay identical. Tags: OBSERVED (measured or read from code here), INFERRED (arithmetic from
observed parts), UNKNOWN.

The live path (OBSERVED, `~/.local/state/aien-q38/start-27b.sh`, `/proc/1527223/exe`): port 18091 runs
`aien-serve-http` built from sovereign-core `qwen38/serve-fast` 46134247 linked to omega `qwen38/decode-speed`
0010f7d3; FP8 e4m3 weights with 128 x 128 block scales, bf16 lm_head, greedy decode on the chip. Model shape from
`config.json`: hidden 5120, 64 layers (48 Gated DeltaNet + 16 full attention, every 4th), MLP 17408, vocabulary
248,320, attention 24 query heads and 4 KV heads of width 256 with rotary on 64 of the 256 dims, DeltaNet 16 key
heads and 48 value heads of width 128, conv kernel 4.

## 1. The count, stage by stage (per decoded word)

The graph is `src/omega_gpu_q38_graph.h` on omega `qwen38/decode-speed` (OBSERVED, header comment): one command list
per word, 596 launches (48 x 9 + 16 x 10 + 4).

### 1.1 Bytes read from DRAM and multiply-adds

| Stage | Per layer | Layers | Bytes per word | Multiply-adds per word | Source |
|---|---|---|---|---|---|
| Embedding row (host, bf16) | 10,240 B | 1 | 10,240 | 0 | `qwen38_omega.rs` step: one row converted on the CPU |
| DeltaNet in_proj qkv+z (FP8) | 16384 x 5120 | 48 | 4,026,531,840 | same | graph header `fp8 mix` |
| DeltaNet a/b rows (bf16) | 96 x 5120 x 2 B | 48 | 47,185,920 | 23,592,960 | graph header `bf16 ab` |
| DeltaNet step (conv 4 taps, gated delta rule) | state 48 x 128 x 128 f32, read + write | 48 | 301,989,888 (calc:b9b095d1) | about 151 M (4 x 128 x 128 per value head, calc:12ff4772) | gdn record; config |
| Attention q/k/v+gate projection (FP8) | 14336 x 5120 | 16 | 1,174,405,120 | same | graph header `fp8 mix` |
| QK-norm, rotary (64 of 256 dims), sigmoid gate | 28 heads x 256 | 16 | negligible | under 0.5 M | graph header |
| Attention over the KV cache | 2 x 4 x 256 x 2 B per earlier word | 16 | 65,536 per earlier word (calc:174870b2) | 196,608 per earlier word (calc:cad2b052) | attn256 record |
| Output projection (FP8) | 5120 x 6144 | 64 | 2,013,265,920 | same | `fp8 o` |
| MLP gate+up (FP8) | 34816 x 5120 | 64 | 11,408,506,880 | same | `fp8 gu` |
| MLP down (FP8) | 5120 x 17408 | 64 | 5,704,253,440 | same | `fp8 down` |
| RMSNorm + residual add (fused) | 5120 | 129 | kilobytes | about 1.3 M | `ADD|NORM` glue |
| FP8 block scales (f32 on the device) | 1 per 128 x 128 | all FP8 | 5,939,200 | folded into one FMA per block per lane | `omega_blackwell_codegen_moe.h` |
| lm_head (bf16) | 248320 x 5120 | 1 | 2,542,796,800 | 1,271,398,400 | `bf16 lm_head` |
| Argmax over 248,320 logits | | 1 | 993,280 B of logits, written once and read once | 0 (compares) | `ARGMAX x2` |
| Sampling (greedy) | | | 0 | 0 | `generation.rs:155`: `wants_logits()` is `temperature > 0.001`; greedy never reads logits back, no softmax anywhere |

Totals (INFERRED from the rows above, exact integers):
- FP8 weights 24,326,963,200 B (calc:741df630, cross-check calc:e19bb6d3) plus bf16 a/b and lm_head 2,589,982,720 B
  (calc:81016d51): **26,916,945,920 weight bytes per word**, 9.45 percent of it the lm_head (calc:2a7b6449).
- **25,621,954,560 weight multiply-adds per word** (calc:dc47e9b6), plus about 0.15 G in the DeltaNet recurrence and
  196,608 per earlier word in attention. At 7.63 words/s that is 0.195 T multiply-adds per second (calc:04d2bb9a): the
  chip's arithmetic is mostly idle. The word is paid in bytes.
- Hardware ceiling at 273 GB/s: **10.14 words/s** (calc:0c1b9334). The live server's 7.63 is 0.75 of it.

### 1.2 Launches, syncs, host

| Item | Per word | Source |
|---|---|---|
| Kernel launches | 596, in one command list | graph header (OBSERVED) |
| Host to chip submits | 1 command list (doorbell store) | L123 audit section 4.3 |
| Completion waits | 1, a sleep-poll on a marker word | L123 audit section 4.3 (`m16_native.c`, 50 us sleep) |
| Syscalls and context switches | see section 2 | measured on my own copy of the server |

## 2. Measured on my own copy of the live engine

My own sha-checked copy of the live binary (`~/workspace/l274-work/srv`, `sha256sum -c SHA256SUMS` OK) on port 18491;
18091 is never touched. Run `~/workspace/l274-work/runs/serve-20261020T182916Z` (`measure-serve.sh`, exit 0).

### 2.1 Syscalls in one 64-word request (OBSERVED)

Server started under `strace -f -ttt` (Yama ptrace_scope 1 forbids attaching later), one greedy request with
max_tokens 64, syscalls counted between the timestamps taken just before and after the request (`c-t0`, `c-t1`):

| Syscall | Count | Share |
|---|---|---|
| clock_nanosleep | 111,510 | |
| sched_yield | 84,519 | the two together 0.9456 (calc:19a3049f) |
| futex | 11,072 | |
| everything else (write, epoll, accept, ...) | 212 | |
| **total** | **207,313** over 23.23 s, 8,924 per second (calc:1d9ee069) | 3,239 per decoded word (calc:b2f13ad1) |

Command: `awk -v a=$(cat c-t0) -v b=$(cat c-t1) '$2>=a && $2<=b' strace.log`, 23 threads. The request was
prefill 4,147 ms plus 64 words in 19,025 ms (3.36 words/s, slowed by strace and by other agents' chip and build work).
What it means (INFERRED): 95 percent of the host's syscalls are the completion wait sleeping and yielding while the
chip works. They scale with wait time, not with work: the 81k per 64 words claimed in the brief fits a faster run of the
same loop (64 words at 7.63 words/s times 8,924 per second is 74,858, calc:4b1c40d1). This is identity 9 (the wait as one event, owned by
L133 / L272).

Context switches, all threads, around one 64-word request without strace: **210,926** (`/proc/PID/task/*/status`,
voluntary plus nonvoluntary). Same run, same contention.

### 2.2 Speed and joules per word (PENDING a quiet run)

The first run's speed numbers (3.46 words/s, graph chip 291 ms per word) are not publishable: other agents' chip
probes and builds ran at the same time (the server log shows 107 s waiting on the chip lock). Phase A (UPS idle 60 s,
then a 256-word request under `quiet-measure`) failed in that run (exported shell functions do not reach
`quiet-measure`); `measure-quiet.sh` runs it as a script and retries the load when the chip refuses a channel.

## 3. Identities evaluated

Ranked by bytes removed per word, because decode is memory bound.

| # | Identity | Exact? | Work it removes per word | Verdict | Evidence |
|---|---|---|---|---|---|
| 1 | Lossless code for the weights (exponent field Huffman-coded per tensor, sign and mantissa raw) | yes, bit-identical bytes into the same arithmetic | 4.97 GB (calc:12590f66), 1.226 x fewer weight bytes (calc:1f4957aa); ceiling 10.14 -> 12.44 words/s (calc:8a70eba0) | **CONFIRMED, top identity, built (section 4)** | `wpt_entropy` |
| 2 | Batch the lm_head (and every weight) over a draft block or several requests | yes | weights read once for N words | CONFIRMED but already owned: speculative verify (L126, L128, L190), batching (L129) | L123 audit levers G, H, J |
| 3 | Exact argmax over the vocabulary with norm bounds (skip rows whose upper bound is below a proven lower bound) | yes (sound bounds with an fp32 rounding margin) | at most the 2.54 GB head; measured: nothing | **REFUTED**: on all 48 golden positions every one of the 248,320 rows survives every bound tried | section 3.1 |
| 4 | Skip exactly-zero products | yes | 233,217 zero weights of 24.33 G (0.001 percent); activations are f32 (`omega_gpu_fp8_api.h:105`) and the SwiGLU output is never exactly 0 in practice | **REFUTED** (nothing to skip at sector granularity) | `wpt_entropy` zeros column |
| 5 | Fold the RMSNorm scale into the next weight matrix | only in real arithmetic: folding into FP8 weights re-quantizes them (different bytes, NOT IDENTICAL) | one multiply per element of a 5120 vector per norm, already fused into the ADD|NORM kernel with (1 + w) pre-folded | **REFUTED** as a saving: it removes no pass (the norm is fused) and no bytes | graph header |
| 6 | Fold QK-norm scale and rotary | QK-norm divides by the vector's own norm (not linear); rotary mixes dimension pairs, so a diagonal scale commutes only where pair scales match | under 0.5 M multiply-adds of 25.6 G | **REFUTED** as a saving (0.002 percent) | config |
| 7 | Greedy sampling as argmax with no softmax | yes | the softmax over 248,320 | **ALREADY DONE** in the live path | `generation.rs:155`, `ARGMAX x2` |
| 8 | Integer arithmetic for the FP8 path | the current decode is already integer bit tricks to f32 (exact, `omega_blackwell_codegen_moe.h` numeric scheme) | arithmetic is not the limit (section 1.1) | **NO GAIN** while memory bound | |
| 9 | Closed form for the completion wait (an event or a predicted wake time instead of a sleep-poll) | yes (never misses a completion) | the host syscalls and context switches of section 2 | CONFIRMED as work removed, owned by L133 (SPEED-9) / L272 (launch path) | section 2 |

### 3.1 The lm_head bound study (refuted, with numbers)

Tool `tools/wpt_headbound.c`. The reference run stores only the f32 logits L = W h, so h (the final normed hidden
state) is recovered by least squares on 8,192 head rows and checked: all 248,320 logits recomputed from h match the
stored ones to a max error of 7.6e-6 to 1.4e-5, and the argmax matches at 48 of 48 positions (OBSERVED,
`~/workspace/l274-work/headbound-golden.tsv`). Bounds tried, each with an fp32 rounding margin:
- whole-row Cauchy-Schwarz |w_i| |h|;
- adaptive: read the m 16-wide column blocks carrying most of |h|^2 (m = 8, 16, 32, 64, 128 of 320) for every row,
  bound the rest by sqrt(|w_i|^2 - |w_iS|^2) |h_rest|, or per 256-wide super-block;
  lower bound = the best exact logit among the 16 rows with the largest partial.

Result: survivors = 248,320 of 248,320 at every position for every bound. Why: |h| is 88.7 to 141.2, the top
logit only 12.0 to 44.3, so the winner's cosine with h is small (top logit over |h| |w_top|); a bound that assumes cosine 1 on the unread
part can never beat it. h is not concentrated enough (the top 256 of 5120 coordinates carry 0.29 to 0.68 of |h|^2).

## 4. The top identity, built: FX1 lossless weights

PENDING: codec round trip over all 24.33 G FP8 weights, exact bytes of the GPU layout, chip GEMV.
