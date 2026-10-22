# What Omega is

**Omega is AIEN's language for all computing.** You write a program once. Omega's own tools check it and compile it straight to the machine code of the processor that runs it, GPU or CPU. No vendor compiler, toolkit or runtime library is involved anywhere.

On the GPU, that makes Omega the replacement for CUDA. We call Omega's GPU stack **CUDA++**: everything CUDA gives you (sections 2 and 3), plus what CUDA does not give you, the motor (section 6). On the CPU, Omega replaces C and its toolchain.

**What CUDA++ is for: less computation, out of the big data centers.** The goal is not a faster way to rent a data center. It is to need less of one: do the same work with fewer bytes moved, fewer operations and fewer joules per answer, so it runs on a machine a person owns (a DGX Spark, a PC with one graphics card, a laptop) instead of a building full of them. Every CUDA++ result is reported as work per answer (bytes, operations and energy per word, against the machine's own ceiling), not only as speed. This file says how far each of those has got, and the checks that have to pass before we can claim each one. Status words: PASS (a receipt exists on the named commit), PARTIAL, MISSING. Ledger item L386 tracks the work; tracking issue omega-mv#593.

## 1. The parts

| Part | What it is | Where it lives |
| --- | --- | --- |
| **The language** | Omegese (the language the AI writer uses, `docs/one_layer/OMEGESE.md`) and OSC (the text language, `docs/osc/`). Both compile to one binary IR. checker2 checks the IR and issues a receipt. A backend then turns the IR into machine code. | `tools/omegese/`, `src/compiler/`, `src/checker2/` |
| **The GPU toolchain** | Omega writes the GPU's own machine code (sm_121 SASS on the GB10). There is no PTX, no nvcc and no ptxas. A kernel builder with 59 instruction kinds, a register allocator and an encoder produce every kernel Omega runs. | `src/omega_blackwell_codegen*.c`, `src/omega_blackwell_encoder.c` |
| **The GPU runtime** | Opens the device, allocates memory, launches kernels, records command lists and runs jobs. It does the job of CUDA's runtime and driver API (libcudart, libcuda). | `src/omega_gpu_session.h`, `omega_gpu_cmdlist.h`, `omega_gpu_engine.h`, `omega_gpu_platform.h` |
| **The libraries** | Matrix multiply, matrix-vector (BF16, FP8, 4-bit, ternary), attention (decode and prefill), reductions, elementwise. They do the job of cuBLAS and cuDNN. | `src/omega_gpu_*_api.h` |

**Below Omega, and not part of it:** the driver. Omega reaches the GPU through the physics repository's driver client (`nvrm/nvrm.c`, `m16/m16_native.c`). The client talks to NVIDIA's open-source kernel module (`nvidia.ko`, `nvidia-uvm.ko`, version 580.173.02) and the module boots NVIDIA's signed GSP firmware. Replacing the kernel module with our own driver is a separate job, L332. The firmware stays NVIDIA's for a physical reason: the GPU checks its signature and will not boot anything else.

## 2. CUDA, piece by piece

"CUDA" is a stack of NVIDIA software. This is each piece and Omega's replacement for it.

| CUDA piece | Its job | Omega's replacement | Status |
| --- | --- | --- | --- |
| CUDA C++ language | How you write a kernel | **The kernel builder (C calls).** Omegese and OSC do not compile to the GPU yet. | **MISSING** (bar C1) |
| nvcc, NVVM, PTX, ptxas | Compile the kernel to machine code | Omega's own sm_121 encoder and register allocator | PASS: every kernel Omega runs is encoded by it |
| libcuda (driver API) | Talk to the kernel driver | physics `nvrm` client, ioctls straight to the open kernel module | PASS (`evidence/m16-blackwell-native-path-requalification-receipt.json` in physics) |
| libcudart (runtime API) | Device, memory, launch, streams | Omega GPU session, command lists, job engine | PARTIAL: second queue, events and recovery after a timeout are built (bars C4, C5, branches); chip runs pending |
| cuBLAS | Matrix multiply | Omega matmul, tensor-core GEMM, FP8 and BF16 APIs, any-shape GEMM (bar C3, branch) | PARTIAL: any-shape GEMM built, chip run pending |
| cuDNN, FlashAttention | Attention and network layers | Omega decode and prefill attention | PARTIAL: model-shaped |
| CUB, Thrust | Reductions, scans, sorts | Omega frozen-order reductions (`evidence/E1-CLOSURE/`), general reductions and prefix sums up to 2^28 (bar C3, branch) | PARTIAL: scan built, chip run pending; no sort |
| cuFFT, cuRAND | FFT, random numbers | none | MISSING |
| cuobjdump, nvdisasm | Read machine code back | Omega's own sm_121 disassembler (`build/sm121dis`); `nvdisasm` stays only as the reference it is checked against | PASS on a branch (bar C6) |
| Nsight | Measure | Omega's own ceiling probes (115.14 TFLOPS tensor-core peak, 195.1 GB/s reads; `evidence/OWN30B-T3-TC-PEAK-BW-5faa0f70-20261013T2236Z/`) | PARTIAL: probes exist, no single tool |
| CUDA samples, docs | Your first kernel | none yet (`runtime/r2-hello`, `runtime/r1-api` are unmerged branches) | MISSING |
| `nvidia.ko`, `nvidia-uvm.ko` | Kernel driver | not Omega: AIEN's own GB10 driver, L332 | MISSING |
| GSP firmware | Runs inside the GPU | stays NVIDIA's (signature-checked by the chip) | not replaceable |

### What is already true, with a receipt

The 35B model AIEN serves on the Spark runs on Omega with no NVIDIA user-space software at all. On 2026-10-21 the live server (port 18091, `--engine omega`) had only `libc`, `libm`, `libgcc_s` and the loader mapped: zero CUDA libraries. It held `/dev/nvidia0`, `/dev/nvidiactl` and `/dev/nvidia-uvm` open, which is the open kernel module and nothing above it. Receipt: `evidence/WHAT-OMEGA-IS-20261021/live-serve-no-cuda.txt` (binary sha256, mapped libraries, `ldd`, driver version, the commands used).

So this sentence is true today: **Omega runs AIEN's models on the GB10 with no CUDA software: no libcuda, no cuBLAS, no nvcc.**

This one is not true yet: **"Omega replaces CUDA."** The language does not reach the GPU, and the toolkit has gaps a stranger would hit on day one.

## 3. The bar for "Omega replaces CUDA"

We say it when every row is PASS, each with a receipt pushed on the exact commit. No row is waived.

| Bar | Done when | Status (2026-10-22) |
| --- | --- | --- |
| **C1. The language reaches the GPU.** | Kernels written in Omegese compile through the IR and checker2 to sm_121 machine code and run on the GB10: vector add, a tiled GEMM and one decode-attention kernel. Their output matches a host reference bit for bit (or within a tolerance stated before the run), checker2 accepts every one, and a deliberately broken kernel is refused. At least one kernel of the live 35B decode path is then replaced by its Omegese version with identical golden tokens. | PARTIAL: the compile path is built and checked off the chip (omega `omegese/gpu`); the chip runs and the 35B swap on the merge train are waiting for chip time. |
| **C2. A stranger can run a kernel.** | From an empty folder, only the README commands build and run hello-GPU on the GB10, and the zero-CUDA gate (`aien-sovereign-core scripts/zero-cuda-gate.sh`) passes on the binary. The runtime API is written down and versioned (`docs/runtime/API.md`, manifest check with negative controls). | PARTIAL: zero-CUDA gate and its self-test PASS (omega `runtime/r2-hello` 0f44b52b); the empty-folder run on the chip is waiting for chip time. |
| **C3. General libraries.** | Any-shape GEMM in BF16 and FP8, both layouts, at least 200 shapes from 1 to 8192, every shape correct against a host reference, speed given as a fraction of the measured tensor-core ceiling. Reductions and prefix sums over lengths 1 to 2^28, bit-exact against the frozen-order host reference. | PARTIAL: any-shape GEMM (omega `c3/gemm` 73e5e33f) and reductions and prefix sums (omega `c3/reduce-scan` a42ea928) are bit-exact in the simulator against the chip's own arithmetic model; the 208-shape chip run and the 1 to 2^28 chip run are waiting for chip time. |
| **C4. Streams and events.** | Two independent queues of the same work finish in under 0.75 of the sum of their solo times; an event wait orders two dependent jobs, checked by data, on the chip. | PARTIAL: second queue (from L136) and the event wait (omega `c4/streams` 54c12286), checked off the chip; the timing and the event on the chip are waiting for chip time. |
| **C5. Sharing and recovery.** | Two processes on one GPU both get correct results, or the second gets a named refusal and the first is unharmed. A kernel that never ends is timed out and a correct launch then succeeds in the same process. | PARTIAL: recovery after a timeout without restarting the process (omega `c5/isolation` 560324e2), checked on a stand-in device with 9 of 9 planted faults caught; the chip run is waiting for chip time. |
| **C6. No NVIDIA tool in the loop.** | Omega's own sm_121 disassembler agrees with `nvdisasm` on every library kernel in `src/`, and every check that called `nvdisasm` calls ours. | PASS on omega `c6/disasm` 19eebe0e (`evidence/C6-DISASM/`): all 331 library kernels agree word for word, the planted-fault checks fail as they must, every listing check now uses ours. Waiting to merge. |

## 4. The rest of "Omega replaces everything"

These are not part of the CUDA claim. They are the rest of what Omega is for, and each has its own bar.

- **No NVIDIA software at all.** Our own GB10 driver replaces `nvidia.ko` and `nvidia-uvm.ko` (L332): Omega's golden tokens identical on our driver and on NVIDIA's, with per-word timing. The GSP firmware stays, as above.
- **The runtime written in Omega.** The GPU runtime and libraries are C today. The language rule (aien-architecture ADR 0024) keeps C only where hardware, boot, ABI or measurement justifies it, so the runtime moves into Omega once C1 makes that possible.
- **The CPU.** The OSC and Omegese backends emit AArch64 machine code today (`src/compiler/osc_a64.c`). The live server is still a gcc and Rust build linked against glibc; it moves to Omega-compiled code as the language grows.

## 5. Performance: matching and then beating CUDA

Same machine (one DGX Spark GB10), same kind of model (a ~30B mixture-of-experts with about 3B active per word). Our numbers are measured by us, with receipts. Everyone else's are published by others and labelled that way, with their source; none of them has been re-measured here yet (bar P0). Their models are close relatives of ours, not identical: Qwen3-30B-A3B has no DeltaNet state, Qwen3.6-35B-A3B does.

| What | Omega today (Qwen3.6-35B-A3B, FP8) | Others on the same box (published by others) | Physical ceiling here |
| --- | --- | --- | --- |
| One conversation, words per second | 41.7 to 49.8 (20.1 to 24.0 ms per word; L314 `evidence serve-combined/README.md`, L354 door 20.1 ms) | FP8: vLLM about 28 to 30 on this 35B (Tessera, June 2026), 44 on Qwen3-Coder-30B FP8 (Hellohal2064 vLLM build); llama.cpp 44.3 at Q8_0. 4-bit: llama.cpp 89.3 (Q4_K_M, DandinPower llama.cpp_bench), vLLM 77 to 81 (AWQ/NVFP4), up to 97 to 120 with NVFP4 + MTP (Tessera) | FP8 58.5 to 67 at the measured 207 to 237 GB/s; 4-bit experts and head 82 to 94; plus 4-bit trunk about 142 (`~/handoffs/2026-10-20-q36-ceiling-numbers.md`, L310) |
| Eight conversations at once, total words per second | 57.11 (N=8 stroke, sc `serve/q36-batch-v2` 9c62690a, calc:f67ae6c1) | vLLM 313 (Qwen3-30B-A3B, 8 concurrent, NVIDIA forum harness); 845 (Qwen3-Coder-30B AWQ, GPUwerk) | about 164 at 4k context with f32 state, more with bf16 state (ceiling notes) |
| Reading the prompt (prefill), words per second | 196.8 at 8k, 250 at 2k, 275.5 at 256 (L369, window 20261021T215012Z, omega `serve/q36-prefill-v2` 60ba3051) | llama.cpp about 2,000 to 2,500; vLLM 5,705 at 8k (GPUwerk, AWQ) | roughly 30,000 from the FP8 tensor rate (about 7 GFLOP per word at 237.5 TFLOPS; arithmetic, not measured); prefill is compute-bound |
| Extra kernel launch in a list | 24 to 31 us with wait-for-idle (`evidence/OWN30B-LAUNCH-COST-20261014T0120Z/`) | CUDA graphs: a few us per node (published) | 2.95 percent of a word today (efficiency review G1), so small now; it grows with batching |

Where we stand: at FP8 with one conversation we are level with or ahead of the published CUDA numbers. We are behind on three things: prompt reading (7 to 30 times), many conversations at once (5 to 15 times), and 4-bit weights (not yet live). All three are work, not physics: the ceilings above are far from reached.

### The performance bar

Each row PASS needs our receipt and the other system measured by us on this Spark, same model, same prompts, same output length, in the same quiet window.

| Bar | Done when |
| --- | --- |
| **P0. Measure them ourselves.** | llama.cpp and vLLM (their own current releases, built for sm_121) run Qwen3.6-35B-A3B at FP8 and at 4-bit on this Spark, one and eight conversations, 512 and 8k prompts, three runs each; table and logs pushed, labelled measured-by-us. Benchmark only: nothing of theirs enters the Omega path. |
| **P1. One conversation.** | Omega's best format beats the best measured CUDA number at one conversation, with golden tokens identical to our host reference. Levers: 4-bit experts, head and trunk in our own format (L284, L355), exact two-pass head (L314), MTP and tree verify (L310, L013). |
| **P2. Prompt reading.** | Omega prefill beats the best measured CUDA number at 512 and 8k. Levers: general tensor-core GEMM at a high fraction of the 237.5 TFLOPS rate (bar C3), flash-style prefill attention, chunked DeltaNet scan (L369, L017). |
| **P3. Many conversations.** | Omega total words per second beats the best measured CUDA number at 8 and 32 conversations. Levers: grouped expert GEMM over the expert union, bf16 state (L313), continuous batching on the stroke (L312, L354). |
| **P4. What CUDA stacks do not do.** | At least one measured win that comes from owning the whole path: one submission per word or a resident whole-model kernel (L272), zero-copy use of the shared memory, our own driver's launch path (L332). |

### Targets: the roof, not parity (Drake 2026-10-22: "Get us level or ahead for everything. Push this for the roof as high as you can go.")

Parity with the best CUDA number is the floor. The target for each row is a fraction of this machine's own physical ceiling, set high enough to clear every published CUDA number by a wide margin. The same-model CUDA numbers from P0 replace the published ones as soon as they exist; if a measured CUDA number is above a target here, the target moves above it.

| Row | Best CUDA number known (published by others) | Our target | Owner |
| --- | --- | --- | --- |
| One conversation, 8-bit | about 44 words/s | 60 (0.9 of the 8-bit ceiling) | L356, L314 |
| One conversation, best format | 97 to 120 (NVFP4 with MTP) | 150 or more: 4-bit experts, head and trunk in our own format (ceiling about 142) plus several words per pass (MTP, tree verify) | L355, L284 |
| One request end to end, prompt included | n/a | within 10 percent of the decode figure for prompts up to 2k | L369, L375 |
| 8 conversations at once | 313 to 845 | 0.8 of the per-N ceiling with bf16 state and 4-bit weights, and above the P0 figure for the same model | L354, L312, L313 |
| 32 conversations at once | 1,679 (30B, AWQ) | above the P0 figure for the same model | L354, L312 |
| Prompt reading, 8k | 5,705 | 15,000 (0.5 of the tensor-core ceiling) | L403 (GEMM and attention kernels), L369 (door) |
| Time to first word, 8k prompt | 3.85 s (vLLM writeup, 7,234 tokens) | under 1 s | L403, L369, L375 |

## 6. CUDA++: the motor

CUDA gives you kernels, memory and streams. It leaves the hard part to you: how a whole model's bytes reach the math cores in time, and how many conversations share one read of the weights. Omega's answer is the motor. The motor is the ++ in CUDA++.

**What the motor is.** Separate engines in one body, each with one job and its own timing trace, coupled by one schedule:

- **The schedule**: one static table, built once from the model's structure before anything runs. It names which weight part lands in which buffer at which beat. A host proof checks it is complete, acyclic and collision-free, and that it has exactly the true dependency edges (buffer full before fire, layer before layer, word before word, release before refill) and no others. Nothing is decided while running.
- **The movement engines**: the NVMe engine reads a beat's weight parts from disk and checks each one's fingerprint on arrival; the RAM engine copies a checked part into the buffer and checks it again. A part that fails a check stops the run.
- **The power engine**: Omega's own GPU kernels, firing on a buffer that is already full. A routed expert that is not in the buffer is a misfire, and the run fails closed.
- **The stroke**: N conversations fire on one read of the weights. Each conversation must produce exactly the words it produces alone.

Design, every part and every receipt: [docs/speed/ENGINE.md](https://github.com/aien-dev/omega/blob/edd951976a0a8b199e63a1b55149680cf77033d0/docs/speed/ENGINE.md) (omega `feat/typewriter-engine`). Engine code: [`crates/aien-engine`](https://github.com/aien-dev/aien-sovereign-core/tree/d311394098afc3e09526c46d8c492173ff2c4933/crates/aien-engine) (`schedule.rs`, `movement.rs`, `piston.rs`, `timing.rs`; aien-sovereign-core `feat/typewriter-engine`). The 35B kernels the stroke fires: omega [`motor/serve`](https://github.com/aien-dev/omega/tree/881f43977aa4f6ed8842057bee2e6128649ae05f).

### Live now

Port 18091 on the Spark serves Qwen3.6-35B-A3B on the motor's stroke (`AIEN_Q36_STROKE=1`, binary `bin-16b0e6f8`), with zero CUDA libraries loaded (section 2 receipt).

| What | Measured | Receipt |
| --- | --- | --- |
| Total words per second, N conversations at once (32 mixed chat prompts, 128 words each, prompt reading included, chip exclusive and quiet, 2 runs) | N=1: 22.0 to 22.5. N=8: 57.1. N=16: 62.4. N=32: 65.7 to 66.4 | aien-sovereign-core [`evidence/q36-batch-20261021`](https://github.com/aien-dev/aien-sovereign-core/tree/9c62690a252ab1c0b75eb8933c550316b5c50d52/evidence/q36-batch-20261021) |
| The stroke against the same conversations taken in turn | 1.80x at N=8, 1.98x at N=16, 1.88x at N=32; at N=1, 0.98x (inside the run-to-run spread) | same |
| Chip time per word, one conversation | 21.9 to 23.0 ms (43.5 to 45.7 words per second), 0.67 of the 63.25 ceiling at today's bytes | aien-sovereign-core `evidence/motor-chip-20261021` (ENGINE.md section F) |
| Same words as the exact host reference | Gate 1: 32 of 32 words on three prompts, 31 of 32 on the fourth with its one flip inside a band written before the run; the mutant (shared expert dropped) fails. Gate 2 through the HTTP door: 24 of 24 ids; the mutant server fails | same |
| Movement engines alone, 35B layer beats | 12 of 12 configurations: 0 late beats, 0 misfires over 1,600 beats each; best beat 7.0 ms for 25.2 MB, median wait between fuel and fire 21 us | omega `evidence/engine/35b-engines-20261020T2349Z` (ENGINE.md section C) |
| Whole 35B fed from the engines' buffers (host) | 32 of 32 words equal to the reference, 1,280 beats, 0 misfires | omega `evidence/engine/35b-host-20261021T0025Z` (ENGINE.md section B) |

The one-conversation figure in section 5 (41.7 to 49.8) is chip time per word. Through the door with the prompt read word by word, a single request today gets about 22 words per second end to end. Faster prompt reading (below) is what closes that gap.

### Ready, not yet live

- **Prompt reading in chunks**: 275.5, 250.0 and 196.8 words per second at 256, 2k and 8k prompts against 49.7, 48.6 and 45.3 word by word (5.5x, 5.1x, 4.3x), output bit-identical at all three lengths. omega `serve/q36-prefill-v2` 60ba3051, aien-sovereign-core `serve/q36-prefill-v2` [`evidence/q36-prefill-20261021`](https://github.com/aien-dev/aien-sovereign-core/tree/f6f341efc654263ce8a2812bfaef04e297c00808/evidence/q36-prefill-20261021).
- **Exact two-pass word scoring**: 21.8 against 24.0 ms per word, served words identical (L314).

### Not done, said plainly

- The chip piston (a resident kernel fed by the movement engines inside GPU-visible buffers) is built and proven in its host simulator but has not fired on the chip.
- The thesis gate, the planned loop against today's path with identical words and at least the same speed at every context, needs that chip run.
- The NVMe engine moves 3.0 to 3.4 GB/s against 10.6 GB/s possible.
- None of the motor is on omega `main` yet. Every piece is a pushed branch with its receipts, waiting in the desk's merge queue.
- The engine code is Rust today (scaffolding under ADR 0024); it moves into Omega with the rest of the runtime.
