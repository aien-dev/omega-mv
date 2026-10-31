# seed/kflow/: KernelFlow, one tool from "I need a kernel" to a receipt

Status: milestone 1 IMPLEMENTED and EXERCISED on the host (the Spark, arm64). No GPU ran anything from it:
every gpu and cloud stage is a queued job file with status NOT_RUN. Nothing here is QUALIFIED.

All three programs are seeded Omega (OM-2), built by the pinned `seed/out/omc`. `nvdisasm` and `cuobjdump`
(CUDA 13.0) are read-only oracles; nothing they print is copied into an output.

| file | what it is |
| --- | --- |
| `kflow.om` | the conductor: reads a spec, runs the stages, caches them, writes receipts and queue files |
| `kemu.om` | host SASS emulator for the instructions these kernels use (sm_121 and sm_80); harnesses `vadd`, `radd`, `tmm1`, `tmmt`, `tsmx` |
| `kpack.om` | cubin packer, a port of CUDA++ `src/omega_cubin.c` (aien-dev/CUDApp 853e814d) |
| `m1.spec` | milestone 1: 4 jobs and 3 negative controls, 31 expectations |
| `sm121/resadd.s2`, `sm121/resadd.c.bin` | 35B decode RES ADD, sm_121 listing and the C encoder's bytes (CudaArch r6) |
| `sm80/resadd.s2` | the same kernel ported by hand to sm_80, every instruction with a `;=` intent text |
| `specs/tmatmul16.spec`, `sm121/tmatmul16.s2` | 16x16x16 tile matmul on sm_121 HMMA.16816.F32 (f16 in, f32 out): 4 jobs, 20 expectations |
| `specs/tsoftmax.spec`, `sm121/tsoftmax.s2` | row softmax, one warp per 32-value f32 row (SHFL.BFLY, MUFU.EX2/RCP): 3 jobs, 13 expectations |
| `TILE-WORDS.md` | OMEGESE proposal: tile words that would generate these two listings (PROPOSED, no compiler change) |

## Build and run (from the omega-mv root)

```sh
mkdir -p build/kflow
for p in kflow kemu kpack; do seed/out/omc < seed/kflow/$p.om > build/kflow/$p && chmod +x build/kflow/$p; done
build/kflow/kflow seed/kflow/m1.spec STATE      # exit 0 only if every expectation holds
```

## The manual process it replaces

