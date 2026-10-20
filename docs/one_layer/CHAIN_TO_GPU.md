# The chain from Omegese to GPU bytes

Ledger L097, 2026-10-19. Read-only map of omega main `a8ffd2b9` and physics `6d7cf0d4` (the commit
pinned in `physics.lock`). Line counts are `wc -l` at those commits. Nothing in `src/` was changed.

Question (Drake): "Why are we using a CPU programming language for a GPU?" The endpoint is the GB10's
own instruction words (16 bytes each, sm_121). The origin is Omegese text (`docs/one_layer/OMEGESE.md`).

## 0. The short answer

1. **An Omegese program does not reach the GPU today.** No file links the Omega program chain
   (Omegese, `omega_emit`, the OSC binary IR, checker2) to the GPU code generator: a search for any
   file that names both `osc_ir`/`c2_ir`/`omega_emit`/`omegese` and the Blackwell IR (`BW_IR_*`)
   finds none. The only Omegese-to-bytes chain is the CPU one (section 2).
2. **Every GPU kernel today is a recipe written in C.** Fifteen `src/omega_blackwell_codegen_*.c`
   files plus the matmul recipes in `src/omega_blackwell_codegen.c` call `omega_bw_ir_append` one
   GPU instruction at a time. C is the kernel language here: the recipe is the program.
3. **C stops before the chip; the driver is only crossed at setup.** Per launch, our own C writes
   every byte the GPU reads (instruction words, launch descriptor, argument bank, method words, queue
   entry) and then one 32-bit store into a GPU register page (`nvrm_ring`, physics
   `nvrm/nvrm.c:745`). No call into NVIDIA's driver happens per launch. The driver (NVIDIA's open
   kernel module 580.173.02, reached by `ioctl`) is used to open the device, allocate and map
   memory, and create the channel (section 3).
4. **Per generated token, the host work is small except sampling** (section 6, measured on the live 27B
   server): 1.38 ms of a 132.85 ms token for greedy requests (0.0104 of wall), 28.18 ms of 166.06 ms for
   sampled ones (0.1697), because sampling sorts the whole 248,320-word vocabulary twice. The host loop is
   Rust; the C per token (record, doorbell, wait, read back) costs microseconds. Greedy tokens also copy
   ~1 MB of scores back to find on the CPU a token the GPU already picked.

So Drake's suspicion is half right. The words on the chip are ours, not a compiler's. But the thing
that writes them is C, and the language that should write them (Omegese, then Omega IR) cannot yet
say "a GPU kernel" at all.

## 1. The GPU path as it exists today

There is no Omegese stage on this path. The chain starts at a C recipe.

