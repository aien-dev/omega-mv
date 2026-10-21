# Design review 1 of docs/omegese/GPU.md (c0295f31)

Reviewer: Claude Opus 5.5 (read-only subagent). Fable was asked first and refused twice with its
weekly limit ("You've hit your weekly limit, resets Oct 25"); the brief allows the best model
available. Run 2026-10-21 about 22:30Z. The reviewer read the repo over ssh and changed nothing.

I found 19 problems with the GPU kernel design. Five are blockers: the checker as designed would accept kernels that race, hang or read out of bounds. I read GPU.md at commit c0295f31, plus the IR header, the IR validator and `gen_gate`. That function is not on `omegese/gpu`; I read it from branch `alpha/decode-speed-to-main`. I changed no files and ran nothing.

1. **Blocker, §6 and §9: nothing stops races on shared memory.** The rules only control where `sync` may appear. checker2 would accept `put s 0 thread`, where every thread writes the same slot. It would also accept `put s thread v` followed by `get s add thread 1` with no `sync` between. Both give output that depends on timing. Change: add a rule C2-GPU-SHARED-RACE. Split the code into phases at each `sync`, and follow back edges too, so the gap between the last read of a tile and the next loop's write is covered. Within a phase, a shared array is either read-only, or every write uses an index that gives each thread its own slot and each thread reads only its own slots. Accept a fixed set of index forms: `thread`, `add thread mul k threads` with uniform `k`, and the warp index when guarded by a lane-0 test. Tree reductions then need two arrays used in turn, or `down`.

2. **Blocker, §2, §8 and §9: nothing stops races on global memory.** `put c 0 thread` passes. So does one thread reading `c[j]` that another thread or block writes in the same launch. Change: add C2-GPU-GLOBAL-RACE. Every write index must be a recognised form that is different for every (thread, block, batch). A kernel may read an `edit` buffer only at the indices that same thread writes. `sync` orders reads and writes only within a block.

3. **Blocker, §8: the `same-index` proof ignores the second grid dimension.** `add mul block threads thread` repeats for every `batch`. When `batches > 1`, two blocks write the same element. The rest of the rule also has gaps:
   - "no other thread reads it" cannot be checked as written. Require every load and store on both aliased buffers to use that one index.
   - Allow only exact aliases (same base address, same length, same element type) and refuse partial overlaps.
   - The launcher must refuse `same-index` launches unless `batches == 1`, or the form must include `batch`.

4. **Blocker, §2 and §11: the bounds checks trust a length the caller supplies.** `len` comes from the caller's argument list. Change: the launcher takes each length from its own allocation table, or checks `len*4 <= allocation size`. Otherwise trap 3 protects nothing.

5. **Blocker, §6 and §7: a thread that traps and then hits `down` (or `sync`) can hang.** The planned rule ignores trap paths, so `x = get a i` (which may trap 3) followed by `down` is accepted. On the hardware, a shuffle whose lane mask includes an exited lane is undefined, and that is not covered by the planned measurement. Change: a trapping thread does not exit. It records its code, sets a per-thread poisoned flag, and keeps running with every store disabled; a load returns 0; a loop-bound trap leaves the loop. Uniform values never depend on loaded data, so a poisoned thread still reaches every `sync` and `down` the others reach. Drop the "drain path" idea and the barrier measurement. Define the host reference as all threads in step, reporting the lowest code seen.

6. **Should-fix, §6: the uniformity text in the doc is weaker than the planned checker rule.** It says code is uniform again after a varying `if`, but never says values assigned inside that `if` are varying. Copy the planned rule into the doc. Spell out that:
   - "varying" and "divergent" are computed together and repeated until nothing changes (the IR reassigns registers, so it is not SSA).
   - "trap paths" means blocks from which every path ends in a trap.
   - A `ret` goes to a virtual exit node.

   With those, the rule is sound.

7. **Should-fix, §6: requiring whole-block uniformity for `down` is too strict.** It refuses the standard second stage of a block reduction, `if lt thread 32 { ... down ... }`. Change: add a warp-level uniformity: a value depending on `thread` only through `div thread 32`, with block size a multiple of 32, is warp-uniform, and `down` is allowed in warp-uniform regions. Also allow a shared load at a uniform index in a read-only phase to count as uniform (sound once finding 1 is in). Tiled GEMM and decode attention already fit the planned rule: guarded loads, `sync` at the join, uniform tile loops.

