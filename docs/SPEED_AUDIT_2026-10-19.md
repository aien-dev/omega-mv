# Speed audit of the whole setup, 2026-10-19 (ledger L123)

Question (Drake, 2026-10-19): "Review all of the foundational stuff, principles, and mathematics of our entire setup and
find ways to optimize and make it faster. That's my whole goal: faster."

Scope: the Qwen3.8-27B model generating words on our own Omega GPU engine on the DGX Spark (GB10). Speed is judged
as a share of what the hardware physically allows. Every number below carries its source file or a `calc:` receipt
from aien-calc. Where nothing was measured, the text says "no receipt" and names the measurement that would give one.
Nothing was built or measured for this audit; it reads and cites.

## Summary for Drake

- **Where we are.** One person talking to the 27B gets **13.82 words per second** with the weights stored in 4 bits
  (lab, calc:da33c438), or **8.13** with today's 8-bit weights (lab, calc:49bc701e). The live server gives 7.63 on
  8 bits (calc:64f74f90). The fastest code is on branches, not yet on main.
- **What physics allows.** Writing one word means reading every weight once from memory, and the memory moves at most
  273 GB per second. That caps one-word-at-a-time writing at **10.15 words/s for 8-bit weights** (calc:9822c2ba) and
  **20.0 for 4-bit** (calc:07f2175b). The 8-bit engine already runs at 0.80 of its cap (calc:f4c2e3be), the 4-bit
  engine at 0.69 (calc:8e27f3c9).
- **The three biggest levers, with their arithmetic.**
  1. **Guess several words, check them in one read** (speculative decoding). Our own drafters already land 4.15 words
     per check on code (MTP head, 8 guesses) and 6.0 on code, 2.29 on story text (DFlash). Our new checking routine
     handles 32 guesses for about the price of 1 (0.99 to 1.04 ms against 0.97 ms). On paper that is 26.6 words/s at
     8 bits (calc:cad55c6e), 42.6 at 4 bits (calc:16902929), and 68.7 on code with DFlash at 4 bits (calc:a497d66e).
     The answer stays word for word what the big model would have written alone.
  2. **4 bits as the default.** Half the bytes per word: measured 1.67 times faster than 8 bits in the same session
     (calc:cfc16fae). The cost is accuracy: about 1 word in 8 differs from the 8-bit model on code and technical text.
  3. **Serve several people in the same read.** The same weight read can feed 32 conversations for about 7 percent
     more time: about 30 times the total words per second on the weight part (calc:7acea7a9). Each single person goes
     no faster.
- **What not to expect.** No amount of compiler, language or kernel work makes one-word-at-a-time writing beat 10.15
  words/s on 8-bit weights or 20.0 on 4-bit. Above that, only two doors exist: fewer bytes per word, or more words per
  read. The founding principles of Omega (safety rules, receipts, identity encoding) cost nothing per word today,
  because none of them runs inside the word loop (section 6), so removing them would buy no speed.

## 1. Terms used once

- **Word / token**: the unit the model writes, one step at a time ("words per second" = tokens per second).
- **Decode**: writing the answer, one word per step. **Prefill**: reading the prompt, many words at once. Decode is
  limited by memory speed, prefill by arithmetic speed. This audit is about decode unless it says otherwise.
- **Weights**: the model's stored numbers, 26.89 GB at 8 bits for this model.
- **Bandwidth**: bytes per second memory can deliver. **Ceiling**: bandwidth divided by bytes read per word.
- **Ceiling fraction**: measured words/s divided by the ceiling at the same number format.
- **KV cache**: the attention layers' memory of earlier words, read again for every new word.
- **DeltaNet state**: the fixed-size running memory of the model's 48 recurrent layers.
- **Kernel launch**: one job handed to the GPU. **Command list**: a recorded list of launches sent in one go.
- **Channel**: one queue of GPU work; work in one channel runs in order.
- **Tau**: words accepted per check pass in speculative decoding, counting the one free word.
- **ON PAPER**: arithmetic from measured parts, not a measured result.

## 2. The ceilings of this box

