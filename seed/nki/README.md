# seed/nki: Omega to AWS Trainium / Inferentia (NKI)

`irnki.om` is a handwritten, seeded-Omega emitter. It reads an OSC IR v1 unit
(`spec/one-layer-format.md`) and writes an NKI kernel module in Python at the `nki.isa`
(NISA) level. NISA is the lowest level AWS documents publicly. The first kernel is
vecadd: `out[i] = a[i] + b[i]`.

Status, 2026-10-26:

| Item | Status |
|---|---|
| Emitter `irnki.om` | IMPLEMENTED |
| Kernels on the NKI CPU simulator, targets trn1, trn2, trn3 | EXERCISED-on-oracle (interop oracle, not Omega-native) |
| Trainium or Inferentia hardware | NOT_RUN |

## What it emits

Each function becomes one `@nki.jit` kernel. The kernel loops over tiles of
128 partitions x 512 elements (`nl.affine_range`) and keeps three explicit SBUF tiles per
step, `ta`, `tb` and `tc`. It moves data with `nisa.dma_copy` (HBM to SBUF and back) and
computes one `nisa.tensor_tensor(dst=tc, data1=, data2=, op=nl.<op>)`. The module ends
with `KERNELS = {name: (kernel, dtype)}`.

The emitter lifts only this shape:

- two parameters and a result, all u32 (tag 4), and three vregs;
- one block of two instructions: `BIN sub dst=2 a b` (a and b are params 0 or 1), then `RET 2`.

The supported subcodes are add, sub, mul, and, or and xor, mapped to `nl.add`,
`subtract`, `multiply`, `bitwise_and`, `bitwise_or` and `bitwise_xor`. These are refused:

| Input | Exit and message |
|---|---|
| div, rem, shl, shr | 24 UNSUPPORTED_OP. Omega traps on these and NKI has no trap. |
| Any tag other than u32 | 25 UNSUPPORTED_TAG |
| Any other function shape | 29 NOT_ELEMENTWISE |
| Bad bytes | 20–28, as in irwasm |
| Bad arguments | 30 USAGE |

Every refusal happens before the first stdout byte.

`irnki f32` retypes the lanes to float32 and then allows only add, sub and mul. This
retyping is explicit and documented. It exists because IR v1 cannot carry floats: the
compiler refuses float words in `ir` mode with error 16, and the float tags in spec
section 10.3 are not produced yet. The f32 vecadd is therefore the u32 unit
`vecadd.unit.om` with float32 lanes. The gate computes the f32 reference from the same
unit with `uint` read as `f32`.

Shape limits: the kernels take 2-D arrays whose rows are a multiple of 128 and whose
columns are a multiple of 512. Any other shape is refused by an assertion, and the gate
checks that (128, 500) is refused. Three f32 tiles take 6144 bytes per partition, which
is 1/32 of the 192 KiB trn1 SBUF partition (224 KiB on trn2, 256 KiB on trn3). Computed
with WolframLanguageEvaluator: `3*512*4 -> 6144`, `3*512*4/(192*1024) -> 0.03125`.

## Run

```sh
seed/nki/setup-oracle.sh /tmp/nki-oracle          # pinned downloads, user files only
NKI_ORACLE_DIR=/tmp/nki-oracle nice -n 19 seed/nki/gate-irnki.sh
```

Exit codes: 0 PASS, 1 FAIL, 3 NOT_RUN (no oracle directory, the oracle does not start,
the oracle version differs from the pin, or the host is not AArch64). Artifacts go to
`build/seed/nki/`, and `gate.log` holds SOURCE, COMPILER, ORACLE and PLATFORM.

The gate does the following:

1. Builds `irnki` with the pinned `seed/out/omc` (sha256 `2c824805…029f`).
2. Lifts `vecadd.unit.om` (f32 and u32) and `ops.unit.om` (8 functions: every op,
   swapped operands `sub b a`, and repeated operands `add a a`). Each module is emitted
   twice and compared with `cmp`.
3. Computes the expected lanes with a native AArch64 Omega program for each function,
   built from the same unit source plus a driver. No foreign code computes expected
   values.
4. Runs every kernel in `nki.simulate` for trn1, trn2 and trn3 on 262144 lanes, shaped
   (256, 1024), which is 4 tiles. The input set includes all pairs of edge values: 100
   for u32, and 196 for f32, covering ±0, ±inf, NaN, ±max, min-normal, subnormals and
   1+ulp. Results are compared bit for bit, except that two NaNs count as equal, because
   NaN sign and payload are not part of the IEEE result.
5. Negative controls, each of which must FAIL:
   - an emitted `nl.add` edited to `nl.subtract`;
   - the IR BIN subcode byte changed from add to sub before lifting;
   - one expected bit flipped;
   - a kernel dropped from `KERNELS`.
6. Checks 14 refusals: exact exit code, the message on stderr and empty stdout.

Measured 2026-10-26 on the DGX Spark: PASS, with 30 of 30 kernel/target runs equal and
every control rejected. The same result came from a directory freshly prepared by
`setup-oracle.sh`.

## Oracle stack (interop oracle, not Omega-native; never generates kernels)

