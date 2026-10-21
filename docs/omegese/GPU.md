# Omegese on the GPU: kernels

Status: design v1 (2026-10-21, L386). Bar row C1 in `docs/OMEGA.md`.
Tool: `tools/omegese/omegese.c`; IR: `src/compiler/osc_ir.h` (encoding version 6); checker:
`src/checker2/`; backend: `src/compiler/osc_gpu.c` over the sm_121 kernel builder
(`src/omega_blackwell_codegen.h`). No second encoder.

## 1. What this adds

Omegese could only write host functions (AArch64). This adds one more function form, the
kernel. A kernel runs once per GPU thread. The same rules hold as for host functions: one word is
one Qwen3 token, types are inferred, control is structured, every loop is bounded, and the same
trust chain checks it (Omegese, then the op list, then canonical IR, then checker2, then a signed
receipt). The backend is new; nothing above the IR changes meaning.

## 2. The kernel line

```
kernel <name> (<param> <ptype>)*      a kernel; `end` closes it; it returns nothing
```

Parameter types:

| written | meaning | in the IR |
|---|---|---|
| `view float` / `view uint` | a global buffer the kernel reads | pointer (GVIEW, elem) + hidden u32 length |
| `edit float` / `edit uint` | a global buffer the kernel reads and writes | pointer (GEDIT, elem) + hidden u32 length |
| `float` / `uint` / `bool` | one value, the same in every thread | scalar |

A buffer has no length in the source. Its length is passed at launch and read with `len a`
(type uint). Every `get`/`put` on a buffer checks the index against that length (trap 3).
`own` is refused in a kernel line: a kernel never owns global memory.

Two buffers of a launch may be the same memory (the 35B gate kernel writes into one of its
inputs). The launcher allows that only when every alias pair is view/view, or the kernel reads and
writes element i only in thread i of the same pass (section 8 states how this is checked).

## 3. Values a kernel can read

| word | takes | type | meaning |
|---|---|---|---|
| `thread` | 0 | uint | this thread's index in its block, 0..threads-1 |
| `threads` | 0 | uint | threads per block (set at launch, 1..1024) |
| `block` | 0 | uint | this block's index along x, 0..blocks-1 |
| `blocks` | 0 | uint | blocks along x |
| `batch` | 0 | uint | this block's index along y, 0..batches-1 |
| `batches` | 0 | uint | blocks along y |
| `len` a | 1 | uint | length of buffer or shared array a |
| `down` v n | 2 | type of v | v from the thread n lanes higher in the same warp of 32; a lane with no such neighbour gets its own v |
| `min` / `max` a b | 2 | float or uint | smaller / larger |
| `exp` x | 1 | float | e^x, approximate (section 5) |
| `inv` x | 1 | float | 1/x, approximate |
| `root` x | 1 | float | 1/sqrt(x), approximate |

The grid is one-dimensional inside a block (`thread`, `threads`) and two-dimensional across blocks
(`block`, `batch`). The usual global index is `add mul block threads thread`.

## 4. Types in a kernel

`bool`, `uint` (u32) and `float` (IEEE binary32). The 64-bit types and the signed types are refused
inside a kernel in v1 (checker rule C2-GPU-TYPE). Loop counters are uint.

- `add sub mul` on float round to nearest even. They are never fused into one multiply-add: the
  backend emits FADD/FMUL as written (FFMA only for `mad`, not in v1).
- `div` and `rem` on float are refused. `div`/`rem` on uint need a number as the divisor
  (refused otherwise); the backend multiplies by a magic constant and the result is exact.
- `shl`/`shr` on uint: amount >= 32 traps 7 (host rule).
- Comparisons on float are ordered: any comparison with NaN is false, except `ne`, which is true.
- `neg x` on float flips the sign bit (exact). On uint it wraps (host rule).
- Conversions: `float x` from uint rounds to nearest even. `uint x` from float truncates toward
  zero; NaN and values whose truncation is outside [0, 2^32) trap 5.
  `bool` <-> number is refused (host rule).
