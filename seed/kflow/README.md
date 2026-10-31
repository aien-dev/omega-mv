# seed/kflow/: KernelFlow, one tool from "I need a kernel" to a receipt

Status: milestone 1 IMPLEMENTED and EXERCISED on the host (the Spark, arm64). No GPU ran anything from it:
every gpu and cloud stage is a queued job file with status NOT_RUN. Nothing here is QUALIFIED.

All three programs are seeded Omega (OM-2), built by the pinned `seed/out/omc`. `nvdisasm` and `cuobjdump`
(CUDA 13.0) are read-only oracles; nothing they print is copied into an output.

| file | what it is |
| --- | --- |
| `kflow.om` | the conductor: reads a spec, runs the stages, caches them, writes receipts and queue files |
| `kemu.om` | host SASS emulator for the instructions vecadd and RES ADD use (sm_121 and sm_80); harnesses `vadd`, `radd` |
| `kpack.om` | cubin packer, a port of CUDA++ `src/omega_cubin.c` (aien-dev/CUDApp 853e814d) |
| `m1.spec` | milestone 1: 4 jobs and 3 negative controls, 31 expectations |
| `sm121/resadd.s2`, `sm121/resadd.c.bin` | 35B decode RES ADD, sm_121 listing and the C encoder's bytes (CudaArch r6) |
| `sm80/resadd.s2` | the same kernel ported by hand to sm_80, every instruction with a `;=` intent text |

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

## Limits

- The sm_80 RES ADD listing was checked with `nvdisasm` (an NVIDIA disassembler); the rest of `seed/sm80/`
  was checked only against Mesa NAK.
- kemu runs threads one after another, models no timing and no memory ordering, and implements only the
  instruction forms these kernels use. A MATCH says the arithmetic and addressing are right on the host, not
  that the chip will agree.
- The gpu and cloud job files are inputs for a window or for CloudKernels; nothing reads them yet.
- OM-2 reserves `layout`, `field`, `at`, `part`, `out` and others as names (error 16); `le` and other operator
  words cannot name a variable either.
