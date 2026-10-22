# Omegese on the GPU: kernels

Status: design v2 (2026-10-21, L386), after review 1 (section 14). Bar row C1 in `docs/OMEGA.md`.
Tool: `tools/omegese/omegese.c`; IR: `src/compiler/osc_ir.h` (encoding version 6); checker:
`src/checker2/`; backend: `src/omega_osc_gpu.c` over the sm_121 kernel builder
(`src/omega_blackwell_codegen.h`), launcher: `src/omega_osc_launch.c`. No second encoder.
Tests: `make test-osc-gpu` (host simulator), `make test-osc-gpu-chip` (the GB10).

## 1. What this adds

Omegese could only write host functions (AArch64). This adds one more function form, the
kernel. A kernel runs once per GPU thread. The same rules hold as for host functions: one word is
one Qwen3 token, types are inferred, control is structured, every loop is bounded, and the same
trust chain checks it (Omegese, then the op list, then canonical IR, then checker2, then a signed
receipt). The backend is new; nothing above the IR changes meaning.

A kernel that checker2 accepts gives the same output bits on every run. That is the design goal
of sections 6 to 9: no data races, no hang at `sync` or `down`, and a trap code that does not depend
on thread timing.

## 2. The kernel line

```
kernel <name> <threads> (<param> <ptype>)*     a kernel; `end` closes it; it returns nothing
```

`<threads>` is a number: the threads in each block, 32 to 1024, a multiple of 32. It is fixed
when the kernel is written, so the checker knows the exact range of `thread`. The launcher uses it
and cannot change it.

Parameter types:

| written | meaning | in the IR |
|---|---|---|
| `view float` / `view uint` | a global buffer the kernel reads | pointer (GVIEW, elem) + hidden u32 length |
| `edit float` / `edit uint` | a global buffer the kernel reads and writes | pointer (GEDIT, elem) + hidden u32 length |
| `float` / `uint` / `bool` | one value, the same in every thread | scalar |

A buffer has no length in the source. The launcher passes it, and reads it from its own table of
allocations, never from the caller (section 11). `len a` reads it (type uint). Every `get`/`put` on a
buffer checks the index against that length (trap 3).

`omegese next` and `omegese complete` (the writing aids) cover host functions only in v1: a kernel is
written whole and given to `omegese emit`. The kernel words (`kernel`, the words of section 3,
`shared`, `sync`) are words only inside a kernel; a host function may still use them as names.
Every fixed word, these included, is one Qwen3 token (`evidence/omegese-gpu/front/tokenizer-gate.txt`).

`own` is refused in a kernel line: a kernel never owns global memory. A kernel has at most 32 IR
parameter registers; a buffer uses two (pointer and length), a scalar one.

## 3. Values a kernel can read

| word | takes | type | meaning |
|---|---|---|---|
| `thread` | 0 | uint | this thread's index in its block, 0 to threads-1 |
| `threads` | 0 | uint | the thread count from the kernel line (a number) |
| `block` | 0 | uint | this block's index along x, 0 to blocks-1 |
| `blocks` | 0 | uint | blocks along x |
| `batch` | 0 | uint | this block's index along y, 0 to batches-1 |
| `batches` | 0 | uint | blocks along y |
| `len` a | 1 | uint | length of buffer or shared array a |
| `down` v n | 2 | type of v | v from the thread n lanes higher in the same warp of 32; n is a number 1 to 31; a lane with no such neighbour gets its own v; v is uint or float |
| `min` / `max` a b | 2 | float or uint | smaller / larger (float rules in section 4) |
| `mad` a b c | 3 | float | a times b plus c, rounded once |
| `exp` x | 1 | float | e^x, approximate (section 5) |
| `inv` x | 1 | float | 1/x, approximate |
| `root` x | 1 | float | the square root of x, approximate |

`inv root x` is read as one operation, 1/sqrt(x) (section 5).