| Quantity | Value | Source |
|---|---|---|
| Unified memory, capacity | 128 GB LPDDR5x, one pool for CPU and GPU; kernel reports MemTotal 127,598,528 kB | spark-hardware-manual skill (vendor page); `/proc/meminfo` on the Spark |
| Memory bandwidth, spec | 273 GB/s | sovereign-core alpha/ceiling-and-model `docs/qwen38/CEILING.md:29` |
| Weight stream, best measured whole model | 218.6 GB/s (0.80 of spec) | `CEILING.md:32-35`, calc:e726ff77 |
| GPU streaming probe (tensor-core path) | 195 to 208 GB/s | gpu_tc_peak_test, handoff 2026-10-19-cascade-tree-verify-gemm.md |
| CPU memcpy, loaded box | 65 to 98 GB/s at 8 threads; 18 GB/s one thread | M1 passport block 4, `~/yardsticks/aien-model/block4.csv` |
| FP8 tensor arithmetic, vendor dense | 207.7 TFLOPS | handoff 2026-10-19-moonshot-d-long-context.md |
| FP4 tensor arithmetic, vendor | 1 PFLOP sparse | DGX Spark hardware page, docs-read record quoted in handoffs |
| Tensor arithmetic, measured by Omega | 115 TFLOPS micro-test (2026-10-13); 21 to 31 TFLOPS in real prompt-reading matrix routines | moonshot E section 3; handoff 2026-10-19-cascade-prefill.md |
| bf16 arithmetic, measured | 96 TFLOPS | moonshot D |
| INT8 arithmetic | no source in our repos | |
| GPU size | 48 SMs (compute units) | cascade-tree-verify-gemm handoff |
| NVMe, model-free disk round trip | load 0.64 to 1.95 GB/s, spill 0.38 to 0.69 GB/s | sovereign-core a62caae1, handoff 2026-10-19-cascade-document-memory.md |
| CPU to GPU path | same physical memory; no copy needed, only cache rules (GPU-cached vs uncached pages) | spark-hardware-manual skill section 4 |
| Launch path | driver-free: CPU writes the queue entry, then one 32-bit doorbell store; the driver is used only for setup | handoff 2026-10-19-chain-to-gpu.md (`nvrm.c:745`), physics doc/driver-floor `docs/numeric/DRIVER_FLOOR.md` |

Note on the brief's figure "212 GB/s": it was not found in the repos and handoffs searched (sovereign-core alpha
branches, omega, handoffs). This audit uses 273 GB/s (spec) as the ceiling and 218.6 GB/s as the best measured stream.

## 3. Where the 27B decode is today (cited, not re-measured)

| Setup | ms per word | words/s | ceiling | fraction | Source |
|---|---|---|---|---|---|
| 8-bit weights, 16-bit head (lab, final speed code) | 123.04 | 8.13 | 10.15 | 0.801 | handoff 2026-10-19-qwen38-decode-speed.md, calc:aa6036e9, calc:49bc701e, calc:587c7c9d |
| 8-bit weights, 8-bit head (lab) | 118.31 to 120.68 | 8.29 to 8.45 | 10.64 | 0.779 | handoff 2026-10-19-ab-fp8head-4bit.md, calc:1c19cda2, calc:9c639a0c, calc:00b3665f |
| 4-bit weights, 8-bit head, W (lab) | 75.33 | 13.28 | 19.17 | 0.693 | same, calc:c5eab84e, calc:a3d7bb22 |
| 4-bit weights, 4-bit head, WW (lab) | 72.37 | 13.82 | 20.01 | 0.691 | same, calc:da33c438, calc:58f08c18 |
| Live server, 8-bit | 131 | 7.63 | 10.15 | 0.752 | handoff 2026-10-19-alpha-serve-path.md, calc:64f74f90 |
| QWEN38-SPEED gate (L068) | not yet | | | | gate built and run, real speed number still blocked by memory (ledger L068) |

All lab numbers were taken with other 27B servers sharing the chip; the ranking between setups is protected by
rotated rounds, the absolute numbers are "under load". Energy per word at the wall: **no receipt** for decode yet
(L109 measures T per joule; L113 adds J per token to the baseline receipt).

