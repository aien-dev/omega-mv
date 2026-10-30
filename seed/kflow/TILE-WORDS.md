# Tile words for OMEGESE: proposal (PROPOSED, not implemented)

Status: PROPOSED. No compiler, encoder or table changed for this proposal. The two hand-written listings
`sm121/tmatmul16.s2` and `sm121/tsoftmax.s2` are what these words should generate, instruction for
instruction; they are EXERCISED on the host (kflow encode, verify, package, emulate) and NOT_RUN on a GPU.

## Why words, not a library

Both listings were written by hand from the PTX ISA 9.4 fragment tables. Most of their 46 and 38 instructions (plus a padding NOP each)
are fragment addressing (lane to row and column, offsets, packing two f16 into one register), and the one real
bug found so far was a register clobber in a butterfly (README, "Tile kernels"). Those are exactly the parts a
compiler should own: the writer states the tile shapes, the compiler picks the fragment layout and registers.

## Words

Prefix order and argument conventions as in OM-2. Types: `f16`, `bf16`, `f32` element types; a tile value
is `tile T R C` (rows, columns), held in registers spread over the 32 lanes of a warp. Only shapes the target
has a table row for are accepted; anything else is a compile error, never a silent fallback.

| word | meaning | sm_121 lowering (rows in `seed/sm121/sm121.tbl`) |
| --- | --- | --- |
| `tload T R C P ld` | load an R x C tile of T from global P (row stride `ld` elements) into the fragment layout the next `tmma` needs | `ldg.0`/`ldg.w.1`/`ldg.2` (32-bit pairs along a row), `ldg.u16.2` + IMAD pack (pairs down a column) |
| `tmma A B C` | D = A x B + C, f32 accumulate; A `tile f16 16 16`, B `tile f16 16 N` (N = 8k), C `tile f32 16 N` or `0` | one `hmma.0`/`hmma.f16.2` (bf16: `hmma.bf16.2`) per 8 columns; `0` lowers to RZ |
| `tstore P ld D` | store an f32 tile from the accumulator layout | `stg.0`/`stg.1`/`stg.2` |
| `wmax x` / `wsum x` | the maximum / sum of x over the 32 lanes, the same value in every lane | 5 x (`shfl.0`/`shfl.bfly.1`/`shfl.2` into a fresh register, then `fmnmx.max.2` or `fadd`) |
| `ex2 x` / `rcp x` | approximate 2^x and 1/x (PTX ex2.approx.f32: 2 ulp; rcp.approx.f32: 1 ulp) | `mufu.ex2.2`, `mufu.rcp.2` |

With these, the two kernels are short (sketch, not compiled):

```
fn tmatmul16 a ptr b ptr d ptr to void
ta = tload f16 16 16 a 16
tb = tload f16 16 16 b 16
tstore d 16 tmma ta tb 0
end

fn tsoftmax x ptr y ptr to void
i = add mul ctaid 32 tid
v = get x i
m = wmax v
e = ex2 mul sub v m 1.44269502162933349609375f32
put y i mul e rcp wsum e
end
```

## Required checks before any word is accepted

1. Every row the lowering emits is checked against nvcc/cuobjdump words (not against this emitter), as done for
   the 48 instructions in the README table, and every emitted instruction against `nvdisasm -b SM121`.
2. kemu runs the generated kernel against an f64 reference within a tolerance derived with Wolfram (the
   README gives the two used here), with a negative control per word (wrong fragment layout, transposed B,
   butterfly turned into SHFL.DOWN).
3. The generated listing for the two sketches above is byte-identical to the hand listings, or every
   difference is explained.
4. A chip run on sm_121 in an authorized window before anything is called QUALIFIED.

## Open questions

- Warp-collective words need every lane to reach them together. The language has no notion of a warp yet;
  kemu reports `DIVERGED` when that is broken, but the compiler should reject it statically.
- Tile values span 32 lanes. OM-2 values are per thread, so `tile` needs a type rule (no `get`/`put` on a
  tile, only the tile words).
- Larger shapes (K loops, multiple warps, shared memory staging with LDSM) and an sm_80 lowering (the same
  HMMA.16816 exists there) are follow-ons; so is a CUDA Tile IR round-trip.