The grid is one-dimensional inside a block (`thread`) and two-dimensional across blocks (`block`,
`batch`). The usual global index is `add mul block threads thread`.

## 4. Types and arithmetic in a kernel

`bool`, `uint` (u32) and `float` (IEEE binary32). The 64-bit types and the signed types are refused
inside a kernel in v1 (checker rule C2-GPU-TYPE). Loop counters, including the hidden ones of `while`
and `for`, are uint.

- `add sub mul` on uint trap 1 when the true result does not fit in 32 bits. On the host these wrap.
  In a kernel they do not, so index arithmetic is exact. The race rules in section 8 depend on this.
- `add sub mul` on float round to nearest even. Denormal inputs and results are kept (no flush to
  zero). The backend never fuses them; only `mad` is fused (one rounding, FFMA on the chip, `fmaf`
  in a host reference).
- `div` and `rem` on float are refused. `div`/`rem` on uint need a number other than 0 as the
  divisor; the backend multiplies by a magic constant and the result is exact.
- `shl`/`shr` on uint: amount 32 or more traps 7 (host rule).
- Comparisons on float are ordered: any comparison with NaN is false, except `ne`, which is true.
- `neg x` on float flips the sign bit and keeps a NaN payload. On uint, `neg` wraps (host rule).
- `min`/`max` on float: if one input is NaN the result is the other; if both are NaN the result is
  the canonical NaN 0x7FFFFFFF; `min` of -0 and +0 is -0 and `max` is +0. This is the chip's FMNMX;
  the chip sweep in `evidence/omegese-gpu/fmnmx/` confirms it before any kernel relies on it.
- Every float op that makes a new NaN gives 0x7FFFFFFF. A host reference canonicalises NaN results
  the same way before it compares.
- Conversions: `float x` from uint rounds to nearest even. `uint x` from float truncates toward
  zero; NaN and values whose truncation is outside 0 to 2^32-1 trap 5. `bool` to or from a number is
  refused (host rule).
- Float numbers in the source: a bare integer in a float place is that integer, and must be exact
  in binary32 (else refused). A number with a point (`1.5`, `-1.442695`) is the decimal rounded once,
  to nearest even, straight to binary32. The leading `-` is part of the number. A point may appear only
inside a kernel (host functions have no float).

## 5. Approximate functions

`exp`, `inv`, `inv root` and `root` use the chip's fast function unit (MUFU). They are deterministic
(the same input gives the same bits on every run) but are not correctly rounded. MUFU treats a
denormal input as zero (so `inv` of a denormal is +inf). The definitions:

- `exp x` = EX2(RN(x * 0x3FB8AA3B)): 2 to the power of (x times log2 e, rounded to binary32).
- `inv x` = RCP(x).
- `inv root x` = RSQ(x), one operation.
- `root x` = RCP(RSQ(x)). This gives 0 at 0 and +inf at +inf.

A host reference for a kernel that uses them states its tolerance before the run, or runs a bit
model of the MUFU unit measured on the chip (`evidence/omegese-gpu/mufu/`).

## 6. Shared memory and sync

```
s = shared float 256         a block-shared array of 256 floats (uint also allowed)
sync                         wait until every thread of the block reaches this line
```

- `shared` is allowed only as the whole value of `<name> =` in the first block of the kernel
  (before any `if`, loop or call). The length is a number, 1 to 12288; the total over all shared
  arrays is at most 48 KiB.
- Every shared array starts zero. The backend zeroes it at kernel start and syncs once, so a read
  before any write sees 0 on every run.
- `get`/`put` on a shared array are bounds checked against the static length (trap 3).
- A shared array name may be read only in its kernel. It cannot be passed, freed, copied or moved.

`sync` and `down` need all threads to reach them together (rule C2-GPU-SYNC-DIVERGENT):

