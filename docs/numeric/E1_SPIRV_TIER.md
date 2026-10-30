# E1 SPIR-V tier: eleven open-math ops as seeded Omega, run through irspirv on PoCL

Status, 2026-10-27: IMPLEMENTED and EXERCISED on the host (PoCL CPU device on the Spark's
Grace cores). Not run on any GPU. Not in CI. Not merged. No Alpha or chip qualification is
implied: the GB10 tier of the same eleven ops is a separate path (`src/omega_numeric_divsqrt_gb10.c`,
receipts under `evidence/E1-TRANSC-GB10/`), and this tier says nothing about it.

## What it is

Each E1 op of the CPU tier (`src/omega_numeric_transc.c`; natural EXP and LOG from
`src/omega_numeric.c`) is written as an IR-mode Omega unit in `seed/spirv/e1/`, compiled by `omc`
to OSC IR v1, translated by `seed/spirv/irspirv.om` to an OpenCL Kernel SPIR-V module, validated
with `spirv-val --target-env opencl2.1`, and run on PoCL. The kernels are `e1_<op>`:
`e1_exp2 e1_log2 e1_sigmoid e1_tanh e1_rsqrt e1_erf e1_gelu e1_sin e1_cos e1_exp e1_log`.

No device floating point is used. `softf.om` implements IEEE 754 binary32 add, sub, mul, div, sqrt
and fused multiply-add on integers (round to nearest even, subnormals kept, default NaN
`0x7fc00000`), the operations `src/omega_numeric_transc.c` issues as AArch64 instructions.
`core.om` holds the shared cores in the CPU tier's operation order; `consts.om` holds every
coefficient as its binary32 bit pattern; `erfcxt.om` holds the erfcx tables for ERF and GELU.
A module is `softf.om consts.om core.om [erfcxt.om] OP.om` (`trig.om` holds both SIN and COS).
The C reference was built with `-ffp-contract=off`, so each C operator is one unfused operation.

Launch convention: the NDRange ABI in `docs/SPIRV.md` (work item `g` reads `inputs[g*P + j]` and
writes `outputs[2g]`, `outputs[2g + 1]`). `seed/spirv/opencl-host --sweep MODULE.spv KERNEL START
COUNT` (verification host only, C, migration pending; see its header) launches the kernel over
`START..START+COUNT-1` and writes the 32-bit results to stdout in input order.

## Build

```sh
OMC=build/seed/ir/stage2 IRSPIRV=build/seed/spirv/irspirv seed/spirv/e1/build.sh OUTDIR
```

The units need the IR capacity of omega-mv #889 (branch `e1open/omc-ir-capacity`): main's compiler before
#889 stops at 64 registers and 32 literals per function. The modules below and the evidence were built
with #889's source-only candidate (`2686b5dcb`, sha256
`2cf38551eb52db9fb01ca9e6b34578f00100320662cbb2a93a069c9c8a77ea08`, stage1 = stage2 = stage3). After #889
was reseeded, `OMC=seed/out/omc` (sha256 `34768ee8f2779347c0cfa49a1b8a82b8d110b14123a7693774d4e1e435c944c6`)
rebuilt irspirv to the same sha256 and all 10 modules byte for byte (2026-10-30, merge `8c95d5481`).
`E1_MUTATE=<const>` flips mantissa bit 12 of one `consts.om` constant for negative controls.

Modules at `f26246453`, built 2026-10-27 with that compiler and irspirv sha256
`865a76d942219140868e4fbbf1318ff725e7692e5c76a5e175491131b670b5ca`; a clean rebuild from the
branch head reproduced every module byte for byte (`cmp`, 10 of 10):

