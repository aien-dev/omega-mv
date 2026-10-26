# Omega to SPIR-V compute kernels

`seed/spirv/irspirv.om` translates the seeded compiler's OSC IR v1 scalar units
into SPIR-V 1.0 **OpenCL Kernel** modules. It is handwritten Omega, built by
`seed/out/omc`; neither LLVM nor a foreign-language generator produces its output.
This is the Kernel profile used by the OpenCL/SYCL ecosystem, not Vulkan Shader
SPIR-V. The exercised consumer is PoCL on a CPU through `clCreateProgramWithIL`.
This does not establish DPC++ integration, Level Zero support, GPU qualification,
or general SYCL language support.

## Build and demo

On AArch64 Linux, from the repository root, with the committed compiler:

```sh
mkdir -p build/seed/spirv
seed/out/omc < seed/spirv/irspirv.om > build/seed/spirv/irspirv
chmod +x build/seed/spirv/irspirv
{ printf 'ir\n'; cat seed/spirv/demo.unit.om; } > build/seed/spirv/demo.input
seed/out/omc < build/seed/spirv/demo.input > build/seed/spirv/demo.ir
build/seed/spirv/irspirv < build/seed/spirv/demo.ir > build/seed/spirv/demo.spv
spirv-val --target-env opencl2.1 build/seed/spirv/demo.spv
spirv-dis build/seed/spirv/demo.spv -o build/seed/spirv/demo.spvasm
cc -std=c11 -O2 -Wall -Wextra seed/spirv/opencl-host.c \
  -o build/seed/spirv/opencl-host -lOpenCL
export OCL_ICD_VENDORS=/absolute/path/to/pocl.icd
build/seed/spirv/opencl-host build/seed/spirv/demo.spv avgu32 4294967295 4294967293
```

Stop immediately if any command fails; never execute a compiler's partial output.
For user-space OpenCL headers/libraries, add `-I`, `-L` and the corresponding
linker rpath to the host build. `OCL_ICD_VENDORS` must name a single PoCL `.icd`
file, not a vendor directory. The host refuses other platforms, overrides
`POCL_DEVICES=cpu`, and enumerates CPU devices only. No GPU is used.

The demo's complete function is:

```text
fn avgu32 a uint b uint to uint
ret add and a b shr xor a b uint 1
end
```

Observed stdout on PoCL 7.2 (2026-10-26):

```text
4294967294 0
```

The first field is the full result bit pattern; the second is the Omega status.
Two other exercised demo calls were `avgu32 0 1` -> `0 0` and `avgu32 7 10` ->
`8 0`. The host prints runtime/device identity on stderr. It creates an IL
program, builds it, creates the named kernel, allocates the input/output buffers,
launches one work item, and reads both output words back before printing.

## ABI and semantics

Every IR function is exported as a kernel with its original name and this ABI:

```c
void kernel_name(__global const ulong *inputs, __global ulong *outputs);
```

- Launch exactly one work item. Launching more races on the shared output pair.
- Input slots are scalar arguments in source order, at most six. Unsigned values
  are zero-extended, signed values sign-extended, and booleans are 0 or 1.
- `outputs[0]` is the canonical 64-bit result, or zero for void and trapped calls.
- `outputs[1]` is zero on success or the exact native Omega status `100 + trap`.
  A trap is a completed kernel result, not an OpenCL API failure.
- Modules declare Addresses, Kernel and Int64 capabilities, Physical64 addressing,
  and the OpenCL memory model. Internal function values use 64-bit integer bits;
  private Function-storage variables hold mutable OSC virtual registers.
- Signed overflow, divide-by-zero, checked conversions, shift bounds, explicit
  traps and compiler-emitted loop bounds retain Omega semantics. A callee's trap
  returns through callers immediately, preserving the first status. Signed
  minimum divided by minus one traps; the corresponding remainder is zero.
- Reachable blocks are emitted in deterministic reverse postorder, so dominators
  precede dominated blocks even when OSC block numbers are not in that order.
  Unreachable blocks are validated as IR but not emitted.

The accepted scalar subset matches `seed/wasm/irwasm.om`: bool and signed/unsigned
8/16/32/64-bit integers, constants, moves, arithmetic and bitwise operations,
comparisons, checked casts, earlier-function calls, branches, returns and traps.
Arrays, memory operations, floating point, host/device words, vectors, atomics,
barriers and other OSC versions are not accepted. Contract strings in OSC v1
are structurally checked metadata, not runtime pre/postcondition enforcement.
There is no shader fallback or silent host-code fallback.

Limits: 32 nonempty functions per unit, six parameters, 480 registers, 512 blocks,
4096 instructions per function, names up to 63 ASCII bytes, input strictly smaller
than 2620252 bytes, and an 8388608-byte output limit. Capacity overflow refuses;
these are bounded translator capacities, not claims about the SPIR-V specification.