- A value is **varying** when it can differ between threads of a block: `thread`, every `get` (global
  or shared), every `down`, and any value computed from a varying value. A value assigned inside a
  divergent block is varying too.
- A block is **divergent** when some threads of the block may run it and others not: every block
  between a branch on a varying condition and that branch's join, where the join is the nearest block
  every path from the branch reaches. A `ret` (also the one inside a `trap N` line, section 7) goes
  to one virtual exit, so the code after a varying `ret` is divergent until the kernel ends.
- The IR assigns a register more than once, so varying values and divergent blocks are computed
  together and repeated until nothing changes.
- `sync` and `down` are allowed only in blocks that are not divergent.
- Varying regions nest at most 16 deep (rule C2-GPU-NEST). This is the depth the chip tests cover
  (`gen_rc_depth` in `src/omega_gpu_elementwise_api.c`), so a kernel checker2 accepts always fits the
  backend's reconvergence barriers.

## 7. Traps on the GPU

A kernel traps with the host codes: 1 uint overflow, 3 bounds, 4 loop bound, 5 conversion, 7 shift.

The launcher passes a hidden status buffer of 16 u32 words, zeroed before launch. A trapping thread
stores 1 into word N. After the launch the launcher reads the words. The result is the lowest N
whose word is 1, and the output buffers are declared undefined.

There are two kinds of trap:

- **A trap inside an operation** (overflow, bounds, conversion, shift). The thread records the code,
  takes 0 as the result of the operation, and keeps running. From then on it stores nothing: every
  later `put` of that thread, global or shared, is skipped. It still reaches every `sync` and `down`
  the other threads reach, because control at those points never depends on its data.
- **A loop that passes its bound (trap 4).** The thread records the code and leaves the loop. From
  then on it stores nothing, as above.
- **A `trap N` line.** The thread records the code and returns: `trap N` is a trap followed by `ret`.
  checker2 treats it like `ret`: when the branch to it is varying, the code after it is divergent,
  so no `sync` or `down` can follow it.

In the IR a kernel TRAP is an ordinary instruction: the thread records the code, stops storing and
runs on to the end of its block. Only RET ends a thread. So for definite assignment the path through
a TRAP continues (a host TRAP ends the path), and a register left unset on that path is refused.

Why the code is the same on every run: a thread's own steps are fixed by its inputs, and the race
rules (section 8) make every input fixed. The only case where the race rules can fail is when an
index overflowed. That records code 1, which is the lowest code, so the result is 1 on every run.

## 8. Race rules

A kernel is refused if two threads can touch the same element of a buffer or shared array, at a time
not ordered by `sync`, and one of them writes it. checker2 proves this with index expressions.

**Index expressions.** An index is read as a tree through registers that have exactly one
assignment. The leaves are:

- numbers;
- uniform inputs that never change: scalar parameters, buffer lengths, `blocks`, `batches`;
- `thread`, `block` and `batch` (the grid digits);
- `rem thread K` and `div thread K` with K a number (the low and high thread digits for K);
- loop counters (the loop digits): a uint counter set from a number before the loop and increased
  by 1 as the last step of the loop's one back edge. A tree with a loop digit may be used only
  inside that loop. Omegese gives the hidden counters of a kernel's loops the type uint, so they
  are loop digits.

The inner nodes are `add`, `mul`, `div` and `rem`. Anything else makes the tree opaque, and an
opaque tree proves nothing.

**One-to-one indexes.** A tree is one-to-one over its digits when different digit values always
give different indexes:

- a single digit is one-to-one;
- `mul x k` with x one-to-one and k a number other than 0 is one-to-one;
- `add mul x M y` is one-to-one when x and y are one-to-one, they share no digit, and y is always
  less than M. "Less" is proven by the largest value of y (from numbers and digit ranges: `thread`
  is below the kernel's thread count, `rem thread K` below K, `div thread K` below the thread count
  over K, a loop digit below a number count) or by an enclosing `if lt y M` whose two sides are
  the same trees;