| Module | sha256 |
|---|---|
| exp2.spv | `5e8bacaadbb2f5a893cea682e4cb7a23b732abcb67638253211aff06033f0a39` |
| log2.spv | `04427798fc0c6d46b68c3df8d8ae8a9e874acdcc787099751c1e69854da15bda` |
| sigmoid.spv | `6558c6d6f6decb8378f3ca9b717a303e78ffdf58c56223de6f4dc8df11f14cef` |
| tanh.spv | `eadff6ddb57fb3aa9ab4da88e22c192750c825c98ff93a21c930fea182819b6f` |
| rsqrt.spv | `198b782b1cbc39296cc0e2528f0036449be216432f8740f7b787103dc8fd4b9e` |
| erf.spv | `394d60050ba4410cb557c4390f26c109ae074dcb5cd149ae79e8286a3b54064d` |
| gelu.spv | `07b7cb606e6055bfbcd94128dc653d2b535f66dd9b4af6181bc1dcc2d6427203` |
| trig.spv (SIN, COS) | `0dccc819952cf1a88466f410a5f88968cccf9fac56e581034767b835d121febb` |
| exp.spv | `c4a26cef0fb660c3af65010d440d0c63bd07f1c8a34b3881d3f5456755d5123b` |
| log.spv | `0a0a3459e5e89431a3e8ca13a077bc4043872bf90264792f46cbbaf0d341a101` |

## Platform

Spark, aarch64 Linux 7.0.0-1019-nvidia, PoCL 7.2 (Release, SPIR-V, LLVM 18.1.3, SLEEF; a
user-space build, `POCL_DEVICES=cpu`), opencl-host sha256
`82c65325d3bc4ae00ddcaedf794e6fd9069566f787b690ecb162b9cf42713c26`. All runs at
`nice -n 19 taskset -c 10-14`, except the second exhaustive run (`taskset -c 5-9`, see Results).

## Parity check

The comparator reads the 32-bit SPIR-V results for one window and compares each with
`omega_math_<op>` of the CPU tier, bit for bit. It is a verification harness, kept with the
evidence (`evidence/E1-SPIRV-POCL/20261027-f2624645/recipe/`), not part of any build:

- `par.c`: built from omega `cb6d20143` sources with
  `gcc -std=gnu11 -D_GNU_SOURCE -ffp-contract=off -DOMEGA_NUMERIC_CPU_ONLY -O2 -Isrc par.c
  src/omega_numeric_transc.c src/omega_numeric.c src/omega_numeric_provenance.c
  src/omega_numeric_divsqrt_gb10.c src/omega_blackwell_{encoder,codegen,matmul,qmd}.c
  src/omega_om_units.c -lm`. Migration pending: the reference it calls is the C CPU tier; its
  seeded-Omega successor is a comparator `seed/spirv/e1/par.om` once that tier is itself `.om`.
- `sample.sh`: 42 windows of 131072 inputs per op: 32 strided at `k*2^27 + 2^25`, and ten at
  `+0`, `-0`, 1.0, 0.5, 2.0, -1.0, `0x7f7f0000` and `0xff7f0000` (running through infinity into
  NaN), 1034000000 and 3181483648.
- `exh.sh`: 256 windows of 2^24, every 32-bit input; one summary line per op.

## Results

Logs and summaries: `evidence/E1-SPIRV-POCL/20261027-f2624645/` and, for the second exhaustive run,
`evidence/E1-SPIRV-POCL/20261027-exh-0fca2343/`. "Sampled" is the 42-window set, 5,505,024 inputs
per op (`sample/run2.summary` for SIN and COS after the fix, `sample/run1.summary` for the other nine, whose
modules in that run were byte-identical to the table above). "Exhaustive" is all 2^32 inputs
(`exhaustive/SUMMARY`, per-window lines in `exhaustive/<op>.log`). A mismatch is any output whose 32 bits differ
from the CPU tier's, NaN payloads included. Every exhaustive run used `chunkdig.py` in the pipe, so each summary line
carries the SPIR-V tier's full-domain digest. TANH and RSQRT: first run (`20261027-f2624645`). The other nine:
second run, 2026-10-27 16:28:51Z-16:51:32Z, `nice -n 19 taskset -c 5-9`, with the same modules, opencl-host, `par`,
`exh.sh` and `chunkdig.py` (sha256 in `20261027-exh-0fca2343/inputs.sha256`, equal to the tables above and to
`recipe/`; the script as run is `run.sh`). EXP2, LOG2 and SIGMOID had an earlier exhaustive run without a digest
(4294967296 checked, 0 mismatches, `20261027-f2624645/exhaustive/`); the second run repeats them with one.