Where the branches are: the 8-bit speed work is omega `qwen38/decode-speed` (0010f7d3), the 4-bit work omega
`qwen38/w4-gemv` (dd5e89ff) and sovereign-core `qwen38/w4-decode`. Neither the 27B decode graph
(`src/omega_gpu_q38_graph.c`) nor the 4-bit routine is on omega main (checked with `git branch -r --contains`).

## 4. One generated word, taken apart

Model shape (moonshot B section 2, from the model config): hidden 5120, MLP 17408, vocabulary 248,320, 64 layers
(48 DeltaNet, 16 full attention with 4 KV heads of width 256).

### 4.1 Bytes read per word

| Part | Bytes per word, 8-bit | Bytes per word, 4-bit (WW) | Source |
|---|---|---|---|
| MLP gate/up/down, 64 layers | 17.11 GB | about half | moonshot B table |
| DeltaNet projections, 48 layers | 5.54 GB | about half | same |
| Attention projections, 16 layers | 1.68 GB | about half | same |
| Word-scoring head (lm_head) | 2.54 GB (bf16) | 0.675 GB at 4.25 bits (calc:2853b281) | same |
| **Weights total** | **26.89 GB** | **13.65 GB** | `CEILING.md:34`; moonshot B row 3 |
| DeltaNet state, read and written | 0.30 GB; measured 29 us per layer = 1.39 ms per word (calc:a34b2b00) | same | moonshot B; decode-speed handoff |
| KV cache, per earlier word | 65,536 bytes (calc:174870b2) | same | 16 x 4 x 256 x 2 x 2 bytes |
| KV cache at 8k / 32k / 128k context | 0.54 / 2.15 (calc:8ab75f35) / 8.59 GB (calc:ce70f1a3) | same | |
| Activations | kilobytes per layer; negligible against the weights | | hidden 5120 values per layer step |

At 32k context the KV read is 15.7 percent of the 4-bit weights (calc:dacc0d54) and pulls the 4-bit ceiling from 20.0
to 17.28 words/s (calc:b28b2816). At 8k it is 4 percent. At 128k it is two thirds of the 4-bit weights.

### 4.2 Time per word, 4-bit WW (the fastest measured setup)

| Piece | Time | Source |
|---|---|---|
| Weight reading | about 67 ms of 71.68 ms GPU timeline | ab-fp8head-4bit handoff (calc:b9134d23) |
| Other GPU work (norms, attention, DeltaNet, glue kernels, argmax) | about 5 ms, 6.91 percent of the word (calc:bdf910fd) | same |
| Host side between GPU words (submit, wait, read token) | about 0.69 ms, 0.95 percent (calc:cba6f39d, calc:2fc24dbd) | difference of two measurements of the same setup (72.37 median vs 71.68 timeline), not a host profile |
| If the 4-bit weight read ran at 273 GB/s | 54.82 ms, 18.24 words/s; 1.32 times today (calc:f8c2fc5f) | calc:b49da385, calc:b175589a |

The 4-bit reading routine averages 209 GB/s (calc:832d43a7) against 239 GB/s for the 8-bit one (calc:106355c5).
The parts add up: 67 + 5 + 0.7 = 72.7 ms against 72.37 measured, so nothing large is hidden.

### 4.3 Launches, channel, doorbell, host

- **Launches per word: 596** for the 27B (48 DeltaNet layers x 9, 16 attention layers x 10, plus 4), all in one
  command list per word (omega `qwen38/decode-speed` `src/omega_gpu_q38_graph.h:24`). The 30B model's graph is 436
  (`src/omega_gpu_decode_graph.h:25`). If every non-weight millisecond were launch overhead, that would be at most
  8.39 us per launch (calc:0c35680b); it is an upper bound, not a measured gap.
- **One channel, one queue**: calls from several threads wait on one lock (`src/omega_gpu_session.h:29-33`). For one
  person this costs nothing measurable: the GPU is busy 71.68 of 72.37 ms. It binds when several requests or prompt
  reading and writing should overlap (section 5, rows L and M).
- **Doorbell**: launching is already driver-free (one store into a mapped register). The driver (8 RM calls plus the
  UVM module) is used only at setup (DRIVER_FLOOR.md). Per word the driver costs nothing.
- **Completion**: the CPU polls a marker word with a 50 us sleep (`m16_native.c`, chain-to-gpu handoff). Worst case
  50 us per word = 0.069 percent of a 4-bit word (calc:96db1b0a).