| # | stage | what it does | language | lines | input | output | needed by |
|---|---|---|---|---|---|---|---|
| G1 | op API wrapper (`src/omega_gpu_*_api.c`, 12 files) | picks a recipe for one operation (matmul, attention, elementwise, router, MoE, fp8...), sizes buffers, packs host data, records the launch | C | 6,072 | a tensor job from the model engine | a recipe call + buffers | **host**: it is the model engine's calling side, not something the chip reads |
| G2 | kernel recipe (`src/omega_blackwell_codegen_*.c`, 15 files, + matmul recipes in `omega_blackwell_codegen.c:1048-2220`) | appends GPU instructions one by one with virtual registers, unroll and tile choices, branch offsets and a hand-set control word per instruction | C | 4,719 + ~1,170 | a shape struct (e.g. `OmegaMatMulSpec`) | Blackwell IR: up to 1,024 `BlackwellIRInsn`, 62 opcodes (`omega_blackwell_codegen.h`) | **mixed**: the instruction choice and the control word are GPU needs; that they are written as C function calls is host baggage (there is no other language that can say them) |
| G3 | register allocator (`omega_bw_regalloc_solve`, `omega_blackwell_codegen.c:137-261`) | maps virtual registers to R2..R127 and UR4..UR31 by live interval, pairs and quads aligned | C | ~125 | Blackwell IR | IR + register map, peak register count | **GPU**: the chip has a fixed register file; the QMD declares the count (`omega_bw_gpr_declared`) |
| G4 | encoder (`encode_single_insn` + `omega_bw_encode_program`, `omega_blackwell_codegen.c:262-826, 997-1047`) | turns each IR instruction into four 32-bit words, opcodes checked against the sm_121 disassembler offline (`tools/*_nvdisasm_check.sh`, never in the program path) | C | ~615 | IR + register map | **code bytes, 16 per instruction** | **GPU**: this is the format the chip executes |
| G5 | launch descriptor + argument bank (`src/omega_blackwell_qmd.c`) | builds the 96-word (384-byte) QMD: code address, grid, block, register count, constant banks; builds constant bank 0: a 224-word template copied from what NVIDIA's driver writes, patched with addresses, plus the arguments at 0x380 | C | 237 | buffer addresses, grid | 384 B QMD + 1 KB bank per launch | **GPU**: the compute front end reads both. Open question L097-Q2: how many of the 224 template words our kernels read |
| G6 | session / command list (`src/omega_gpu_session.c`, `omega_gpu_cmdlist.c`, `omega_gpu_wait.c`, `omega_gpu_serving.c`, `omega_gpu_platform.c`) | owns the buffers, writes the method words (WAIT_FOR_IDLE, INVALIDATE_SHADER_CACHES, SEND_PCAS: 8 words per launch in PCAS mode, ~200 in INLINE mode), the semaphore release and the completion tail | C | 1,284 | QMDs, banks, code | method words in the ring buffer | **GPU** for the method words; **host** for the bookkeeping (handles, generations, parking on timeout) |
| G7 | submit (physics `nvrm_enqueue` + `nvrm_ring`, `nvrm/nvrm.c:723-753`; `m16/m16_native.c`) | writes one GPFIFO entry, the USERD GPPut pointer, then the channel token into the mapped doorbell register | C | 167 (m16) + ~30 | method words | one 32-bit doorbell store | **GPU**: this store is what starts the chip |
| G8 | completion (`m16_native_wait_marker_ge`, `m16_native.c`) | CPU reads a GPU-uncached marker word every 50 µs until the GPU's semaphore release lands | C | ~20 | marker address | done / timeout | **host**: the GPU does not need to be watched; the CPU needs to know |
| G9 | device setup (physics `nvrm/nvrm.c`, 813 + 119 lines; `src/omega_gpu_platform_linux.c`, 383) | opens `/dev/nvidiactl`, allocates RM objects and classes, allocates and maps memory (cached or uncached), maps the doorbell page, creates the channel and USERD | C, calling NVIDIA's kernel module by `ioctl` | 1,315 | none | mapped buffers, a channel, a doorbell address | **GPU** needs mapped memory and a channel; **host OS** needs the driver to grant them (on Linux only the kernel module may touch the page tables) |
| G10 | chip | runs the 16-byte words | sm_121 words | - | code + QMD + bank | results in memory, semaphore release | GPU |

Engine wrappers not on the minimum path: `src/omega_gpu_engine.c` (464) + `src/omega_blackwell_engine.c`
(663) are a second launcher for single jobs; `src/omega_blackwell_submit.c` (724) is the older vector
executor pinned by the M17/M18 gates.

## 2. The CPU path: Omegese to AArch64 bytes

| # | stage | what it does | language | lines | input | output | needed by |
|---|---|---|---|---|---|---|---|
| C1 | `omegese emit` (`tools/omegese/omegese.c`) | parses Omegese, infers types, names registers and blocks, lowers loops to checker2's shape | C | 2,169 | Omegese text | JSON op list | **writer**: removes the bookkeeping from the AI writer |
| C2 | `omega_emit emit` (`tools/omega_emit/omega_emit.c`) | reads strict JSON, checks every op, writes canonical IR after a round trip | C | 782 | JSON op list | OSC binary IR | **host**: JSON is a text hand-off between two of our own C programs. Neither chip needs it |
| C3 | IR codec + validator (`src/compiler/osc_ir.c`) | encodes, decodes and validates the IR bytes | C | 929 | IR bytes | `OscUnit` in memory | **trust**: the IR bytes are what gets hashed, checked and signed |
| C4 | checker2 (`src/checker2/*.c`, `*.h`) | proves the IR: types, definite assignment, loop bounds, with an LRAT proof and bit-blasting | C | 3,167 | IR bytes | ACCEPT / REFUSE + proof | **trust**, neither chip |
| C5 | receipt (`src/checker2/c2_receipt.c`) | writes the signed receipt binding IR digest and verdict | C | 437 (incl. header) | verdict + digests | receipt file | **trust** |
| C6 | AArch64 back end (`src/compiler/osc_cg.c`, `osc_a64.c`) | one pass: every virtual register lives in a stack slot, operands loaded into x9..x13, result stored back | C | 933 | `OscUnit` | AArch64 code bytes, 4 per instruction | **CPU**: this is the format the CPU executes |
| C7 | runtime (`src/compiler/osc_rt.c`) | alloc, free, traps called from the code | C | 336 | calls | effects | **CPU program** (its memory model) |
| C8 | loader (`src/compiler/osc_native.c`) | `mmap` writable, copy, clear the instruction cache, `mprotect` to executable | C (Linux calls) | 53 | code bytes | callable code | **host OS**: Linux will not run bytes it has not been asked to mark executable |