| step done by hand today | kflow stage |
| --- | --- |
| write a `.s2` listing from a template or by hand | generate (design only; OmegaIntent #838/#843 IR is the planned source) |
| pick stall/yield/scoreboard words by hand | schedule (design only; per-arch latency tables, `vectors/sched80.tsv` is NAK's reference) |
| `cat TABLE LISTING \| asm2.bin` | **encode** |
| compare bytes with the C encoder or NAK by hand, read `nvdisasm` output by eye | **verify** |
| build a cubin with CUDA++ `omega_cubin.c`, check with `cuobjdump` | **package** |
| no host check existed before a chip run | **emulate** (kemu) |
| ask for a GPU window, run the chip gate | **gpu**: `STATE/queue/gpu/NAME.job`, NOT_RUN |
| rent a card through CloudKernels | **cloud**: `STATE/queue/cloud/NAME.job`, NOT_RUN, needs the budget cap |
| try variants, keep the fastest | search/tune (design only; needs gpu or cloud timings) |
| write up the evidence | **receipt**: `STATE/jobs/NAME.receipt` |

Stage statuses are NOT_RUN, INVALID, INCOMPLETE, VALID-UNPROVEN and QUALIFIED; there is no bare PASS.
INVALID stops the job. QUALIFIED needs a chip run, so milestone 1 never prints it.

- **encode**: input is the table plus the listing with `;` comments and blank lines removed. INVALID when asm2
  fails or the output is empty or not a multiple of 16 bytes.
- **verify**: `nvdisasm -b SMxx` must decode the bytes. Then the referee bytes must be equal (REF given), or
  every `;=` intent text must equal nvdisasm's text in order, with only NOPs after. Neither: INCOMPLETE.
- **package**: kpack writes the cubin; `cuobjdump -sass` must read it and its instruction texts must equal the
  verify texts.
- **emulate**: kemu runs the harness: MATCH is VALID-UNPROVEN, MISMATCH or a fault INVALID, an
  instruction it does not implement INCOMPLETE.

Each stage is cached in `STATE/cache/KEY.*`, KEY = sha256 of the stage name, the tool's sha256, the input
sha256s and the parameters. This is a content hash of bytes; the semantic hash of #830 is meant to replace it
for listings and is not wired in yet.

## Spec language

```
tool NAME PATH                       asm2 | nvdisasm | cuobjdump | kemu | kpack (all five required)
tsub LINE OLD NEW                    next job only: table line named LINE, token OLD -> NEW (exactly one hit)
lsub OLD NEW                         next job only: listing token OLD -> NEW (exactly one hit)
nointent                             next job only: ignore ';=' lines
job NAME ARCH TABLE LISTING REF HARNESS REGS P...   ARCH 80|121; REF a .bin or '-'; HARNESS or '-';
                                     P = p8|1|2|4|8 parameter sizes, laid out from c[0x0][0x380]
expect STAGE STATUS                  about the job above; STAGE encode|verify|package|emulate|gpu|cloud|job
```

## Milestone 1 results (MEASURED on the Spark, 2026-10-26)

Run: `build/kflow/kflow seed/kflow/m1.spec /tmp/kfx/st2`, binaries from `seed/out/omc` sha256
`2c824805…` at omega-mv 369dbc96. Copies of the output, receipts and queue files are in
`/home/drakestapleton/handoffs/KernelFlow/m1/`.

- vecadd sm_121, vecadd sm_80, RES ADD sm_121, RES ADD sm_80: encode, verify, package and emulate all
  VALID-UNPROVEN; gpu and cloud NOT_RUN. `KFLOW expectations met=31 failed=0 jobs=7`, exit 0.
- Second run on the same STATE: all 24 stage results that ran are cache hits (0 misses), same statuses, exit 0.
- Negative control 1: one planted bit in the sm_121 `ldg.2` table base (`0x0c1e1900` -> `0x0c1e1980`):
  encode VALID-UNPROVEN, verify INVALID, package and emulate NOT_RUN.
- Negative control 2: the same on the sm_80 table (`0x080e0000` -> `0x080e0080`): verify INVALID.
- Negative control 3: sm_80 RES ADD with intents ignored and one FADD source changed (R21 -> R22): verify
  INCOMPLETE, package VALID-UNPROVEN, emulate INVALID (kemu: MISMATCH at arena offset 0x10004).
- kpack's sm_121 cubins for vecadd and RES ADD are byte-identical to CUDA++ `omega_cubin_pack` (checked with
  a throwaway C driver outside the repo). The sm_80 cubin is read by `readelf` and `cuobjdump`; there is no
  CUDA++ sm_80 writer to compare against.

## Tile kernels (OmegaTile, MEASURED on the Spark, 2026-10-26)

Runs: `build/kflow/kflow seed/kflow/specs/tmatmul16.spec STATE` gives `KFLOW expectations met=20 failed=0
jobs=4`; `.../tsoftmax.spec` gives `met=13 failed=0 jobs=3`; `m1.spec` still gives `met=31 failed=0 jobs=7`.
Copies: `/home/drakestapleton/handoffs/OmegaTile/evidence/`. No GPU ran either kernel (gpu and cloud NOT_RUN).

- **Decode.** Every instruction of both listings carries a `;=` intent, and verify requires nvdisasm 13.0
  (`-b SM121`) to print exactly that text. Separately, every table row the kernels use that nvcc 13.0
  (V13.0.88, `mma.sync.m16n8k16` and a warp softmax compiled for sm_121) also emits was encoded by asm2 with
  nvcc's control word: 48 of 48 instructions byte-equal to `cuobjdump` (HMMA f16 and bf16, SHFL.BFLY, FMNMX,
  FADD, FADD with -R, FMUL, MUFU.EX2, MUFU.RCP, LDG.E, STG.E). LDG.E.U16, IMAD, SHF and LOP3 are checked by
  the decode only.
- **Emulation.** kemu now runs a CTA's warps in lockstep: SHFL and HMMA execute once for all 32 lanes, need
  every lane at the same pc with the same guard, and stop the run (`DIVERGED`, exit 2) otherwise. HMMA.16816
  is modelled as f16/bf16 to f32 by bit moves, then f32 products (exact) summed in k order onto C.
  MUFU.EX2 is computed by 2^n e^(f ln 2) in f64 rounded to f32, MUFU.RCP as an f32 divide. Neither is a
  model of the hardware's approximation; the tolerance below covers the hardware's documented bounds.