- **Choosing the next word**: greedy argmax already runs on the GPU (two launches at the end of the graph). Reading
  all scores back would be 993 KB per word at 4 bytes each (calc:486308fb); today only the token id comes back.
- **Host C per word in the server** (detokenize, HTTP streaming, the mask, scheduling): **no receipt**. The lab-to-server
  gap (8.13 vs 7.63 words/s on the same weights) mixes host work with chip load. L097 is building the per-word
  host table; SPEED-9 (L133) measures and cuts it.
- **Prefix reuse**: affects time to the first word, not words per second. Measured: a second question on a 30k
  document answered from the saved state in 0.71 s, word for word the same as a cold start (moonshot D, serve
  handoff). The restore path is `aien-inference-abi` generation.rs.

## 5. Every lever

"Gain" is the arithmetic ceiling gain if nothing else binds. Levers aimed at the same bottleneck do not multiply.
Speculative rows are stated per accepted word, not per byte.

| # | Lever | Arithmetic of the gain | Cost | What exists | Measurement that decides |
|---|---|---|---|---|---|
| A | Weight bits 8 -> 4 | bytes x0.51 (26.89 -> 13.65 GB); ceiling 10.15 -> 20.0; measured 1.67x (calc:cfc16fae) | 1 word in 8 differs on code/technical text; load 93-161 s from conversion at start; FP8 copy also kept (about 44 GB resident) | omega qwen38/w4-gemv, sc qwen38/w4-decode, tool qwen38_ab | SPEED-1 (L125) |
| B | Weight bits below 4 (3-bit trellis / codebook) | 9.85 GB per word, ceiling 27.7 (moonshot B row 6) | quality unknown on our texts; new decoder in our code generator; offline quantizer | nothing in our repos beyond the plan | SPEED-10 (L134), CPU drift study first |
| C | Ternary weights (1.6 bits, oma_pack 5 trits per byte) | 26.89 GB x 1.6/8 -> ceiling 50.8 for a model this size (calc:51557f77); matmul becomes add and subtract | only for a model trained ternary; rounding the 27B to ternary afterwards has no receipt and is not expected to hold quality | src/algebra oma_trit, oma_pack, oma_quant (CPU reference only, no GPU kernel); bitnet-lab lane | oma_quant_rel_l2 + NLL on one 27B layer stack (CPU), before any kernel |
| D | Head (lm_head) at 8 or 4 bits | saves 5.94 ms per word at 8 bits (calc:54c49f10) | 98.54 percent same pick, every flip a near tie (calc:c42edc26); fails the strict 0.9999 gate | in branch, off by default | folded into SPEED-1's 4-bit gate |
| E | Faster 4-bit reading routine | 72.37 -> 54.82 ms at most, 1.32x (calc:f8c2fc5f) | engineering only, output unchanged | codegen_w4 in omega qwen38/w4-gemv; issue #560 for down staging | SPEED-3 (L127) |
| F | Faster 8-bit kernels to 234 GB/s | 8.13 -> 8.69 ceiling at 234 (moonshot B row 15) | engineering only | decode-speed branch | only if 8 bits stays the default |
| G | Speculative decoding, own MTP head, chain of 8 | tau 4.15 on code measured (cascade-mtp-chain handoff); ON PAPER 26.6 at 8 bits (calc:cad55c6e), 42.6 at 4 bits (calc:16902929) | output identical by construction; a drafter pass of about 23 ms is serial (moonshot E) | cascade/mtp-chain, cascade/tree-verify-gemm (32 rows ~ 1 row, bit identical to 1 row), sc-tvwire wiring | SPEED-2 (L126) |
| H | Speculative decoding, DFlash block drafter | words per pass measured: code 6.0, prose 2.29 (ledger L019); ON PAPER at 4 bits: code 68.7 (calc:a497d66e), prose 26.2 (calc:065b7bde) | drafter memory; drafter weights origin is a decision already taken in L019 | cascade/dflash-drafter, omega 8e4058d6 | SPEED-4 (L128) |
| I | Wormhole: Flash-Next drafts for the 27B | 29 of 30 words agree on code; k8 7.5 words per round (ledger L080) | Flash-Next has no GPU forward yet | sc wormhole/27b-flashnext | L080's timed race |
| J | Several requests per weight read (batching) | 32 rows cost 1.07x one row on gate/up -> about 30x aggregate on the weight part (calc:7acea7a9); per person unchanged | each extra person adds their own KV and DeltaNet state reads | tree-verify GEMM; cascade-continuous-batching lane | SPEED-5 (L129) |
| K | Fewer launches per word (fusion, one graph, persistent kernel) | at most the 5 ms non-weight work: 6.91 percent (calc:bdf910fd) | engineering; more complex kernels | the graph is already one command list per word | SPEED-8 (L132) |
| L | More than one channel / overlap transfer and compute | for one person about 0 (GPU busy 99 percent of the word); real for batching and for reading a new prompt while writing | OSC-0 review decision 12 (accepted with a gate: no regression in words/s and J/word) | single channel session today | measured inside SPEED-5 |
| M | Host C per word | at most the 0.69 ms host gap in the lab, 0.95 percent (calc:2fc24dbd); server gap no receipt | small | L097 chain map | SPEED-9 (L133) |
| N | Prefix reuse | time to first word, not words/s; 0.71 s second question at 30k (moonshot D) | memory or disk for saved state | generation.rs restore, kv_persist, document-memory lane | moonshot D "second question at 128k" (no receipt yet) |
| O | Mask-forced positions | if a share f of words is forced by the Omegese mask, feeding forced runs as one multi-row pass gives up to 1/(1-f) more words/s; f has no receipt | needs the multi-row graph from G | L112 omegese mask (forced steps already skip score readback and sampling) | SPEED-6 (L130) |
| P | Sampling on the GPU | greedy is already on the GPU; for temperature sampling, keeping it on the GPU avoids reading 993 KB of scores per word (calc:486308fb) | small | ARGMAX kernels in the q38 graph | part of SPEED-9 |
| Q | KV cache at 8 bits | KV bytes x0.5; 4-bit ceiling at 32k 17.28 -> 18.54 (calc:b28b2816, calc:81bd140d); larger at 128k | small accuracy risk | none for the 27B | SPEED-7 (L131) |
| R | Attention variants (shared or low-rank KV) | the model already shares KV across heads (4 KV heads) and only 16 of 64 layers keep a KV cache; the other 48 hold a fixed-size state | changing it means a different model | | none; KEEP the model's own layout |
| S | Tokenizer | runs once per word on a few bytes; no receipt | | L097 | part of SPEED-9 |
| T | HTTP door | one streamed message per word; no receipt | | aien-serve-http | part of SPEED-9 |
| U | Memory placement | moving scratch and DeltaNet state into GPU-cached memory was the biggest single 8-bit win (decode-speed handoff); x, residual and attention partials are still uncached (gain no receipt); W4-only mode frees about 25 GB | must follow the cache rules for anything the CPU also reads | AIEN_OMEGA_Q38_BUF_CACHED, GDN_STATE_CACHED | inside SPEED-8 and SPEED-1 |
| V | DeltaNet state in bf16 | half of 1.39 ms per word at most, about 1 percent | drift study owed | none | low priority |
| W | Bigger and faster model (Flash-Next, 6B active of 125B) | 4.82 GB per word, ceiling 56.6 (moonshot C, calc:0add470c) | a different model; GPU forward not built | flashnext/port (L020) | L020 chip runs |