- Float numbers in the source: a bare integer in a float place is that integer, and must be exact
  in binary32 (else refused). A number with a point (`1.5`, `-1.442695`) is the decimal rounded to
  the nearest binary32. The point costs tokens; integers are preferred where they suffice.

## 5. Approximate functions

`exp`, `inv` and `root` are the chip's fast function unit (MUFU). They are deterministic (the same
input gives the same bits on every run) but are not correctly rounded. Their definitions are:

- `exp x` = EX2(RN(x * 0x3FB8AA3B)), i.e. 2 to the power of (x times log2 e rounded to binary32).
- `inv x` = RCP(x). `root x` = RSQ(x).

The host reference of a kernel that uses them runs the same formula through a bit model of the
MUFU unit, or the run states a tolerance before it starts. The model is measured, not assumed:
`evidence/omegese-gpu/mufu/` holds the chip sweep that defines it.

## 6. Shared memory and sync

```
s = shared float 256         a block-shared array of 256 floats (uint also allowed)
sync                         wait until every thread of the block reaches this line
```

- `shared` is allowed only as the whole value of `<name> =` in the first block of the kernel
  (before any `if`, loop or call). The length is a number, 1..12288; the total over all shared
  arrays is at most 48 KiB.
- Every shared array starts zero. The backend zeroes it at kernel start (each thread clears a
  stride) and syncs once, so a read before any write sees 0 on every run.
- `get`/`put` on a shared array are bounds checked against the static length (trap 3).
- A shared array name may be read only in its kernel. It cannot be passed, freed or moved.

`sync` and `down` need all threads to reach them together:

- **C2-GPU-SYNC-DIVERGENT**: `sync` is allowed only in a block that every thread of the block
  reaches (uniform control). `down` is allowed only where the whole warp is together, which v1
  checks with the same rule.
- A branch is uniform when its condition depends only on uniform values: numbers, scalar
  parameters, `threads`, `blocks`, `block`, `batch`, `batches`, `len`, and ops on uniform values.
  `thread`, every `get`, and `down` are varying.
- Code after an `if` with a varying condition is uniform again at the join. Code after a varying
  `ret` (a thread leaves early) is varying until the kernel ends.
- A trap path ends the thread; it does not make the code after it varying (section 7).

## 7. Traps on the GPU

A kernel traps with the host codes: 3 bounds, 4 loop bound, 5 conversion, 7 shift. `trap N` in the
source works too.

The launcher passes a hidden status buffer of 16 u32 words, zeroed before launch. A trapping thread
stores 1 into word N and ends. Other threads run on. After the launch the launcher reads the words;
the result is the lowest N whose word is 1 (so the code does not depend on thread timing), and the
output buffers are declared undefined.

A thread that ends while others wait at `sync`: whether the chip counts it as arrived is measured
before the design relies on it (`evidence/omegese-gpu/trap-sync/`). If it does not, a trapping
thread instead jumps to a drain path that takes part in every later `sync` and stores nothing.

## 8. The IR (encoding version 6)

Version 6 is used only when a unit has a kernel; units without one keep versions 1..5 and their
bytes. Additions:

- Each function carries a kind byte: 0 host, 1 kernel. A kernel returns VOID and has no `requires`
  or `ensures`. Host code cannot call a kernel; a kernel cannot call any function in v1.
- Scalar F32. Pointer scalars GVIEW and GEDIT (elem U32 or F32), each followed by its u32 length
  register, as slices are (OSC_T_BYTES).
- REF kind BLOCK: a shared array (elem U32 or F32, len 1..12288).
- New instructions:
  - `GPU` sub THREAD/THREADS/BLOCK/BLOCKS/BATCH/BATCHES: dst u32.
  - `SHARED`: dst = a zeroed shared array; entry block only.
  - `GLOAD` a b: dst = buffer a at index b (u32), bounds against a+1.
  - `GSTORE` a b c: buffer a (GEDIT) at b = c.
  - `SYNC`. `SHFL` sub DOWN a b: dst = down(a, b).
