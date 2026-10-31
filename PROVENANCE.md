# Provenance of hardware knowledge in omega-mv

The rule for adding hardware facts (instruction encodings, container layouts, launch descriptors) to
Omega's NVIDIA back ends, and a record of the NVIDIA tool output already stored here. It is an engineering
record, not legal advice. The same rule is in aien-dev/CUDApp `PROVENANCE.md`, which quotes the licence
clauses it follows: CUDA Toolkit EULA (https://docs.nvidia.com/cuda/eula/index.html, January 2025
revision) §1.2(1), §1.2(8), §1.1.2(4) and §2.1; NVIDIA driver licence §2.1 and §2.2; MAX License §1.4
(https://www.modular.com/legal/max-license, October 21, 2026).

## Allowed sources

- **Class A, use freely and cite:** public NVIDIA documentation (PTX ISA, CUDA Binary Utilities,
  open-gpu-doc); open source under permissive licences (NVIDIA open kernel module headers MIT, Mesa
  NAK/NVK MIT, LLVM `BinaryFormat/ELF.h` Apache-2.0 WITH LLVM-exception, CUTLASS BSD-3-Clause); our own
  bytes run on our own NVIDIA GPU.
- **Class B, oracle only:** NVIDIA's `nvcc`, `ptxas`, `cuobjdump` and `nvdisasm` installed on our own
  machine, run only on inputs we wrote (probe kernels we authored, bytes our encoders emitted), only for
  NVIDIA-GPU targets, with the tool version recorded. The tools are never shipped or invoked by an Omega
  program; `tools/sm121dis` replaces `nvdisasm` in the checks (C6).

## Not allowed

1. Reading, disassembling or extracting the machine code or containers of any NVIDIA binary: `libcuda`,
   cuBLAS, cuDNN, TensorRT, NCCL, the compiler binaries, driver files, firmware, or cubins embedded in
   NVIDIA libraries. This applies to rented machines as well as the Spark.
2. Using a fact learned from class B output for a non-NVIDIA target (SPIR-V, Vulkan, AMD, any other).
   Those back ends take facts from that vendor's class A sources only.
3. Committing SDK files, NVIDIA binaries, firmware, or NVIDIA headers other than the MIT open-module
   headers.
4. Giving MAX source or binaries to an AI system to produce software that reimplements or substitutes
   for MAX.

## What a new fact records

Class and exact source (document and section, file and commit, or tool and version); for class B the
probe input path and command; the chip receipt or NOT_RUN. A golden copied from class B output is a
regression pin, not independent evidence; a golden written from class A and confirmed on the chip is
independent.

## Corrections

- 2026-10-27: cubin ABI 7 notes for `seed/sm80/cubin.om` (PR #864, branch `ck/cubin-om`) had cited CUDA
  12.8 cuBLAS cubins, which were extracted from `libcublas.so` on a rented machine. That is not allowed
  (rule 1). The extracts were deleted and the notes now cite only nvcc 13.0 probe kernels and LLVM
  `ELF.h` (commit `76639738a`). The emitted bytes did not change, so the knowledge was re-sourced, not
  re-derived independently; this is listed as an open question for counsel.

## NVIDIA tool output stored in this repository

`evidence/` is additions-only, so these files stay. Each is class B output.

| File | Input | Tool |
|---|---|---|
| `evidence/gpu-reconv-308/oracle/cf.sass` | `cf.cu`, a probe kernel I wrote, compiled by nvcc 13.0 | `cuobjdump -sass` |
| `evidence/OWN30B-T3-HMMA-MODEL-38fe3d98-20261013T2204Z/oracle.sass` | `oracle.cu`, my probe, nvcc 13.0 | `cuobjdump -sass` |
| `evidence/OWN30B-T3-TC-PEAK-BW-5faa0f70-20261013T2236Z/fa_ops.sass` | `fa_ops.cu`, my probe, nvcc 13.0 | `cuobjdump -sass` |
| `evidence/SEED-0/sm121/vecadd.sass.txt` | `seed/sm121/vecadd.bin`, made by `asm2` | `nvdisasm` |
| `evidence/SEED-0/sm121/forms-nvdisasm.tsv`, `nvdisasm-refused*.txt` | edge cases generated from `seed/sm121/sm121.tbl` | `nvdisasm -b SM121` |
| `evidence/C6-DISASM/11-*.log`, `12-*.log` | `tools/sm121dis/zero_nvdisasm.sh` (11: no file outside the oracle harness looks up `nvdisasm`; 12: red run with planted calls) | not an NVIDIA listing |
| `evidence/E1-SIMT-C3/20261002T211613Z/ldst/nvdisasm.log` | encoder output, 148 kernels | `nvdisasm` 13.0 (verdict only) |

`bench/prime_race/tests/encoding_oracle.md` documents an nvcc + cuobjdump oracle procedure on probe kernels.
`tools/divsqrt_nvdisasm_check.sh` and `tools/ldst_nvdisasm_check.sh` run `nvdisasm` on encoder output.
`evidence/C5-ISOLATION/04-never-ending-kernel.sass.txt` is `sm121dis` output, not NVIDIA's.

## Open questions for counsel

1. Does deriving NVIDIA instruction encodings from nvcc output of our own probe kernels, for an
   NVIDIA-only toolchain, fall within the CUDA EULA (§1.2(8) is limited to non-NVIDIA targets)?
2. `tools/sm121dis` was built to print exactly what `nvdisasm` prints, checked by comparing outputs over
   many inputs. Is systematic black-box comparison with an SDK tool "reverse engineering" of the SDK under
   §1.2(1)?
3. May the class B listings above be published?
4. Does re-sourcing the ABI 7 notes after the cuBLAS extraction (Corrections) cure it?