## 6. Foundations and principles, judged by cost per word

The key fact (L097, chain-to-gpu handoff): **Omegese and the OSC compiler do not reach the GPU or the word loop
today.** The engine's GPU code is generated by C recipes (`src/omega_blackwell_codegen_*.c`) and driven from Rust in
sovereign-core. So the OSC language rules cost nothing per word. They would start to matter when the decode loop's
host program is written in OSC (OSC-0 review decision 14).

| Principle | Where it runs | Cost per word | Verdict |
|---|---|---|---|
| Handles with generations, object relationships instead of pointers (OSC-0 1, 2, 7) | OSC programs | none on the word loop | KEEP |
| Deterministic traps, typed Result, no unwinding (OSC-0 6) | OSC programs; the runtime's typed results (`rx_contract`) | none on the word loop; when the decode loop moves into OSC, a bounds check is a compare and branch per array access on the CPU side, not on the GPU stream | KEEP; when porting, keep checks out of per-weight loops (GPU code is generated, not OSC) |
| One channel, one lock (OSC-0 3; `omega_gpu_session.h`) | GPU session | about 0 for one person; limits batching and overlap | AMEND as decided (OSC-0 review decision 12, accepted with a speed gate); lever L |
| Four layouts, canonical encoding, big-endian identity (OSC-0 5, spec/canonical-encoding) | hashing of identities and records | off the word loop | KEEP |
| Receipts (spark-receipt, calc ids) | per measurement run | off the word loop | KEEP |
| Reproducible compiler, build twice (OSC-0 9) | build time | none | KEEP |
| Uncached memory as the safe default (hardware manual, 2026-10-03) | every engine buffer at first | was the largest 8-bit loss; cached scratch fixed it (decode-speed handoff) | KEEP for anything the CPU and GPU both touch (flags, inputs the host rewrites); AMEND for engine-private scratch (done for scratch and DeltaNet state, x/residual still open) |
| Bit-exact verify (each checked guess equals a one-word step) | speculative checking | forced one shared code path for 1 to 64 rows; measured cost about 0 (32 rows 0.99-1.04 ms vs 1 row 0.97 ms) | KEEP |
| Strict accuracy gate (24 golden words, score match 0.9999) | release gate | not per word, but it blocks every lower-bit setup, including the 8-bit head | ADD a 4-bit fit gate (inside SPEED-1); keep the strict gate for exactness claims |
| Typed results and the Omegese mask (spec/typed-result-constraints, L112) | per word when the mask is on | no receipt; L112 G3 measures it. A 248,320-word allow list is 31 KB as a bit vector (calc:2c4f8408), small next to 13.65 GB | KEEP; lever O turns the mask into a speed gain |
| Action graphs, plan reuse, branch-state reuse, cognitive routing (spec/action-graph-ir, branch-state-reuse, cognitive-routing) | the agent around the model | off the word loop; plan reuse measured 1.0 to 1.2x end to end on its own tasks (M1 block 3) | KEEP |
| Accelerator world: device stays open (spec/accelerator-world) | GPU session | saves the 40.7 ms open/close per job measured in M1 block 2 on every word | KEEP; this is why the word loop pays no driver cost |
| Turing yield profile (docs/turing) | measurement | off the word loop | KEEP |
| One-window measurement, GPU lock, memory guard (L113, L118) | the test bench | not per word, but runs waited hours; three resident 27B copies block 56 GB runs | outside this audit's code; L118 owns it |