- **References.** `tmm1`: A, B random normal f16 (exponents -5 to 5), reference sum in f64 from f16 values
  decoded arithmetically (not by the emulator's bit moves). `tsmx`: x multiples of 2^-17 in [-4, 4) (so x - m
  is exact in f32), reference e^(x-m) by a separate series in f64 and an f64 sum.
- **Tolerances** (Wolfram Language, quoted query and result):
  `uu=2^-23; gam[n_]:=n uu/(1-n uu); ur=2^-24; lg=Log[2,E]; bits=FromDigits["3fb8aa3b",16];
  lh=(2^23+BitAnd[bits,2^23-1])/2^23*2^(BitShiftRight[bits,23]-127); dl=Abs[lh-lg]; ey=8 dl+ur*8*lh;
  ee=(1+5/2*2^-23)*2^ey-1; et=(1+ee)/((1-ee)(1-ur)^5)*(1+3/2*2^-23)(1+ur)-1;
  N[{lh,dl,ey,ee,et,et/2^-23,Ceiling[et*2^40],gam[15],Ceiling[gam[15]*2^40]},12]` ->
  `{1.44269502162933349609375, 1.92596299113e-8, 8.42007633558e-7, 8.81658785339e-7, 2.29976200922e-6,
  19.2918019887, 2528616, 1.78814254071e-6, 1966084}`.
  - Matmul: products of two f16 are exact in f32 (22 significant bits); with C = 0 the first addition is
    exact, leaving 15 roundings. Allowing u = 2^-23 per rounding (any rounding mode, any order),
    |d - ref| <= gamma_15 * sum_k |a_ik b_kj|, gamma_15 = 1.78814254071e-6; kemu uses 1966085 * 2^-40.
  - Softmax, relative: |x - m| <= 8; the log2 e word 0x3fb8aa3b is off by dl, and FMUL rounds (u = 2^-24),
    so the exponent is off by at most ey; ex2.approx.f32 is within 2 ulp of the correctly rounded value
    (PTX ISA 9.4), taken as 2.5 * 2^-23 relative; the 5 sum additions at u = 2^-24 each; rcp.approx.f32 within
    1 ulp of correctly rounded, taken as 1.5 * 2^-23; the final FMUL 2^-24. Total et = 2.29976200922e-6
    (19.29 * 2^-23); kemu uses 2528617 * 2^-40. The f64 reference's own rounding is not budgeted; it is
    about 2^29 times finer than f32 and the bound was rounded up by one unit of 2^-40.
  - Used by the emulated kernels (`tol_used_ppm`, largest error as parts per million of the bound), seeds 1,
    2, 3, 20261026: matmul 108395, 88757, 98075, 109922; softmax 189426, 181659, 206161, 223365.
- **Negative controls.** `tmmbf`: the HMMA type bit flipped in the table (`hmma.f16.2` 0x00001800 ->
  0x00041800) prints HMMA.16816.F32.BF16, verify INVALID; with intents ignored (`tmmbfe`) kemu reads the f16
  bits as bf16 and the emulate stage is INVALID. `tmmt`: the same bytes with B transposed in memory, verify
  VALID-UNPROVEN, emulate INVALID. `tsmxdn` / `tsmxdne`: the butterfly table row turned into SHFL.DOWN, verify
  INVALID, and with intents ignored emulate INVALID.
- The first emulation of `tsoftmax.s2` was a MISMATCH: the max butterfly wrote its partner value over its own
  (`SHFL R13, R13` then `FMNMX R13, R13, R13`). The listing now keeps the partner in R19.
- **Schedules are MODELED.** Every variable-latency instruction sets scoreboard 0 and its first reader waits;
  HMMA stalls 15 and two NOPs stall 15 each before the stores. kemu ignores control words; only a chip run
  can show the waits are enough.

## Limits

- The sm_80 RES ADD listing was checked with `nvdisasm` (an NVIDIA disassembler); the rest of `seed/sm80/`
  was checked only against Mesa NAK.
- kemu runs the lanes of a warp in lockstep and warps one after another, models no timing and no memory
  ordering, and implements only the
  instruction forms these kernels use. A MATCH says the arithmetic and addressing are right on the host, not
  that the chip will agree.
- The tile kernels use fixed shapes (one 16x16x16 tile, 16 rows of 32), parameters at fixed constant offsets
  and no bounds checks. A real attention or GEMM kernel needs loops over K and over tiles, which are not here.
- The gpu and cloud job files are inputs for a window or for CloudKernels; nothing reads them yet.
- OM-2 reserves `layout`, `field`, `at`, `part`, `out` and others as names (error 16); `le` and other operator
  words cannot name a variable either.