| Exit | Named stderr error | Meaning |
|---|---|---|
| 20 | `irspirv: MAGIC` | Wrong OSC magic |
| 21 | `irspirv: VERSION` | Not OSC v1 |
| 22 | `irspirv: TRUNCATED` | Incomplete input |
| 23 | `irspirv: CAPACITY` | Translator capacity exceeded |
| 24 | `irspirv: UNSUPPORTED_OP` | Unsupported instruction |
| 25 | `irspirv: UNSUPPORTED_TAG` | Unsupported scalar tag |
| 26 | `irspirv: INVALID` | Invalid types, registers, CFG, name, use-before-definition or zero functions |
| 27 | `irspirv: TRAILING` | Bytes after the unit |
| 28 | `irspirv: IO` | Read/write failure |

Parsing, validation and emission are buffered: format/semantic/capacity refusals
produce no stdout. An I/O failure during final output can leave a partial stream;
consumers must check the exit status. Runtime/compiler failures also are not modules.

## CPU acceptance gate

```sh
export SPIRV_VAL=/absolute/path/to/spirv-val
export OCL_ICD_VENDORS=/absolute/path/to/pocl.icd
# Optional for user-space headers and libraries:
# export OPENCL_CFLAGS='-I/absolute/path/include'
# export OPENCL_LIBS='-L/absolute/path/lib -Wl,-rpath,/absolute/path/lib -lOpenCL'
sh seed/spirv/gate-irspirv.sh
```

The gate pins omc sha256
`befb8199db8c4bca188a494dfaac2a6f0ee5c36fcea11188c75666c55fce8a1d`.
It compiles the same units as irwasm, preserves the entire original 129-row
`seed/wasm/cases.tsv`, and builds a native Omega caller for every row. Comparison
uses all eight result bytes and exact native trap statuses, never just process
status modulo 256. Every exported kernel must appear in the table. Empty tables,
missing files, malformed host responses, timeouts, API failures and skipped
kernels fail closed. Missing prerequisite tools are NOT_RUN, not PASS.

Observed on AArch64 Linux with the toolchain below:

| Check | Observed |
|---|---|
| Original irwasm corpus | 129 exact native/PoCL matches, including 29 traps |
| Supplemental nested-call corpus | 8 exact matches, including 4 callee traps |
| Demo corpus | 3 exact matches |
| Reordered CFG `0 -> 2 -> 1` | Validator accepted, CPU returned `0 0` |
| Determinism | 16 modules translated twice, byte-identical |
| `spirv-val --target-env opencl2.1` | 17 modules accepted, including the valid arithmetic mutant |
| Wrong-add-to-sub mutant | Executes successfully with `12 0` instead of baseline `26 0`; parity checker exits 1 |
| Validator negative control | Missing final OpFunctionEnd rejected |
| Named refusal cases | 16 exact error/status matches, empty stdout |

The mutant is not a crash control: the gate requires successful OpenCL execution
and exactly one result mismatch when substituted into the baseline corpus.
The checker uses exit 2 for API/setup faults, distinct from mismatch exit 1.
Artifacts include `gate.log`, `case-spirv-parity.tsv`,
`regression-case-spirv-parity.tsv`, `demo-case-spirv-parity.tsv`, per-case raw
outputs/statuses, validator logs, and the rejected mutant comparison table under
`build/seed/spirv`. A gate run replaces this scratch directory; archive receipts
before rerunning. Published evidence is retained under `evidence/IRSPIRV/`.

## External oracle/toolchain ledger

These are verification/demo dependencies, not translator implementation or
production dependencies. The translator build depends only on seeded omc.
No DPC++/SYCL package was available in the inspected Ubuntu 24.04 arm64 package
catalog, so this demo uses the explicitly requested OpenCL IL route.

| Tool | Exercised version and role |
|---|---|
| omc | sha256 above; builds translator, emits OSC IR and native comparison programs |
| SPIRV-Tools | v2025.1, Ubuntu `2025.1~rc1-1~ubuntu0.24.04.2`; validator and disassembler |
| PoCL | v7.2, source commit `888c9774b94590fff1afef003154ca2c0ea66605`; CPU OpenCL oracle |
| LLVM/clang | 18.1.3; PoCL's CPU compilation dependency, not Omega translation |
| SPIRV-LLVM-Translator | 18.1.8; PoCL's SPIR-V reader |
| ocl-icd | 2.3.2; test host's OpenCL dispatch loader |
| OpenCL headers | `3.0~2023.12.14`; C demo interface declarations |
| GCC | 13.3.0; builds only the C demo host |
| Python | 3.12.3; read-only parity oracle and malformed-IR test construction |