## 7. Mathematics: cheaper formulations

| Formulation | Arithmetic | What exists | Status |
|---|---|---|---|
| Speculative checking: words per weight read | single-stream bound becomes tau x ceiling; at tau 4.15 and 4 bits that bound is 83 words/s (calc:43b31197), an upper bound not a forecast | MTP chain, DFlash, tree-verify GEMM | lever G, H |
| Batching: one weight read for N rows | arithmetic per row 51.2 GFLOP; free rows = tensor rate x read time / 51.2 GFLOP (moonshot E section 3) | tree-verify GEMM | lever J |
| 4-bit decode by bit tricks | nibble to float bits with integer ops, scale folded into the exponent (ab-fp8head-4bit plan) | codegen_w4 | in use; multi-row 4-bit needs the GB10's FP4 tensor path (no receipt) |
| Ternary matmul as add and subtract | weights in {-1, 0, 1}: no multiplies, 1.6 bits per weight packed | oma_trit, oma_pack (CPU reference) | only for a ternary-trained model (lever C) |
| Integer-only sampling | greedy argmax is already a compare; temperature sampling in integers would avoid float work on a 248,320 vector | none | not on the critical path |
| The mask as a bit vector | 31 KB per position, one lookup per candidate word | L112 libomegese | lever O |
| KV compression | 8-bit KV halves the KV read | none | lever Q |
| Low-rank or shared KV | already in the model (4 KV heads, 16 attention layers) | | KEEP |
| Recurrent layers (DeltaNet) | fixed state, so 48 of 64 layers cost the same at 1k or 128k context | in the model | KEEP |