Side by side:

| | CPU path | GPU path |
|---|---|---|
| origin | Omegese text | a C recipe (no Omegese) |
| stages from origin to bytes | 6 (C1-C6) | 4 (G1-G4) |
| stages from bytes to running | 1 (C8) | 4 per launch (G5-G8) + setup (G9) |
| proof (checker2, receipt) | yes, on the IR | none on the kernel; fixture checks compare encoder words with the disassembler offline |
| written in | C throughout | C throughout |
| driver crossings per run | `mmap` + `mprotect` once | none per launch; `ioctl` at setup only |
| who writes the instruction words | `osc_a64.c` | `encode_single_insn` |

## 3. Where C stops and bytes begin on the GPU path

- **The C that writes GPU instruction words:** `encode_single_insn` (`src/omega_blackwell_codegen.c:262`),
  driven by `omega_bw_encode_program` (`:997`) and `omega_blackwell_kernel_encode_into` (`:1219`). The
  first fixed kernel, `omega_blackwell_encode_vecadd` (`src/omega_blackwell_encoder.c`, 110 lines),
  holds 32 words as constants (sha256 pinned in `omega_blackwell_encoder.h`).
- **What still happens in C after the words exist:** copy into a CODE buffer; build QMD and argument
  bank (G5); write method words (G6); GPFIFO entry, GPPut, doorbell store (G7); poll the marker (G8);
  read results back. All of these are plain loads and stores into memory the CPU already has mapped.
- **The driver boundary, exactly.** Our code ends at physics `nvrm/nvrm.c`. Everything that crosses
  into code we did not write is an `ioctl` on `/dev/nvidiactl` or a device fd, or the matching `mmap`:
  `NV_ESC_RM_ALLOC`, `NV_ESC_RM_FREE`, `NV_ESC_RM_CONTROL`, `NV_ESC_RM_MAP_MEMORY`,
  `NV_ESC_RM_UNMAP_MEMORY_DMA`, `NV_ESC_REGISTER_FD`, and the UVM calls (`nvrm.c:58-180`). These run
  inside `nvrm_open`, `nvrm_alloc*`, `nvrm_free`, `nvrm_channel`, `nvrm_channel_destroy`,
  `nvrm_close`. Behind them sits NVIDIA's kernel module, which boots the GPU's own firmware (GSP),
  owns the page tables and the IOMMU, and hands back mapped addresses. The full responsibility table
  (who owns each today, which AIENOS cut takes it over) is `docs/numeric/OMEGA_GPU_NATIVE_BOUNDARY.md`
  section 2; that map stays the authority and is not repeated here.
- **Per-launch cost of the boundary: zero calls.** `nvrm_ring` is `*rm->doorbell = rm->token`
  (`nvrm.c:746`), a store to the usermode register page mapped once at `nvrm_open` (`nvrm.c:281-285`).

## 4. The shortest C-free chain we can defend

Target chain, five stages:

```
Omegese text --(T1 translate)--> Omega IR bytes --(T2 check + receipt)--> proven IR
   --(T3 lower: select, allocate, control word, encode)--> sm_121 words
   --(T4 launch image: code + QMD + bank + method words, then the doorbell store)--> chip
   (T5 device setup: once, outside the per-program chain)
```

Which stages collapse:

- **C1 + C2 collapse into T1.** The JSON op list exists only so two C programs can talk. Omegese can
  write IR bytes directly; `omega_emit` stays as a checker for the JSON arm of exp445 only.
- **G1 + G2 collapse into "an Omega program that is a kernel".** The recipe stops being C function
  calls and becomes an Omegese function marked as a kernel. Unroll and tile choices become loop
  bounds the lowering unrolls.
- **G3 + G4 collapse into T3**, one pass, the way C6 is already one pass on the CPU side.
- **G5 + G6 + G7 collapse into T4**: building the launch image is writing bytes at known offsets,
  then one store. No logic needs to stay in C.

Which must stay, and the physical reason:

| stays | why (physical) |
|---|---|
| the IR bytes + checker2 + receipt (T2) | the trust chain: what was proven is what runs. Not a GPU need, kept on purpose |
| register allocation and the control word (in T3) | the chip has a fixed register file and the QMD declares the count; the control word carries the scheduling the chip expects with each instruction (exact bit meanings: UNVERIFIED here, see `encode_single_insn`) |
| QMD, argument bank, method words, doorbell (T4) | that is the only door into the compute front end |
| a host waiting for completion | the CPU must know when results are in memory; polling a GPU-uncached word is the rule from the E1 stall (`OMEGA_GPU_NATIVE_BOUNDARY.md` section 2) |
| device setup (T5) | someone must own page tables and the channel. Under Linux only the kernel module may; under AIENOS cut B it becomes ours (NOT_RUN there). Not per program, so not in the chain's stage count |

What the language needs so each remaining stage is written in Omega or emitted as bytes, tied to the
six reasons in `docs/osc/OSC-1-SELF-HOST-STATEMENT.md` (R1-R6):

