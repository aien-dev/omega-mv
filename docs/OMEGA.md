# What Omega is

**Omega is AIEN's language for all computing.** You write a program once. Omega's own tools check it and compile it straight to the machine code of the processor that runs it, GPU or CPU. No vendor compiler, toolkit or runtime library is involved anywhere.

On the GPU, that makes Omega the replacement for CUDA. On the CPU, it replaces C and its toolchain. This file says how far each of those has got, and the checks that have to pass before we can claim each one. Status words: PASS (a receipt exists on the named commit), PARTIAL, MISSING. Ledger item L386 tracks the work; tracking issue omega-mv#593.

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
| libcudart (runtime API) | Device, memory, launch, streams | Omega GPU session, command lists, job engine | PARTIAL: one device, one process; no streams or events; a timeout ends GPU use for the process |
| cuBLAS | Matrix multiply | Omega matmul, tensor-core GEMM, FP8 and BF16 APIs | PARTIAL: shaped for the models we run; no any-shape GEMM |
| cuDNN, FlashAttention | Attention and network layers | Omega decode and prefill attention | PARTIAL: model-shaped |
| CUB, Thrust | Reductions, scans, sorts | Omega frozen-order reductions (`evidence/E1-CLOSURE/`) | PARTIAL: no general scan or sort |
| cuFFT, cuRAND | FFT, random numbers | none | MISSING |
| cuobjdump, nvdisasm | Read machine code back | none: the tests still call NVIDIA's `nvdisasm` when it is installed | MISSING |
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

| Bar | Done when |
| --- | --- |
| **C1. The language reaches the GPU.** | Kernels written in Omegese compile through the IR and checker2 to sm_121 machine code and run on the GB10: vector add, a tiled GEMM and one decode-attention kernel. Their output matches a host reference bit for bit (or within a tolerance stated before the run), checker2 accepts every one, and a deliberately broken kernel is refused. At least one kernel of the live 35B decode path is then replaced by its Omegese version with identical golden tokens. |
| **C2. A stranger can run a kernel.** | From an empty folder, only the README commands build and run hello-GPU on the GB10, and the zero-CUDA gate (`aien-sovereign-core scripts/zero-cuda-gate.sh`) passes on the binary. The runtime API is written down and versioned (`docs/runtime/API.md`, manifest check with negative controls). |
| **C3. General libraries.** | Any-shape GEMM in BF16 and FP8, both layouts, at least 200 shapes from 1 to 8192, every shape correct against a host reference, speed given as a fraction of the measured tensor-core ceiling. Reductions and prefix sums over lengths 1 to 2^28, bit-exact against the frozen-order host reference. |
| **C4. Streams and events.** | Two independent queues of the same work finish in under 0.75 of the sum of their solo times; an event wait orders two dependent jobs, checked by data, on the chip. |
| **C5. Sharing and recovery.** | Two processes on one GPU both get correct results, or the second gets a named refusal and the first is unharmed. A kernel that never ends is timed out and a correct launch then succeeds in the same process. |
| **C6. No NVIDIA tool in the loop.** | Omega's own sm_121 disassembler agrees with `nvdisasm` on every library kernel in `src/`, and every check that called `nvdisasm` calls ours. |

## 4. The rest of "Omega replaces everything"

These are not part of the CUDA claim. They are the rest of what Omega is for, and each has its own bar.

- **No NVIDIA software at all.** Our own GB10 driver replaces `nvidia.ko` and `nvidia-uvm.ko` (L332): Omega's golden tokens identical on our driver and on NVIDIA's, with per-word timing. The GSP firmware stays, as above.
- **The runtime written in Omega.** The GPU runtime and libraries are C today. The language rule (aien-architecture ADR 0024) keeps C only where hardware, boot, ABI or measurement justifies it, so the runtime moves into Omega once C1 makes that possible.
- **The CPU.** The OSC and Omegese backends emit AArch64 machine code today (`src/compiler/osc_a64.c`). The live server is still a gcc and Rust build linked against glibc; it moves to Omega-compiled code as the language grows.