- adding a tree with no digit keeps it one-to-one.

`rem thread K` and `div thread K` with the same K together stand for `thread`.

**Fixed digits.** Inside `if eq D c`, where D is a grid digit or thread digit and c a number, D has
the fixed value c. A fixed digit counts as covered, but only for a pair of accesses that both have
the same fixed value.

**Global buffers (C2-GPU-GLOBAL-RACE).** For every `edit` buffer that the kernel writes, every `get`
and every `put` on it must use the same index tree E. E must be one-to-one and must cover `thread`,
plus `block` if the kernel reads `block`, plus `batch` if it reads `batch`. The launcher refuses a
launch with more than one block along x or y when the kernel does not read `block` or `batch`.
`view` buffers are never written, so any index is allowed.

**Shared arrays (C2-GPU-SHARED-RACE).** The `sync` lines cut the kernel into phases. Two accesses can
run at the same time only if a single `sync` (or the kernel start) reaches both of them without
passing another `sync`. checker2 computes that set of starting points for every access, following
loop back edges. For every pair of accesses to one shared array that can run at the same time,
where at least one is a `put` (a `put` is paired with itself), both must use the same index tree E,
one-to-one and covering `thread`.

At most 63 `sync` lines in a kernel. This bounds the phase sets.

**Aliasing.** The launcher refuses a launch where an `edit` buffer overlaps any other buffer. To
write a result into an input, declare the input `edit` and use the same index (the 35B gate in
section 12 does this).

## 9. The IR (encoding version 6)

Version 6 is used only when a unit has a kernel; units without one keep versions 1 to 5 and their
bytes. Additions:

- Each function carries a kind byte: 0 host, 1 kernel. A kernel then carries its thread count
  (u16, 32 to 1024, a multiple of 32). A kernel returns VOID and has no `requires` or `ensures`.
  Host code cannot call a kernel; a kernel cannot call any function in v1.
- Scalar F32. Pointer scalars GVIEW and GEDIT (elem U32 or F32), each followed by its u32 length
  register, as slices are (OSC_T_BYTES).
- REF kind BLOCK: a shared array (elem U32 or F32, len 1 to 12288).
- New instructions:
  - `GPU` sub THREAD/BLOCK/BLOCKS/BATCH/BATCHES: dst u32. (`threads` lowers to CONST.)
  - `SHARED`: dst = a zeroed shared array; entry block only.
  - `GLOAD` a b: dst = buffer a at index b (u32), bounds against a+1.
  - `GSTORE` a b c: buffer a (GEDIT) at b = c.
  - `SYNC`.
  - `SHFL` sub DOWN a b: dst = down(a, b); b is a register with one assignment, a CONST 1 to 31.
  - `MAD` a b c: dst = a * b + c, F32, rounded once.
- New BIN subs MIN and MAX; new UN subs EXP, INV, RSQ, SQRT (F32 only).
- CAST is extended to u32 to and from f32; CMP to f32.
- LOAD and STORE work on BLOCK refs; ALLOC, RELEASE and slices are refused in kernels.

## 10. checker2 rules for kernels

Restated independently in `src/checker2/` (as for host code):

- C2-GPU-TYPE: only bool, u32, f32, GVIEW/GEDIT, BLOCK refs in a kernel; none of them in a host
  function.
- C2-GPU-KERNEL: a kernel returns VOID, has no contract, and has 32 to 1024 threads, a multiple of
  32.
- C2-GPU-SYNC-DIVERGENT and C2-GPU-NEST: section 6.
- C2-GPU-SHARED: SHARED only in the entry block, once per register; total bytes at most 49152.
- C2-GPU-BUFFER: a buffer register is a parameter and is never assigned.
- C2-GPU-STORE-VIEW: no GSTORE to a GVIEW buffer.
- C2-GPU-CALL: no call into or out of a kernel.
- C2-GPU-DIV: uint div/rem need a CONST divisor that is not 0.
- C2-GPU-SHFL: the distance is a CONST 1 to 31.
- C2-GPU-GLOBAL-RACE and C2-GPU-SHARED-RACE: section 8.
- The host rules all still apply: definite assignment, loop bounds, types per op.