| need | for stage | self-host reason | status there |
|---|---|---|---|
| byte slices and text input | T1 (read Omegese), T3/T4 (write bytes) | R1 no bytes, strings or text | open |
| structs | IR in memory, QMD layout | R2 | superseded by OSC-2 item 2 (omega#149) |
| large or growable storage (thousands of entries; 1,024 GPU instructions, 480 registers) | T1-T3 | R3 no large storage (64-cell arrays) | open |
| forward calls or recursion | T1 parser | R4 | open (an explicit-stack parser avoids recursion; forward calls still needed) |
| an output channel: write bytes to a buffer the host maps | T3 code buffer, T4 launch image, doorbell store | R5 no output channel | open; for the GPU this is a store to a mapped address, which needs a capability for raw device memory |
| checked contracts | T2 | R6 | superseded by OSC-2 item 1 (omega#148) |
| **GPU-only, not in the self-host list:** a kernel form (per-thread and per-block index values), views over large device memory (beyond 64 elements), float types (f32, bf16, fp8; the IR has only bool and 8-64-bit integers), block barrier and shared memory, the tensor-core multiply | T1-T3 for GPU kernels | none: new list | open; none exists in Omegese or the OSC IR today |

The last row is the real gap behind Drake's question: before the chain can be shortened, Omegese must
be able to say what a GPU kernel says. The C recipes are where that vocabulary lives today
(62 Blackwell IR opcodes).

## 5. How to measure "shortest and fastest" with what we already own

No new metric name. Four measures already in use:

1. **Stage count**, origin to running words (tables above). Today: CPU 7 (C1-C6, C8); GPU no chain
   from Omegese, 8 from a C recipe (G1-G8). Target: 4 per program (T1-T4).
2. **Bytes through each stage** for the same program. Measured for the 10 public exp445 dev tasks on
   the CPU path: `evidence/CHAIN-TO-GPU-0/` (section 5.1).
3. **Tokens at the origin**, the OMEGESE.md section 5 method (Qwen3 tokenizer): 449 tokens for the
   10 dev tasks (417 short form), against 4,210 for the JSON op list. The GPU path has no token
   figure because nothing is written at the origin.
4. **Fraction of the physical ceiling at the endpoint** (Drake's rule). The GPU path built from C
   recipes reaches 0.752 of the memory ceiling on the 27B decode (7.63 tok/s against 10.15; source:
   the site speed inventory, `~/handoffs/2026-10-19-site-all-machines.md`; not re-measured here). An
   Omegese program has no GPU fraction because it cannot run there. The CPU code from C6 has no
   ceiling figure on record (it keeps every value in a stack slot); not measured.

**TURING Yield does not apply here**: it scores held-out bits a learner predicts per joule
(`TURING_YIELD_CURRENT_STATE.md` line 190), and this chain is a lossless translator that predicts
nothing; it applies to whatever learner later runs at the endpoint.

### 5.1 Bytes per stage, CPU path

Measured by `evidence/CHAIN-TO-GPU-0/measure.sh` (output `evidence/CHAIN-TO-GPU-0/bytes.md`), the 10 public
exp445 dev tasks, totals: Omegese text 1,028 B; JSON op list 6,572 B; OSC binary IR built from the Omegese
1,640 B. The hand-written `reference.osc` of the same tasks: IR 1,851 B, AArch64 code 2,776 B. The JSON op list
is the only stage that grows the program (6.4 times the Omegese text); it exists for the host tools, not for
either chip, and is the first stage to collapse in section 4.

## 6. What the CPU does for every generated token today (the live 27B server)

Drake's sharper question: which host code runs per generated token, and what it costs. Source: the server
answering on the Spark now, `aien-serve-http --engine omega` (aien-sovereign-core `qwen38/serve-fast`
`46134247`, linked to omega `qwen38/decode-speed` `0010f7d3`). **The host side of the token loop is Rust, not C;
the C is the GPU driver layer of omega** (recording the command list, ringing, waiting, reading back).

Measured outside the GPU wait, from the server's own per-request log over 128 requests
(`evidence/CHAIN-TO-GPU-1/host-gap.md`, script `host-gap.sh`; "GPU wait" is the host clock from the doorbell
to the completion marker, `omega_gpu_cmdlist.c` submit):

| request kind | requests | tokens | GPU wait ms/token | wall ms/token | host outside the wait ms/token | share of wall |
|---|---|---|---|---|---|---|
| greedy (always the top token) | 108 | 46,580 | 131.47 | 132.85 | **1.38** | 0.0104 |
| sampled (top_k 20, random draw) | 20 | 2,746 | 137.88 | 166.06 | **28.18** | **0.1697** (calc:6f84c8cb) |

Inside the GPU wait, on the 4-layer chip test with stamps (omega `evidence/q38-decode-speed-chip-20261019T0142Z/
default-l4.log`, 41 launches in 1 submit): the GPU's own timeline spans 19,799.3 us, the host-clock wait is
19,826.7 us, so doorbell, front end, marker write and polling add **27.4 us** per submit (calc:48060859); recording
the command list plus copy-in and read-back add **5.6 us** more (calc:cceed405). Not measured at 64 layers.

Per-token steps, in the order they run (one line per step; "us" only where a receipt exists):

| # | step | language, file | what it does | cost per token |
|---|---|---|---|---|
| H1 | sample | Rust, sc `aien-serve-http/src/sampling.rs` + `aien-inference-abi/src/transformer_backend.rs` | greedy: argmax over 248,320 logits. sampled: `top_k_mask` sorts all 248,320 indices and copies them, then the top_p step takes exp in f64 over all 248,320 and sorts all 248,320 again | greedy within the 1.38 ms; **sampled about 26.8 ms more** (28.18 - 1.38, the only difference between the two rows). Fixer **L148** started; open PR #646 removes only the first sort |
| H2 | stop check | Rust, `qwen38_engine.rs` | is the token a stop id | within 1.38 ms |
| H3 | text out | Rust, `server.rs` + `text.rs` | token to UTF-8 text piece (tokenizer decode), one channel send to the HTTP thread; the HTTP write itself runs on another thread, beside the next token | within 1.38 ms |
| H4 | embedding | Rust, `aien-inference-abi/src/qwen38_omega.rs` `step` | 5,120 bf16 values to f32 on the CPU | within 1.38 ms |
| H5 | copy-in + record | C, omega `src/omega_gpu_q38_graph.c` `begin_token`, `record_layer` x64, `record_final` | copy 20,480 B into mapped memory; write one QMD and argument bank per launch for the whole token | 5.6 us at 4 layers (with H8); 64 layers not measured |
| H6 | submit | C, `src/omega_gpu_cmdlist.c` `omega_gpu_cmdlist_submit` | method words into the ring, queue entry, doorbell store (no driver call, section 3) | within the 27.4 us |
| H7 | wait | C, same file `wait_word` + physics `m16/m16_native.c` | spin on the marker word up to 2,000 us, then poll with 50 us sleeps | within the 27.4 us (completion seen up to one poll late) |
| H8 | read back | C, `omega_gpu_q38_graph_read` | sync, then copy all 248,320 logits (993,280 B, calc:246601272) into a new Rust vector | within the 1.38 ms |

What is baggage here, said plainly:

1. **The GPU already picks the greedy token.** The decode graph ends in an `argmax` launch and `end_token` reads
   its answer, but `Qwen38Session::advance` throws it away (`self.model.step(token).and_then(|(_, stats)| ...`)
   and reads the whole logit row back (H8) so the CPU can find the same argmax again (H1). For greedy requests
   (108 of 128 here) H8 and H1 are redundant: about 1 MB copied and 248,320 values scanned per token to learn
   one number the chip already wrote.
2. **Sampling is the one large host cost**, 17% of a sampled token's wall time, and it is two full sorts of the
   vocabulary where 20 values are needed. That belongs on the GPU (a top-k and draw kernel beside `argmax`,
   reading the seed from the argument bank) or at least on 20 values in the engine loop (L148).
3. **Mask:** no grammar mask runs per token in this server today (the Omegese mask is lane L112). When it lands it
   joins H1 and should run where H1 runs.
4. **Tokenizer:** encoding the prompt is once per request; per token only the text-piece decode (H3) runs.
5. **Scheduling:** the chip lock and the session mutex are taken once per request, not per token.
6. **C per token** is H5-H8: microseconds against a 132 ms token. The C there is not the cost; the copy of the
   logits (H8) is, and it exists only to feed H1 on the CPU.

Fraction of the ceiling: the greedy server runs at 132.85 ms/token wall, 0.742 of the 10.15 tok/s memory ceiling (calc:7f4fbf0c; ceiling from
`~/handoffs/2026-10-19-site-all-machines.md`). The host share (0.0104 greedy, 0.1697 sampled) is the part of the
gap to the ceiling that is CPU work rather than GPU time.

## 7. Open questions (ledger lines)

```
L097-Q1 cc-chain-to-gpu  Kernel form for Omegese: which words (thread index, block index, device view, f32/bf16, barrier, shared, mma) and their IR ops | done when: spec section in OMEGESE.md + IR opcodes listed
L097-Q2 cc-chain-to-gpu  Which of the 224 constant-bank template words copied from NVIDIA's driver do our kernels read (0x358 seen in recipes) | done when: list of read offsets per recipe; unread words dropped from the launch image on chip parity
L097-Q3 cc-chain-to-gpu  Does checker2 apply to a GPU kernel (bounds per thread, no data races between threads) or does the GPU path need its own check | done when: decision recorded
L097-Q4 cc-chain-to-gpu  One lowering for both chips: can the IR feed osc_cg (AArch64) and a Blackwell lowering from the same proven bytes | done when: design note with the shared IR subset
L097-Q5 cc-chain-to-gpu  Control word bits per instruction (stall, barriers, yield): documented source or chip-observed only | done when: each bit tagged DOCUMENTED / OBSERVED in the encoder
L097-Q6 cc-chain-to-gpu  CPU back end fraction of ceiling: C6 keeps every value in memory; measure one dev task against the CPU's own ceiling | done when: quiet-measure receipt under evidence/
L097-Q7 cc-chain-to-gpu  Greedy decode reads 993,280 B of logits per token to redo the GPU's argmax on the CPU (qwen38_omega/session.rs advance drops the graph's token) | done when: greedy requests use the graph's token and read logits only when sampling or logprobs are asked; host ms/token re-measured with host-gap.sh
L097-Q8 cc-chain-to-gpu  Sampling on the GPU: top-k + seeded draw as a launch beside argmax, so sampled tokens also skip the logits read | done when: chip test gives the same tokens per seed as the CPU sampler; host ms/token re-measured
L097-Q9 cc-chain-to-gpu  Command-list recording cost at 64 layers (only 4 layers measured: 5.6 us record+copy, 27.4 us doorbell+wait overhead) | done when: GraphStats record_ns/total_ns/chip_ns logged per request by the server or one quiet-measure receipt
```