The nki wheel is published for x86_64 only, so on AArch64 it runs as x86_64 CPython
under qemu-user. Every item is a host dependency of the oracle only.

| Item | Version | Licence | sha256 |
|---|---|---|---|
| nki (from `pip.repos.neuron.amazonaws.com`) | 0.7.0+37713544100.gfede04bb, cp312 linux_x86_64 | Apache-2.0 | `03543bed…0c4e` |
| numpy | 2.2.6 | BSD-3-Clause | `fd83c012…6249` |
| ml_dtypes | 0.5.3 | Apache-2.0 | `1b255aca…9e55` |
| typing_extensions | 4.15.0 | PSF-2.0 | `f0fa19c6…e548` |
| CPython (python-build-standalone 20261026) | 3.12.16 x86_64 | PSF-2.0 | `6a79e922…9de` |
| qemu-user (Debian trixie) | 10.0.13+ds-0+deb13u1 arm64 | GPL-2.0 | `c73711af…158d` |
| libc6, libgcc-s1, libstdc++6, zlib1g (Ubuntu noble amd64) | see `setup-oracle.sh` | LGPL-2.1 / GPL-3 with runtime exception / zlib | see `setup-oracle.sh` |

`setup-oracle.sh` holds the full pins. The host qemu 8.2.2 crashes (internal SIGSEGV)
when Python reads `/proc/self/maps`, which is why the Debian 10.0.13 static build is used.

`oracle.py` is the only foreign code. It writes the deterministic inputs, runs the
emitted kernels and compares bits. It never writes a kernel or an expected value.
There is no Omega replacement yet: it needs an Omega reader for the NKI simulator, or
an Omega NISA interpreter.

## How open the stack is (primary sources, read 2026-10-26)

- The NKI programming interface (`nki.language`, `nki.isa`) and the CPU simulator ship in
  the `nki` wheel under Apache-2.0. Wheels exist for x86_64 only.
  https://awsdocs-neuron.readthedocs-hosted.com/en/latest/nki/get-started/about/index.html,
  https://awsdocs-neuron.readthedocs-hosted.com/en/latest/nki/guides/nki_simulator.html
- The NKI compiler (MLIR based) is announced as open source but is a private beta. The
  wheel's project URL `github.com/aws-neuron/private-nki-staging` is not public, and
  `github.com/aws-neuron/nki` returns 404. The NKI IR and its MLIR dialect ship only as
  compiled binaries and are not documented.
  https://aws.amazon.com/about-aws/whats-new/2025/12/announcement-aws-neuron-2-27/,
  https://awsdocs-neuron.readthedocs-hosted.com/en/latest/nki/deep-dives/nki-compiler.html
- The lowest documented level is therefore `nki.isa`, one call per engine instruction.
  Below it, NEFF and the engine binary encodings are not public.
- `neuronx-cc` 2.28.7631.0 (wheel METADATA and LICENSE.txt, read without installing) is
  under the "AWS Neuron Software License Agreement". That agreement grants use only "in
  connection with AWS Services" and requires an AWS account in good standing. The gate
  does not use it.
- nki-samples is MIT-0; nki-library and nkipy are Apache-2.0; aws-neuron-driver is
  GPL-2.0. https://awsdocs-neuron.readthedocs-hosted.com/en/latest/about-neuron/oss/index.html
- Architecture figures (SBUF 24/28/32 MiB = 128 partitions of 192/224/256 KiB; PSUM
  2 MiB) come from
  https://awsdocs-neuron.readthedocs-hosted.com/en/latest/nki/guides/architecture/trainium_inferentia2_arch.html,
  `trainium2_arch.html` and `trainium3_arch.html`.

## What is NOT established

- No hardware run. The simulator is AWS's functional model; it is not the chip. Engine
  choice, timing, subnormal flushing and NaN payloads on Trainium/Inferentia are not
  measured. The simulator runs with its default `NKI_PRECISE_FP=1`.
- Kernels are not compiled by `neuronx-cc`, and no NEFF exists.
- Only single-op elementwise u32 units are covered, plus the f32 retyping. There are no
  reductions, no matmul (TensorE/PSUM), no broadcasting and no ragged shapes.

## Path to hardware (NOT_RUN)

1. An AWS account and a Neuron instance: inf2.xlarge or trn1.2xlarge (NeuronCore-v2),
   trn2 (v3) or trn3 (v4), with the Neuron DLAMI or the driver, runtime and `neuronx-cc`
   from the Neuron setup guide.
   https://awsdocs-neuron.readthedocs-hosted.com/en/latest/setup/index.html
2. Run the same emitted module with `@nki.jit` on NumPy arrays (it compiles through
   `neuronx-cc` and runs on the NeuronDevice), and compare the lanes with the same
   native Omega references used here.
3. Record the instance type, Neuron SDK versions and the NEFF sha256 in a receipt. Only
   that receipt can move the status past EXERCISED-on-oracle.

The next kernel is matmul: `nisa.nc_matmul` with a 128x128 stationary tile and a 128x512
moving tile accumulating in PSUM, then `nisa.tensor_copy` PSUM to SBUF and `dma_copy` out,
checked on the same simulator against a native Omega reference.