## 11. Backend and launcher

`src/omega_osc_gpu.c` reads a checked unit and builds one BlackwellIRProgram per kernel, then
calls `omega_bw_regalloc_solve` and `omega_bw_encode_program`. The host code generator
(`osc_cg`) and the interpreter (`osc_interp`) refuse a unit that has a kernel.

- Arguments live in constant bank 0 from 0x380: for each parameter in order, a buffer is a 64-bit
  address then a 32-bit length; a scalar is 32 bits. Then the hidden args: status address (64 bit),
  blocks, batches (32 bit each). 8-byte values are 8-byte aligned.
- Each IR block becomes a run of builder instructions. Branches become BRA with a relative target,
  patched after layout. A bool is kept as 0/1 in a register; a CBR is a compare then @P0 BRA.
- Reconvergence. A branch on a varying value whose nearest join J is a block opens a region: its
  members are the blocks reachable from the branch without passing J, and its head is the one block
  every edge into the members enters (the branch block, or the loop head when the branch sits in a
  loop that J is outside of). Each block is laid out as [joins][arms][body][branches]: one BSYNC per
  region that joins at this block (innermost first), then one BSSY per region whose head it is
  (outermost first). An edge lands on the first join of its target that it belongs to, else on the
  first arm it is outside of, else on the body. A branch whose paths meet only at the virtual exit
  opens no region (the lanes that return exit, the rest stay together); the kernel is refused if a
  region head can be reached after such a branch. Regions that overlap without nesting are refused.
  Uniform branches need none.
- Barrier registers: two regions get different ones when one contains the other or both lie in a
  common region; otherwise they share. At most 16 (rule C2-GPU-NEST keeps checked kernels inside).
- Liveness is computed over the CFG (loops included) and passed to the builder's allocator as
  intervals over the final layout. A kernel that needs more than the allocator's registers is
  refused with a message; v1 does not spill.
- Scheduling follows the rules the elementwise emitter learned on the chip
  (`src/omega_gpu_elementwise_api.c`). v1 waits on everything pending before each instruction.
  Correct first; speed is C3's job.
- A trap inside an operation is a compare, a store of 1 to the status word, and setting the thread's
  poison flag; every `put` is predicated on the flag being clear.

`omega_kernel_launch` (`src/omega_osc_launch.c`) takes the receipt, the code, the buffers, the scalars
and the grid. Its checks live in `omega_kernel_launch_words` (`src/omega_osc_launch_args.c`, no device
calls), so the host tests prove each refusal. It:

- refuses code whose SHA-256 differs from the receipt;
- refuses a launch whose argument kinds or count differ from the kernel line, or a bool other than 0 or 1;
- takes each buffer as a range (start, element count) inside one session allocation (`OmegaGpuMem`,
  whose size the session recorded when it allocated it), and refuses a range past its end;
- refuses overlaps that involve an `edit` buffer;
- refuses more than one block along x (or y) when the kernel never reads `block` (or `batch`);
- zeroes the status words, launches through `omega_gpu_session_launch` with a stated timeout, then
  reads the status words and reports the trap code or 0.

## 12. Examples

Vector add:

```
kernel vadd 256 a view float b view float c edit float
i = add mul block threads thread
if lt i len c
put c i add get a i get b i
end
end
```

The 35B gate, in place (a = a * sigmoid(g)):

```
kernel gate 256 a edit float g view float
i = add mul block threads thread
if lt i len a
put a i mul get a i inv add exp neg get g i 1
end
end
```