Packages were downloaded with apt and extracted with `dpkg-deb -x` into a
user-space prefix. Ubuntu PoCL 5.0 did not expose SPIR-V IL and was rejected.
PoCL 7.2 was built from its pinned source with `ENABLE_ICD=ON`,
`ENABLE_SPIRV=ON`, `HOST_CPU_ENABLE_SPIRV=ON`, LLVM 18 and
`LLC_HOST_CPU=cortex-a720` (LLVM 18 did not recognize this host's newer cores).
The resulting CPU reports SPIR-V 1.0 through 1.4 and `cl_khr_il_program`.
The oracle's SPIR-V reader can abort on malformed IL; validation is mandatory
before execution, and a host abort is a failed oracle call, never parity success.

Binary/semantic authorities: [SPIR-V specification](https://registry.khronos.org/SPIR-V/specs/unified1/SPIRV.html),
[OSC wire format](../spec/one-layer-format.md), and the native seeded compiler.
The signed multiplication guard uses magnitude division; the Wolfram check
`Reduce[a b>l && a<=Quotient[l,b] && a>=0 && b>0 && l>=0,{a,b,l},Integers]`
returned `False`. This symbolic check is not the CPU parity receipt.

### Reproducing the user-space PoCL oracle

The following is the exercised Ubuntu arm64 provisioning recipe. It assumes
system CMake, Ninja, GCC, Git and apt tools. Run it in a disposable prefix; no
root install, ICD system registration, service or GPU change is required.
`evidence/IRSPIRV/` retains the exact downloaded package versions/checksums.

```sh
T=/tmp/spirv-tools-root
R=$T/root
mkdir -p "$T/debs" "$T/src" "$R"
cd "$T/debs"
apt-get download spirv-tools=2025.1~rc1-1~ubuntu0.24.04.2 \
  opencl-c-headers opencl-clhpp-headers ocl-icd-opencl-dev \
  ocl-icd-libopencl1 ocl-icd-dev clinfo llvm-18 llvm-18-dev llvm-18-tools \
  llvm-18-runtime llvm-18-linker-tools libllvm18 clang-18 libclang-18-dev \
  libclang-cpp18 libclang-cpp18-dev libclang-common-18-dev libclang1-18 \
  llvm-spirv-18 libllvmspirvlib-18-dev libllvmspirvlib18.1 \
  libpolly-18-dev libclang-rt-18-dev
for d in ./*.deb; do dpkg-deb -x "$d" "$R"; done
git clone --depth 1 -b v7.2 https://github.com/pocl/pocl.git "$T/src/pocl"
test "$(git -C "$T/src/pocl" rev-parse HEAD)" = 888c9774b94590fff1afef003154ca2c0ea66605
export LD_LIBRARY_PATH=$R/usr/lib/aarch64-linux-gnu:$R/usr/lib/llvm-18/lib
export PKG_CONFIG_PATH=$R/usr/lib/aarch64-linux-gnu/pkgconfig:$R/usr/share/pkgconfig
export PKG_CONFIG_SYSROOT_DIR=$R
export PATH=$R/usr/bin:$PATH
cmake -S "$T/src/pocl" -B "$T/build/pocl" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$T/pocl" \
  -DWITH_LLVM_CONFIG="$R/usr/lib/llvm-18/bin/llvm-config" \
  -DLLC_HOST_CPU=cortex-a720 -DENABLE_ICD=ON -DENABLE_TESTS=OFF \
  -DENABLE_EXAMPLES=OFF -DENABLE_HWLOC=OFF -DENABLE_LLVM_FILECHECKS=OFF \
  -DENABLE_POCL_BUILDING=OFF \
  -DLLVM_SPIRV_INCLUDEDIR="$R/usr/include/LLVMSPIRVLib" \
  -DLLVM_SPIRV_LIB="$R/usr/lib/aarch64-linux-gnu/libLLVMSPIRVLib.so" \
  -DHOST_LLVM_SPIRV="$R/usr/bin/llvm-spirv-18" \
  -DHOST_SPIRV_LINK="$R/usr/bin/spirv-link" \
  -DCMAKE_INSTALL_RPATH="$R/usr/lib/aarch64-linux-gnu;$R/usr/lib/llvm-18/lib"
nice -n 19 ionice -c3 ninja -C "$T/build/pocl" -j2
nice -n 19 ionice -c3 ninja -C "$T/build/pocl" install
export SPIRV_VAL=$R/usr/bin/spirv-val
export OCL_ICD_VENDORS=$T/pocl/etc/OpenCL/vendors/pocl.icd
export POCL_DEVICES=cpu
export OPENCL_CFLAGS="-I$R/usr/include"
export OPENCL_LIBS="-L$R/usr/lib/aarch64-linux-gnu -Wl,-rpath,$R/usr/lib/aarch64-linux-gnu -lOpenCL"
# Return to the Omega checkout before running its acceptance gate.
```

Confirm CMake reports `ENABLE_SPIRV`, `HOST_CPU_ENABLE_SPIRV`, and `ENABLE_ICD`
enabled. In particular, omitting `ocl-icd-dev` can disable ICD support despite
the requested option. These are oracle setup checks, not permission to bypass
the acceptance gate or use another device.