8. **Blocker, §3: the `down` semantics are wrong for partial warps and large distances.** When `threads % 32 != 0`, the lanes past the end of the last warp do not exist. A full-mask shuffle there is undefined; it does not return the thread's own value. The hardware also uses only the low 5 bits of the distance (my recollection of the PTX spec, not checked on the chip), so `down v 33` acts as `down v 1`. Change: the launcher requires `threads % 32 == 0`, and the distance must be a number from 1 to 31.

9. **Should-fix, §3: there is no shuffle that gives every lane the result.** After a `down` reduction only lane 0 holds the total. Softmax in decode attention needs the max and the sum in every lane, which today costs a shared write plus a `sync`. Change: add an all-lanes shuffle (XOR/butterfly, `swap v n`) or a broadcast-from-lane shuffle (`from v k`).

10. **Should-fix, §4: NaN and denormal behaviour is not pinned down, so bit-exact host references can fail.**
    - GPU arithmetic returns the canonical NaN 0x7FFFFFFF. The AArch64 host keeps NaN payloads unless default-NaN mode is set. The host reference must canonicalise.
    - `neg` keeps the payload; say so.
    - FADD and FMUL must be emitted without flush-to-zero.
    - The MUFU fast functions flush denormals to zero, so `inv` of a denormal is +inf. Put that in §5.
    - `ne` must compile to the unordered not-equal compare (FSETP.NEU).

11. **Should-fix, §3 vs `osc_ir.h`: min/max on NaN is described only in the IR comment** ("a NaN operand gives the other operand"). Put it in §3, and add that two NaNs give 0x7FFFFFFF and that `min -0 +0` is -0 (measure the FMNMX instruction).

12. **Should-fix, §4: decimal literals can be rounded twice.** Decimal to double to float can round twice. Require one correctly rounded step (ties to even), or add a hex-bits literal. State whether a leading `-` belongs to the number.

13. **Should-fix, §4: there is no fused multiply-add (`mad`).** Most CUDA kernels need fused multiply-add for accuracy and speed, and the live RMS-norm kernel (`gen_res`) uses FFMA, so it cannot be reproduced bit for bit. Add `mad` in v1. It is a single rounding on both sides (FFMA on the GPU, FMADD on the host).

14. **Should-fix, §2 and §3: the block size is only known at launch.** A tiled GEMM's thread rows and columns need constant `div`/`rem`. The race proofs in findings 1 and 2 need the thread range. The 32-multiple in finding 8 needs it too. Change: `kernel gemm threads 256 ...`. checker2 then knows the exact range, and the launcher enforces it.

15. **Should-fix, §2 and `osc_ir.h`: the 16-parameter limit is really about 8 buffers.** OSC_MAX_KPARAMS (16) counts registers, and each buffer uses two (pointer plus length). Decode attention needs q, K, V, out, a block table, sequence lengths and scalars. Raise it to 32 registers, or count source parameters. Also state that the IR refuses `down` on bool, while the doc says `down` takes "type of v".

16. **Should-fix, overall goal: missing pieces for a "full CUDA replacement".**
    - **Needed in v1:** atomics, unsigned only (add/max/min, which are order-independent), with float atomics refused so results stay deterministic.
    - **Roadmap (C3), but should be named:** bf16, fp16 and fp8 element types with conversion loads; vector loads (the live kernels use 128-bit global loads); tensor-core matrix multiply; sin/cos for RoPE (rotary position embedding); a third grid dimension; uniform runtime divisors.
    - State what happens when a kernel needs more than 255 registers (refuse or spill) and when the 48 KiB shared cap is exceeded (sm_121 allows about 99 KiB).

17. **Should-fix, §5: `root` means 1/sqrt.** A model will read it as sqrt and get silently wrong results. Change: make `root` = sqrt, and define the two-word form `inv root x` as one RSQ operation.

18. **Nit, §3: one-letter word pairs.** `thread`/`threads`, `block`/`blocks` and `batch`/`batches` differ by one letter, and a model's slip type-checks fine. `batch` will also be read as the ML batch. Consider `row`/`rows` for the y index. Check every new word against the Qwen3 tokenizer and save the evidence; the one-token claims are not verified anywhere in the evidence folder.

19. **Nit, §10: the backend refuses nesting deeper than the chip-tested reconvergence depth.** checker2 does not enforce that limit, so a kernel it accepts can still fail to compile. Mirror the cap in checker2.

The gate example itself matches the live kernel. `exp neg g` gives the same bits as the live constant `C_NLOG2E` (0xBFB8AA3B), and `add 1`, `inv` and `mul a` follow the live instruction order. The only differences are the bounds against `len o` instead of a count argument (equal if the launcher sets them so) and the extra load bounds checks, which do not change the output.