## 8. Ranked order (highest predicted gain per unit of engineering first)

"Parallel" means it touches different files from the others and can run at the same time; "after X" means it shares
a kernel or graph with X.

| Rank | Ledger | Lever | Predicted gain (ON PAPER unless measured) | Engineering | Run | Deciding measurement |
|---|---|---|---|---|---|---|
| 1 | L125 | SPEED-1 4-bit as served default (store once, W4-only, 4-bit head, 4-bit gate) | measured 1.67x (calc:cfc16fae); frees about 25 GB | low: prototype measured | parallel (sc loader, omega set_layer) | W4-only vs FP8, 4 rotated rounds in one window, drift table |
| 2 | L126 | SPEED-2 speculative decode end to end (tree verify + MTP chain) | 26.6 at 8 bits, 42.6 at 4 bits (calc:cad55c6e, calc:16902929) | medium: kernel exists, wiring in progress | parallel with 1 and 3 (sc spec + omega tv) | tokens identical to k0, words per pass, wall words/s |
| 3 | L127 | SPEED-3 4-bit routine to the streaming ceiling | up to 1.32x (calc:f8c2fc5f) | medium | parallel (codegen_w4 only) | GB/s per step, ms per word vs 72.37 |
| 4 | L128 | SPEED-4 DFlash on 4 bits with tree verify | code 68.7, prose 26.2 (calc:a497d66e, calc:065b7bde) | medium | after 2 | words per pass and words/s, identical output |
| 5 | L129 | SPEED-5 several requests per weight read | about 30x aggregate on the weight part (calc:7acea7a9) | medium-high (server scheduling, per-stream state) | after 2 (same multi-row graph) | aggregate and per-person words/s, J per word, N = 1 to 32 |
| 6 | L130 | SPEED-6 mask-forced runs as one pass | 1/(1-f), f no receipt | low once 2 lands | after 2 and L112 | share of forced words on dev tasks, then words/s |
| 7 | L131 | SPEED-7 8-bit KV cache | 7 percent at 32k on 4 bits (calc:81bd140d vs calc:b28b2816), more at 128k | low-medium | parallel (attention kernel) | words/s at 8k/32k/128k, drift |
| 8 | L132 | SPEED-8 non-weight GPU work (fusion, cached x/residual) | at most 6.91 percent (calc:bdf910fd) | medium | coordinate with 1 (same q38 graph file, different functions) | GPU timeline per kernel class |
| 9 | L133 | SPEED-9 host side per word | at most 0.95 percent in the lab (calc:2fc24dbd); server gap no receipt | low | parallel (server) | per-word host microseconds table |
| 10 | L134 | SPEED-10 below 4 bits, drift study first | ceiling 27.7 (moonshot B row 6) | high | parallel (CPU only) | drift table on our six texts |

Combined, the top rows act on two different things: rows 1, 3 and 7 cut bytes per read; rows 2, 4, 5 and 6 add words
per read. Those two families multiply; levers inside one family do not.

## 9. What physics forbids

- One-word-at-a-time writing of this model above **10.15 words/s at 8 bits** or **20.0 at 4 bits** (bytes per word over
  273 GB/s). Kernel, compiler and language work can only close the gap to that line (today 0.80 and 0.69 of it).
- Faster than the memory: no setting makes LPDDR5x deliver more than 273 GB/s; the best measured stream is 218.6.
- Free lunch on accuracy: every bit removed from the weights changed some words in our own measurements (section 3,
  lever A). Only speculative decoding and batching raise speed with the output kept identical.
- Long context for free: the KV read grows with every word in the conversation; at 128k it is 8.59 GB per word in
  bf16 (calc:ce70f1a3), more than half the 4-bit weights.

## 10. Gaps (no receipt) and owed measurements

- Energy per word at the wall for decode: L109, L113.
- Host C per word in the server: L097 table, SPEED-9.
- Share of forced words under the Omegese mask: L112 G2.
- Whole-model speculative speed with the tree-verify kernel wired in: SPEED-2.
- A 4-bit accuracy gate fit for purpose: SPEED-1.
- The QWEN38-SPEED gate's first real number: L068.
- INT8 tensor rate of the GB10: no source in our repos.