- New BIN subs MIN and MAX; new UN subs EXP, INV, ROOT (F32 only).
- CAST is extended to u32 <-> f32; CMP to f32.
- LOAD and STORE work on BLOCK refs; ALLOC, RELEASE and slices are refused in kernels.

Aliasing: checker2 issues the receipt for the kernel as written. The launcher refuses a launch
where two buffers overlap and either is GEDIT, unless the kernel's receipt carries the fact
`same-index` (every GSTORE to a buffer is at an index equal to `add mul block threads thread`
or the same thread's GLOAD index, and no other thread reads it). v1 proves `same-index` only for
the simple form the gate kernel uses; any other overlap is refused.

## 9. checker2 rules for kernels

Restated independently in `src/checker2/` (as for host code):

- C2-GPU-TYPE: only bool, u32, f32, GVIEW/GEDIT, BLOCK refs in a kernel.
- C2-GPU-SYNC-DIVERGENT: as in section 6.
- C2-GPU-SHARED: SHARED only in the entry block; total bytes <= 49152.
- C2-GPU-STORE-VIEW: no GSTORE to a GVIEW buffer.
- C2-GPU-CALL: no call into or out of a kernel.
- C2-GPU-DIV: uint div/rem need a CONST divisor that is not 0.
- The host rules all still apply: definite assignment, loop bounds (any integer counter type),
  types per op.

## 10. Backend

`src/compiler/osc_gpu.c` reads a checked unit and builds one BlackwellIRProgram per kernel, then
calls `omega_bw_regalloc_solve` and `omega_bw_encode_program`.

- Arguments live in constant bank 0 from 0x380: for each parameter in order, a buffer is a 64-bit
  address then a 32-bit length; a scalar is 32 bits. Then the hidden args: status address (64 bit),
  threads, blocks, batches (32 bit each). 8-byte values are 8-byte aligned.
- Each IR block becomes a run of builder instructions. Branches become BRA with a relative target,
  patched after layout. A bool is kept as 0/1 in a register; a CBR is ISETP.NE then @P0 BRA.
- Reconvergence: every varying conditional gets BSSY before the branch and BSYNC at its join
  (immediate post-dominator); a loop gets BSSY before the head and BSYNC at its exit. Uniform
  branches need none. The nesting depth is capped at the depth the chip tests cover.
- Liveness is computed over the CFG (loops included), then passed to the builder's allocator as
  intervals over the final layout, so a value live around a back edge stays in its register.
- Scheduling follows the rules the elementwise emitter learned on the chip
  (`src/omega_gpu_elementwise_api.c`): variable-latency results set a scoreboard and the consumer
  waits; fixed-latency ops stall 6; a compare stalls 14 before a predicated use; v1 serialises
  (each instruction waits on everything pending). Correct first; speed is C3's job.
- Bounds checks, shift checks and conversion checks become a compare and a predicated branch to the
  block's trap path.

## 11. Launcher

`omega_kernel_launch` takes the receipt, the code, the buffers (device address + length), the
scalars, threads and the grid. It refuses a launch whose argument kinds or count differ from the
kernel line, zeroes the status words, launches through `omega_gpu_session_launch` with a stated
timeout, then reads the status words and reports the trap code or 0.

## 12. Examples

Vector add:

```
kernel vadd a view float b view float c edit float
i = add mul block threads thread
if lt i len c
put c i add get a i get b i
end
end
```

The 35B gate (out = a * sigmoid(g)):

```
kernel gate a view float g view float o edit float
i = add mul block threads thread
if lt i len o
put o i mul get a i inv add exp neg get g i 1
end
end
```

`exp neg x` gives the same bits as the live kernel's EX2(x * -log2 e): negation is exact and
RN((-x) * L) = -RN(x * L).

## 13. Done-when (C1, from OMEGA.md)

Vector add, a tiled GEMM and one decode-attention kernel, written in Omegese, pass checker2 and run
on the GB10 with outputs equal to a host reference (bit for bit, or a tolerance stated before the
run); a broken kernel is refused; one kernel of the live 35B decode path (the gate) is swapped for
its Omegese version on a test server with identical golden tokens.
