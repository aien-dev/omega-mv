# Omega runtime roadmap: an open-source CUDA alternative

Status: draft, 2026-10-19. Owner: the Omega runtime stream (ledger L085). Tracking issue: omega-mv#593.

Omega is meant to be a GPU compute runtime that anyone can use instead of CUDA: its own command
format, its own kernel toolchain, its own memory model, launch path and libraries, AGPL, with no
NVIDIA user-space library (no libcuda, no cuBLAS, no nvcc or PTX toolchain). The only NVIDIA parts
it relies on are the ones the hardware requires: the open kernel driver and its firmware.

This file has three parts:

1. **Gap map.** For each thing a developer replacing CUDA needs, what exists today (with a path or a
   receipt) and what is missing.
2. **Slices.** The ordered work, each with a done-when you can check.
3. **Rules.** Reuse first, receipts, one sensitive measurement at a time.

Every "exists" line names a file on `origin/main` at `a8ffd2b9` unless it says otherwise. Anything
on a branch is marked as a branch. "Exists" never means "qualified": a host test is not a chip result.

## 1. Gap map

### 1.1 Runtime API surface (devices, memory, launch, ordering, errors)

| Need | Exists today | Gap |
| --- | --- | --- |
| Open a device | `src/omega_gpu_session.h` `omega_gpu_session_open`: one device, one channel, one process-wide lock. | One device only, no device query (name, SM count, memory) through the API. |
| Platform layer | `src/omega_gpu_platform.h` (platform contract ABI 1.2: typed handles, explicit rights, polling completion), Linux reference `src/omega_gpu_platform_linux.c` (uses `nvrm.h`/`m16_native.h` from the physics repo), freestanding build `make gpu-engine-freestanding`. Design: `docs/numeric/OMEGA_GPU_NATIVE_BOUNDARY.md`. | Good base. Not documented for outside users. |
| Memory | `omega_gpu_session_alloc`, `_alloc_code`, `_free`, `_sync_to_gpu`, `_sync_to_cpu`, scratch buffers; GPU-cached read-only weights `src/omega_gpu_mem_cached.h`. Every ordinary buffer is GPU-uncached (reason in the session header). | No written memory model. No cached read-write buffers. No async copy (a copy-engine encoder exists only on branch `rig/gb20x`). |
| Launch one kernel | `omega_gpu_session_launch` with `OmegaGpuLaunch` (code address, registers, block, grid, argument words, shared memory, timeout). | Launch is synchronous. Measured cost of each extra launch in a list: about 30 us with a wait-for-idle barrier (`evidence/OWN30B-LAUNCH-COST-20261014T0120Z/r1/receipt.json`, verdict FAIL on 4 of 11 checks). |
| Many launches at once | `src/omega_gpu_cmdlist.h` (record N launches, submit once, barrier or independent); one-command-list decode token `src/omega_gpu_decode_graph.h`. | No streams, no events, no host callbacks. Pipelined, concurrent and dependency-ordered execution is being built in omega-mv#580 (ledger L022): reuse it, do not rebuild it. |
| One-call job engine | `src/omega_gpu_engine.h` `omega_gpu_execute` with named states and failures. Chip receipt for the vector-add job on branch `hive/DEEP2-A3-gpu-engine-impl` (`evidence/OMEGA-GPU-ENGINE/gb10-1-2eb912d`). | Accepts one job shape (1-D vector) today; #580 widens it. |
| Error model | Every failure names its stage (`omega_gpu_session_last_error`); an uncertain completion latches the process and parks buffers. | A single timeout ends GPU use for the whole process. Recovery is open in omega-mv#518. |
| Several processes | Nothing. | A second channel failed with NV_ERR_NO_MEMORY while 27B servers were resident (`~/handoffs/2026-10-19-omega-580-chords.md`), and GPU channel create returns status 0x51 under load (omega-mv#523). A runtime has to share the GPU between processes or refuse cleanly. |
| Written, versioned API | Comments in headers; `docs/numeric/OMEGA_GPU_ENGINE.md`. | **No API specification and no version promise.** Slice R1. |

### 1.2 Kernel authoring (writing your own kernel without nvcc)

| Need | Exists today | Gap |
| --- | --- | --- |
| A way to write a kernel | C kernel builder `src/omega_blackwell_codegen.h`: 59 instruction kinds on virtual registers, a register allocator (`omega_bw_regalloc_solve`) and an encoder to sm_121 machine code (`omega_bw_encode_program`). Every library kernel is written this way (`src/omega_blackwell_codegen_*.c`). | No guide, no worked example for someone outside the project. The builder is C calls, not a kernel language. |
| Checking a kernel before it runs | Warp simulators `tests/bw_warp_sim*.h`, tensor-core models `tests/bw_hmma_model.h`, `tests/bw_qmma_model.h`; register-count guard `omega_bw_gpr_declared_ok`; static hazard checker planned in omega-mv#554. | The simulators live under `tests/`, not as a tool a kernel author runs. |
| Reading machine code back | The tests call `nvdisasm` when it is present (`~/handoffs/2026-10-19-omega-port-hosted.md`, gpu-attention listing check). | That is an NVIDIA tool. We need our own sm_121 disassembler for the checks. |
| A kernel language | OSC compiler (`docs/osc/`) targets the CPU today. | No path from a high-level kernel to the GPU. Later wave. |

### 1.3 Libraries

| Need | Exists today (API, kernel, chip receipt) | Gap |
| --- | --- | --- |
| Matrix multiply | `src/omega_gpu_matmul_api.h` (`evidence/GPU_MATMUL_API/receipt-6cd2dc0-run4.json`), FP8 grouped GEMM on tensor cores `src/omega_gpu_tcgemm_api.h`, `src/omega_gpu_fp8gemm_api.h` (`evidence/OWN30B-FP8GEMM-10198f93-20261013T2058Z`). | Shapes and formats follow one model (Qwen3-30B). No general GEMM with any M, N, K, layout and type, and no table of results against the hardware ceiling. |
| Matrix-vector | FP8 `src/omega_gpu_fp8_api.h`, bf16 `src/omega_gpu_bf16_gemv_api.h`, ternary `src/omega_gpu_tern_api.h`, 4-bit work in the cascade streams. | Same: model-shaped, not general. |
| Attention | decode `src/omega_gpu_attention_api.h`, `src/omega_gpu_decode_attn_api.h`; prefill on tensor cores `src/omega_gpu_prefill_attn_api.h`. | Model-shaped. |
| Reductions and elementwise | `src/omega_gpu_elementwise_api.h`; FP32 reductions with a frozen summation order `src/omega_numeric_reduce.h`, closed on the chip (`evidence/E1-CLOSURE/`). | No general reduce/scan/sort library with sizes chosen by the caller. |
| FFT, sort, random numbers | Nothing found in Omega. | Later wave. |

### 1.4 Portability

| Need | Exists today | Gap |
| --- | --- | --- |
| GB10 (DGX Spark) | Everything above. | |
| RTX 5070 (Blackwell desktop, sm_120) | Branch `rig/gb20x` (ledger L024): target table and probe, a common submit interface, a copy-engine encoder, a WSL2 `/dev/dxg` backend tested against a fake kernel, design `docs/rig/dxg-backend.md`. Nothing has run on a real 5070 yet. | Owned by L024; this stream tracks it. |
| Other GPUs | Nothing. | A written hardware-abstraction note: what is chip-specific (machine code encoder, launch descriptor, class numbers) and what is not. |

### 1.5 Proof for outsiders

| Need | Exists today | Gap |
| --- | --- | --- |
| Build from a fresh checkout | Host suites build with `PHYSICS_DIR=/nonexistent` (README). The GPU path needs a second repository checked out beside Omega at the commit in `physics.lock`. | No one-command GPU build from a fresh clone. |
| "Hello GPU" | Vector add exists inside the engine tests, not as an example. | No `examples/`. Slice R2. |
| A small non-AIEN program | Nothing. | Slice R4. |

### 1.6 Ceilings we measure against (our own probes)

| Ceiling | Measured | Receipt |
| --- | --- | --- |
| Tensor-core peak | 115.14 TFLOPS (best row, 0 wrong values) | `evidence/OWN30B-T3-TC-PEAK-BW-5faa0f70-20261013T2236Z/peak.log` |
| Memory read bandwidth, distinct bytes | 195.1 GB/s delivered (96 MB distinct) | same folder, `bw.log` |
| Launch cost in a command list | about 24 to 31 us per extra launch with wait-for-idle | `evidence/OWN30B-LAUNCH-COST-20261014T0120Z/r1/receipt.json` |

### 1.7 Parts in other AIEN repositories

Checked 2026-10-19 against each repository's `origin/main` after a fetch, plus `gh search code --owner aien-dev`
for `omega_gpu`, `hello gpu`, `hello_gpu` and `roofline`.

| Repository | Part | What it gives the runtime |
| --- | --- | --- |
| physics (aien-dev/physics-mv) | `nvrm/nvrm.c`, `nvrm/nvrm.h` | The driver client: talks to the NVIDIA open kernel module (`/dev/nvidiactl`, `/dev/nvidia0`, `/dev/nvidia-uvm`) with no libcuda. Client, device, address space, channel, compute object, alloc (`nvrm_alloc_gpu_uncached`), submit, doorbell. Gates `physics-nvrm-lifecycle`, `physics-submission-visibility`. |
| physics | `m16/m16_native.{c,h}`, `m16/m16_gpu_wait.{c,h}` | Channel creation, release build, method submit, marker wait. Chip receipt `evidence/m16-blackwell-native-path-requalification-receipt.json` (includes "no libcuda link"). |
| physics | `forge/forge_descriptor.c` | Reads the chip identity live (compute class, SM version, sm_121). |
| physics | `third_party/nvidia-open-580.173.02/` | The pinned open-driver headers. |
| aienos (aien-dev/aienos-mv) | `docs/GB10_NATIVE_DEPENDENCY_MAP.md`, `docs/GB10_NATIVE_STATUS.md`, `docs/GB10_FIRMWARE_INVENTORY.md`, `native/kernel/core/smmu.{c,h}`, `native/kernel/tools/ck_gsp_*.c` | The plan and first host/QEMU parts for running Omega with no Linux driver at all (aienos#286). Nothing has run natively on the GB10; channel, doorbell and completion (cut D) are missing. |
| aien-sovereign-core (aien-dev/aien-sovereign-core-mv) | `crates/aien-omega-gpu/src/lib.rs`, `src/ffi.rs`, `abi/omega_gpu.manifest` | A safe Rust wrapper over the Omega GPU API and a manifest of 34 functions, 6 structs and 23 constants pinned to 5 Omega headers. Chip evidence `docs/inference/evidence/own30b-omega4799-20261015/abi-qualify/chip/evidence.json`. **The starting point for Omega's published API list and its Rust binding.** |
| aien-sovereign-core | `crates/aien-abi-conformance/` | Checks headers, a compiled layout probe and library symbols against the manifest. |
| aien-sovereign-core | `scripts/zero-cuda-gate.sh`, `tests/gates/sc-zero-cuda.gate` | Fails any binary that needs a CUDA library or symbol, with a negative-control self-test. **Reuse as the "no CUDA inside" proof for every Omega example.** |
| aien-sovereign-core | `crates/aien-omega-{bf16,fp8,moe,router,dattn,dgraph,pf,compose}`, `crates/aien-inference-abi/src/omega_backend.rs` | Per-header Rust bindings and a backend that runs 10 tensor operations on Omega. |
| aien-rig (aien-dev/aien-rig) | branch `app/v0` `docs/WINDOWS-GPU-PATH.md`; branch `engine/core` `engine/CEILING.md` | The plan to put the physics driver interface over WSL2 `/dev/dxg`; paper ceilings for the RTX 5070. The engine there is Omega through sovereign-core; no CUDA code found in the repository. |
| benchmarks (aien-dev/benchmarks-mv) | `prime-drag-race/` | A native GB10 program that is not a model (sieve). No GPU ceiling probes. |

Not found anywhere in the organisation: a "hello GPU" example, a kernel-authoring guide, a general
(any-shape) GEMM, an sm_121 disassembler of our own, streams and events, multi-process sharing.

## 2. Slices

Each slice is one branch off `origin/main`, its own worktree, a ledger line and a GitHub issue. The
done-when is something anyone can check: a command, its output, and a pushed receipt. Each slice
puts its make targets in its own fragment `mk/runtime-<slice>.mk` (one `include` line in the
`Makefile`), so parallel slices do not collide. Plan reviewed by Fable on 2026-10-19
(`~/handoffs/omega-runtime-advisor-r1.txt`); its corrections are folded in below.

### Wave 1 (started 2026-10-19; none of these touches the cascade streams' kernels)

**R1. Omega Runtime API v0: written down, listed, versioned, checked.** (omega-mv#594)
Write `docs/runtime/API.md`: devices and platform, memory, launch, command lists, the job engine,
the error model (named failures, the latch, parked buffers) and the threading model, each entry
marked STABLE, EXPERIMENTAL or INTERNAL. Bring the API list into Omega as
`docs/runtime/omega_runtime_api.manifest`, starting from sovereign-core's `abi/omega_gpu.manifest`.
From now on **Omega's manifest is the canonical one**; sovereign-core's copy must match it (R12
checks that). Add a version number (one header macro) and a written compatibility rule: what may
change in a minor version and what needs a major one.
Reuses: `aien-omega-gpu/abi/omega_gpu.manifest`, `aien-abi-conformance` (method), the session,
cmdlist, engine and platform headers.
Done when: `make test-runtime-api` (host, no GPU) passes; it fails when a listed function is
removed from a header or the version macro is missing (negative controls logged); every STABLE
manifest entry has a section in `API.md`; `API.md` states the version and the compatibility rule.
The manifest check covers the list, not the prose, so `API.md` gets an Opus review against the
headers before merge.

**R2. Hello GPU from a fresh checkout.** (omega-mv#595)
`examples/hello_gpu/`: a vector add written with the kernel builder, launched through the session
(`omega_gpu_session_launch`, on main) and checked on the host. It does not depend on the job engine,
whose chip receipt is still on an unmerged branch. One make target builds it from a fresh clone,
including getting the physics repository at the commit in `physics.lock` without a hand-made
sibling folder. A README section "Run your first kernel" with only those commands.
Reuses: the vector-add kernel code (`src/omega_blackwell_encoder.c`), the session API, the
`physics.lock` checks in the `Makefile`, sovereign-core's `scripts/zero-cuda-gate.sh`.
Done when: a script clones the branch into an empty folder, runs only the README commands, and on
the GB10 prints the hello result with every element checked; the zero-CUDA gate passes on the
binary; log and receipt pushed under `evidence/RUNTIME-R2-HELLO/`. The one chip run is a
correctness check (no timing), taken through `quiet-measure` so it never overlaps a measurement.

**R3. Hardware abstraction note and portability table.** (omega-mv#596)
`docs/runtime/PORTABILITY.md`: which parts of Omega are chip-specific (machine-code encoder,
launch descriptor, class numbers, cache rules) and which are not; the platform contract; the three
backends (Linux open driver today, WSL2 `/dev/dxg` on `rig/gb20x`, native AIENOS planned); what a
new GPU needs. A table of every `src/omega_gpu_*` and `src/omega_blackwell_*` file marked
chip-specific or portable.
Reuses: `src/omega_gpu_platform.h`, `docs/numeric/OMEGA_GPU_NATIVE_BOUNDARY.md`, `rig/gb20x`
`docs/rig/dxg-backend.md`, aien-rig `docs/WINDOWS-GPU-PATH.md`, aienos `docs/GB10_NATIVE_DEPENDENCY_MAP.md`.
Done when: the doc is pushed and `make test-runtime-portability` (host) passes and fails when a new
GPU file is added without a row. That check proves every file has a row, not that each row is
right; each "portable" row names the reason (for example "no Linux header, builds in
`make gpu-engine-freestanding`").

### Wave 2 (in this order)

Order: R4, R13, R5, R6, R10, R7, R8, R9, R11, R12. R4, R9 and R10 need the chip for more than a
correctness check: each books a quiet window (no other sensitive measurement on the machine) and
records the window in its receipt.

**R4. Ceiling probe tool.** One command that measures memory bandwidth, tensor-core peak and
launch cost on this machine and writes one ceilings file that every library receipt cites.
Reuses `tests/gpu_tc_peak_test.c`, `tests/gpu_launch_cost_test.c`. Done when: three runs in one
quiet window agree within 3 percent of their median on each figure (stated now, before any run),
and the file and the three logs are pushed.

**R13. Device query.** Name, SM count, memory size, compute class and SM version through the API,
read from the chip. Reuses physics `forge/forge_descriptor.c` (reads class and SM version live) and
the `rig/gb20x` probe. Done when: `examples/device_query` prints them on the GB10 and they match
the physics hardware-identity receipt (`evidence/M19R/` in physics).

**R5. Kernel authoring guide and a non-AIEN program.** `docs/runtime/KERNELS.md` (the kernel
builder's instructions, registers, shared memory, how to check a kernel on the host simulator, how
to launch) and `examples/image_blur/` (a 3x3 blur on an image file). Reuses the kernel builder, R2's
example layout and `tests/bw_warp_sim*.h`. Done when: the blur runs from a fresh checkout by README
only, matches a host reference bit for bit, and reports its speed as a fraction of the R4 bandwidth
ceiling.

**R6. General GEMM v0.** Any M, N, K in bf16 and FP8 on the tensor cores, row- or column-major.
R6 owns the general API and the choice of kernel for a shape; it uses the existing kernels as they
are and does not tune the model-shaped kernels the cascade streams own (tree-verify, prefill,
w4-gemv). Reuses `src/omega_blackwell_codegen_tc*.c`, `omega_gpu_tcgemm_api`,
`omega_gpu_matmul_api`. Done when: a sweep script over at least 200 shapes (sizes 1 to 8192, both
layouts, both types) matches a host reference within the stated tolerance on every shape, and a
pushed table gives speed as a fraction of the R4 tensor-core ceiling.

**R10. Recovery after a timeout.** Today one uncertain completion ends GPU use for the process;
any outside developer meets this on their first bad kernel. Builds on omega-mv#518. Done when: a
chip test forces a timeout with a kernel that never ends, the runtime recovers (channel reset or
re-created), and a correct launch then succeeds in the same process; receipt pushed.

**R7. General reductions and scans.** Sum, max, min, mean and prefix sum over any length, reusing
the E1 frozen-order reductions (`src/omega_numeric_reduce*.c`). Done when: a sweep over lengths 1
to 2^28 matches the frozen-order host reference bit for bit and reports speed as a fraction of the
R4 bandwidth ceiling.

**R8. Streams and events.** A small public API (queue, record event, wait on event) layered on the
pipelined, dependency-ordered engine from omega-mv#580. Starts after #580 lands. Done when: two
independent queues of the same work finish in less than 0.75 of the sum of their solo times, and
an event wait orders two dependent jobs correctly (checked by data), both on the chip.

**R9. Several processes on one GPU.** Find why a second channel fails (NV_ERR_NO_MEMORY, status
0x51, omega-mv#523) and make the runtime either share the GPU or refuse with a named error. Run
through the `investigate` procedure. Done when: a two-process test on the chip ends with both
results correct, or the second process gets the named error and the first is unharmed.

**R11. Our own sm_121 disassembler** so kernel checks stop calling an NVIDIA tool. Reuses the
encoder's instruction tables. Done when: its output agrees with the NVIDIA disassembler on every
library kernel in `src/` (while that tool is still installed here), and the checks that call the
NVIDIA tool switch to ours.

**R12. Rust binding.** Document sovereign-core's `aien-omega-gpu` as the Rust way in. Done when:
sovereign-core's conformance check passes against Omega's canonical manifest (R1) at a pinned
Omega commit.

### Tracked here, owned elsewhere

- RTX 5070 first launch: branch `rig/gb20x`, ledger L024.
- Pipelined, concurrent, dependency-ordered engine: omega-mv#580, ledger L022.
- Static hazard checker: omega-mv#554.
- Running with no Linux driver: aienos#286 (cut D: channel, doorbell, completion).

### Public face

**P1.** The README says plainly what Omega is for the world (an open-source CUDA alternative),
what works today with receipts, and what does not yet. A short note for www.aienos.com in the same
words. Branch `runtime/readme-public`. Drake reviews the wording before merge.

## 3. Rules for every slice

- **Reuse before you build** (Drake, 2026-10-19). Before any new file, search all aien-dev and
  aien-spark repositories (`gh search code --owner aien-dev`, `--owner aien-spark`), the local clones,
  `~/handoffs`, `ledger list`, and open branches. Each slice names the parts it reuses. Build new only
  when nothing fits, and say why in the handoff.
- Measure against the hardware ceiling from our own probes. No comparison numbers with other
  vendors' software in this repository.
- Every measurement is pushed on the branch, pass or fail. Builds through `quiet-build`, timings
  through `quiet-measure` (GPU lock `/tmp/aien-gb10.lock`). One sensitive measurement at a time on
  the machine. Chip runs are never killed.
- A failing gate means the work is wrong until a cause is found.
- No Python. C and shell, as the rest of Omega.