| Op | Sampled: mismatches | Exhaustive: inputs checked, mismatches | Full-domain digest | Equal to CPU tier and GB10 receipt |
|---|---|---|---|---|
| EXP2 | 0 | 4294967296, 0 | `227cb657…5bd8` | yes, `DIGEST_EXP2`, GB10 `b2f4afe9` |
| LOG2 | 0 | 4294967296, 0 | `fdf21ee2…07f3` | yes, `DIGEST_LOG2`, GB10 `d846fbe3` |
| SIGMOID | 0 | 4294967296, 0 | `b4e43cb7…886d` | yes, `DIGEST_SIGMOID`, GB10 `ae3a4123` |
| TANH | 0 | 4294967296, 0 | `932c09ff…5daa` | yes, `DIGEST_TANH`, GB10 `ae3a4123` |
| RSQRT | 0 | 4294967296, 0 | `454fc9d5…51e4` | yes, `DIGEST_RSQRT`, GB10 `d846fbe3` |
| ERF | 0 | 4294967296, 0 | `aef63f62…4aba` | yes, `DIGEST_ERF`, GB10 `d846fbe3` |
| GELU | 0 | 4294967296, 0 | `bda4da73…89db` | yes, `DIGEST_GELU`, GB10 `d846fbe3` |
| SIN | 0 | 4294967296, 0 | `9e4debc7…cddc` | yes, `DIGEST_SIN`, GB10 `ae3a4123` |
| COS | 0 | 4294967296, 0 | `28d580ee…ceb3` | yes, `DIGEST_COS`, GB10 `ae3a4123` |
| EXP | 0 | 4294967296, 0 | `413c3429…829c` | yes, host-model EXP digest, GB10 `b2f4afe9` |
| LOG | 0 | 4294967296, 0 | `05e94f63…f51e` | yes, host-model LOG digest, GB10 `b2f4afe9` |

No window of either run lacked a result (`no_result_windows=0` on every summary line), and opencl-host's stderr held
only its three banner lines per window. The reference digests as compared are in
`20261027-exh-0fca2343/ref-digests.txt`.

Negative controls (`control.out`): with `E1_MUTATE`, EXP NC4 gives 43084 of 131072 mismatches at
start `0x3f000000`; SIN S5 gives 131072 of 131072; LOG NL2 gives 0 at start `0x3f000000` and
26037 of 131072 at start `0x3f400000`. The comparator fails on a changed constant, so a 0 count
is not a comparator that accepts anything.

## The trig defect parity found

The first sampled run (`sample/run1.log`) produced no result for three SIN windows (starts
3212836864 = -1.0, 3254779904, 3388997632) and two COS windows (the last two): opencl-host reported
`returned value 0 status 105` for the first input of each, and `sample.sh` counts such a window as
1 in its summary (`run1.summary`: SIN 3, COS 2). For a negative quadrant `k + qoff`,
`ulong(k + qoff)` is a checked conversion and traps with status 105; a host build of the same
statement with the pinned omc exits 105 too. Commit `f26246453` masks on the signed value first, `kq = and add k qoff 3`, then
converts: two's-complement `& 3` is the floor-mod quadrant the CPU tier uses. The rerun
(`sample/run2.summary`) gave 0 mismatches and no missing window for both over the same 42 windows.

## Recipe files

`recipe/` holds `par.c`, `sample.sh`, `exh.sh`, `chunkdig.py` and `pocl-env.sh` as run, with machine paths replaced
by `$OMEGA_MV`, `$TOOLS`, `$PAR`, `$SCRATCH`, `$HOME`. `exh.sh` pipes each window through `chunkdig.py`, which passes
the bytes on unchanged and records the per-chunk digest of the `tests/test_omega_transc.c` full-domain digest tree;
the summary line then carries the SPIR-V tier's full-domain digest, comparable with the frozen CPU digests in that
file and with the GB10 receipts. `chunkdig.py` is verification-only, like `par.c`; its successor is the same
`seed/spirv/e1/par.om` comparator.

## Not run, and limits

- No CI job builds or runs this tier; every result above is one run on one host.
- PoCL's CPU device only. No GPU OpenCL or Vulkan driver has run these modules.
- The modules need omega-mv #889's compiler. With a compiler before #889 `build.sh` stops at the first
  module: `omc: line 258: error 14`, `omc failed on exp2 (rc 14)`.
