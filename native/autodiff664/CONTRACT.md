# Native reverse-mode tape (bounded) — `native/autodiff664/tape.om`

Status: IMPLEMENTED and EXERCISED on the host (aarch64 Linux, DGX Spark). Not
QUALIFIED on hardware, not MERGED, not DEPLOYED. Tracks omega-mv #664 (native
autograd) and builds on #663 / PR #674 (`native/ecosystem663/metadata.om`).

## What it is

A handwritten seeded `.om` reverse-mode tape for rank-2 IEEE binary32 tensors.
Values and gradients use native `f32` words. It contains no FFI, no C fallback
and no generated code. Build order:
`native/ecosystem663/metadata.om`, `native/autodiff664/tape.om`, then the consumer.

| word | meaning |
|---|---|
| `tpreset` | empty tape (node count, backward flag, arena offset to 0) |
| `tpleaf r c rg` → id | leaf `[r,c]`; `rg` 1 requires grad |
| `tpset n i v` → 0 | write leaf/node value element `i` |
| `tpadd a b`, `tpsub a b`, `tpmul a b` → id | elementwise, equal shapes |
| `tpmm a b` → id | `[m,k]·[k,n]` |
| `tprelu a` → id | `x > 0 ? x : +0` |
| `tpbcast a rows` → id | `[1,c]` → `[rows,c]` |
| `tpsum a axis` → id | axis 0 → `[1,c]`, axis 1 → `[r,1]` (keepdims) |
| `tpback root` → 0 | reverse pass, seed 1.0, once per tape |
| `tpval n i`, `tpgrad n i`, `tphasg n` | read value / gradient / whether a gradient exists |

## Admission and limits

Each recorded node has an M20 metadata-v1 descriptor (`TD`) and an allocator
snapshot (`TS`). Every operand passes `emcheck` from `metadata.om` before an op
runs, so a stale or non-live operand is refused with the metadata error.
The `tests/autodiff664` `tstale` check exercises this path.

| code | condition |
|---|---|
| −30 | capacity: 33rd node, >64 elements, reduced axis >32, arena full |
| −31 | `tpback` already run or failed, or any op after `tpback` |
| −32 | bad id, shape mismatch, bad layout, bad axis |
| −33 | `tpback` root not `[1,1]` or not requiring grad |
| −1..−8 | passed through from `emcheck` |
| exit 97 | `tpval`/`tpgrad` out of range or on a node without a gradient |

A refusal records nothing (the node count is unchanged).

## Numeric order (the contract the fixture oracle checks)

- **Reductions** use one E1 tile: copy the n ≤ 32 inputs, pad lanes n..31 with
  −0.0, then for d = 16, 8, 4, 2, 1 set `t[i] = t[i] + t[i+d]` for `i < d`.
  This mirrors `src/omega_numeric_reduce.c` `cpu_sum_like` (line 124;
  `OMEGA_REDUCE_SUM_PAD_BITS`).
- **Matmul** output `(i,j)` takes the products `A[i,q]·B[q,j]` in q order, then the
  E1 tile. This is the M20 matmul v1 order (FMUL_RNE products, then the E1 tree).
  Backward: `gA = g·Bᵀ` (products `g[i,j]·B[q,j]` in j order) and
  `gB = Aᵀ·g` (products `A[i,q]·g[i,j]` in i order), each followed by the E1 tile.
- **Backward walk** (`src/autodiff/omega_autodiff.c` `omega_ad_backward`,
  line 511): nodes in decreasing index order, `in0` before `in1`. The first
  contribution to a gradient is stored as is; later contributions are added as
  `grad + contrib` (`accumulate`, line 107). Because there is no zero
  initialisation, signed zeros match the C tape bit for bit.
- **Rules:** SUB `in1` takes `neg g`. MUL takes `g·value(other)`. RELU routes
  `g` only where `x > 0`, so `−0`, `+0` and NaN give `+0`; this matches
  `max_routes_to_a` (line 148) against a zeros operand. BROADCAST sums `g` over
  rows with the E1 tile. SUM spreads `g` along the reduced axis.
- **No contraction:** every `a·b + c` is written as `add (mul a b) c`. The
  pinned compiler emits no fused multiply-add for it. `tape-test.om` `tnofma`
  proves this: with `c = −round(a·b)` the result is exactly +0 on all 64
  trials, while an f64 widening of the same expression is nonzero on at least
  one trial.

## Evidence (host)

The pinned compiler is omega-mv `seed/out/omc`, SHA-256
`62775d2a391cfef7cde64e387655caa9809a267093112910ec76bfa568f3712b`.

```sh
OMC=<pinned omc> OUT=<scratch> sh tests/autodiff664/run-tape.sh
# TAPE_SUITE_PASS NATIVE_TAPE_PASS checks=302
```

- `tests/autodiff664/tape-test.om` (302 checks). Expected values come from
  separate code paths in the test: a test-local E1 tile, a sequential sum,
  IEEE identities (`x·x` grad is `x + x`, SUB grad is `neg s`) and the f64
  widening control. No expected value is a hand-entered constant. `torder`
  requires at least one input where the tree sum differs from the sequential
  sum, and requires the tape to follow the tree on every input.
- `tests/autodiff664/mutate.om`: three compiled source mutants of `tape.om`
  (SUB sign dropped, MUL operand swapped to `value(in0)`, RELU tie `gt`→`ge`).
  Each compiles with rc 0 and makes the test exit 1, at checks 212, 66 and 171.
- `tests/test_omega_ad_fixture.c` (`make test-autodiff-fixture FIXTURE=…`)
  replays one MLP step written by the native trainer (aien-train
  `tests/adtrain`, mode D) on the C M21 tape over the M20 CPU table.
  Observed: `exact=75/75 max_ulp=0 verdict=PASS`. The `--mutant-sign` control
  fails as required. The C tape is a disclosed test oracle only; nothing in the
  native tape calls C.
- The fixture is 187 u32 words, 748 bytes: a 6-word header, 106 inputs and 75
  expected values. The plan's "179 words" was an arithmetic slip.
- The GB10 arm is `tests/run_ad_chip.sh` (`GB10_CHIP_RUN=1`,
  `/tmp/aien-gb10.lock`, physics at `physics.lock`). It needs an explicit GPU
  window. Until a receipt exists, it is NOT_RUN.

## Not claimed

General autograd (rank >2, more than 32 nodes, DIV/SQRT/MEAN/TRANSPOSE), GPU
launch from `.om` (#638 / CUDApp #35), performance, atomic checkpoint
publication, or any hardware qualification.