The live kernel writes its output over its first input, so the Omegese version declares that input
`edit` and reads and writes it at the same index. `exp neg x` gives the same bits as the live
kernel's EX2(x * -log2 e): negation is exact and RN((-x) * L) = -RN(x * L).

## 13. Done-when (C1, from OMEGA.md)

Vector add, a tiled GEMM and one decode-attention kernel, written in Omegese, pass checker2 and run
on the GB10 with outputs equal to a host reference (bit for bit, or a tolerance stated before the
run); a broken kernel is refused; one kernel of the live 35B decode path (the gate) is swapped for
its Omegese version on a test server with identical golden tokens.

## 14. Review 1 (2026-10-21)

The brief asks for a Fable review before building past vector add. Fable refused twice with its
weekly limit, so Claude Opus 5.5 reviewed design v1 (commit c0295f31) read-only. Its full text is
`evidence/omegese-gpu/review/design-review-1.md`. It found 19 problems, 5 of them blockers.

| # | severity | finding | what changed |
|---|---|---|---|
| 1 | blocker | nothing stopped races on shared memory | C2-GPU-SHARED-RACE (section 8) |
| 2 | blocker | nothing stopped races on global memory | C2-GPU-GLOBAL-RACE (section 8) |
| 3 | blocker | the `same-index` alias fact ignored `batch` and partial overlaps | `same-index` removed; an `edit` buffer may overlap nothing; in place is written as one `edit` buffer |
| 4 | blocker | bounds checks trusted a caller-supplied length | the launcher takes lengths from its allocation table |
| 5 | blocker | a thread that traps and exits can hang `sync` or `down` | traps inside an operation keep the thread running with stores off; `trap N` ends the thread and counts as `ret` for divergence (section 7); the drain path and its measurement are dropped |
| 6 | should-fix | the doc's uniformity text was weaker than the checker | section 6 now states the full rule |
| 7 | should-fix | whole-block uniformity is too strict for `down` in a warp-0 stage | deferred: v1 reductions run `down` in every warp, then read the warp results from shared memory; the C1 kernels need nothing more |
| 8 | blocker | `down` was undefined for partial warps and large distances | thread count is a multiple of 32; the distance is a number 1 to 31 (C2-GPU-SHFL) |
| 9 | should-fix | no shuffle gives every lane the result | deferred: the builder has no butterfly shuffle and the C6 oracle is not yet in place to check a new encoding; v1 broadcasts through shared memory |
| 10 | should-fix | NaN and denormal behaviour not pinned | section 4: canonical NaN, no flush to zero for add and mul, `ne` is unordered; section 5: MUFU flushes denormal inputs |
| 11 | should-fix | min/max NaN rules only in a code comment | section 4, to be confirmed by a chip sweep |
| 12 | should-fix | decimal literals could round twice | one rounding straight to binary32; the leading `-` is part of the number |
| 13 | should-fix | no fused multiply-add | `mad` added (IR op MAD) |
| 14 | should-fix | the thread count was known only at launch | the thread count is part of the kernel line |
| 15 | should-fix | 16 parameter registers is only 8 buffers | raised to 32 registers; `down` on bool is refused and the doc says so |
| 16 | should-fix | pieces missing for a full CUDA replacement | deferred to the C3 roadmap, none is needed by a C1 kernel: atomics, bf16/fp16/fp8, vector loads, tensor-core matrix multiply, sin and cos, a third grid axis, divisors known only at run time, more than 48 KiB of shared memory. Too many registers is refused, not spilled (section 11) |
| 17 | should-fix | `root` read as 1/sqrt would be misread | `root` is sqrt; `inv root` is one RSQ |
| 18 | nit | one-letter word pairs, unverified token counts | words kept (they are the words a model already uses for these things); every new word is checked against the Qwen3 tokenizer and the result saved in `evidence/omegese-gpu/tokens/` |
| 19 | nit | the backend nesting cap was not in checker2 | C2-GPU-NEST, cap 16 |
