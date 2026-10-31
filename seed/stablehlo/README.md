# seed/stablehlo: OSC IR scalar unit to StableHLO (Google TPU path, first rung)

`irstablehlo.om` is a handwritten Omega program. It reads one OSC IR unit on stdin and writes a
StableHLO MLIR module that applies one named scalar function elementwise to `N`-element tensors:

```
irstablehlo N FUNCTION < unit.ir > module.mlir
```

For `fn vaddi32 a int b int to int / ret add a b` and `N = 8` it writes:

```
module {
  func.func public @main(%p0: tensor<8xi32>, %p1: tensor<8xi32>) -> tensor<8xi32> {
    %i0 = stablehlo.add %p0, %p1 : tensor<8xi32>
    func.return %i0 : tensor<8xi32>
  }
}
```

## Run the gate

```
IREE_COMPILE=... IREE_RUN=... PYTHON=... nice -n 19 sh seed/stablehlo/gate-irstablehlo.sh
```

Exit 0 PASS, 1 FAIL, 3 NOT_RUN (not AArch64 Linux, or an oracle missing). Artifacts go to
`build/seed/stablehlo`. The gate pins `seed/out/omc` by sha256, builds the translator, translates every
row of `cases.tsv` twice (byte-identical), compiles and runs each module on two oracles, and compares
every element exactly. Integer rows are also run through the same Omega function compiled natively by
omc. It then requires these to be caught: a wrong op (add to subtract, add to multiply), a wrong
expected value (including one f32 ulp), three malformed modules both oracles must refuse, and 16
translator refusals that must leave stdout empty.

## Oracles (interop oracle, not Omega-native)

Neither oracle writes the module; both only read what Omega wrote. jax.jit and JAX tracing are unused.

| Oracle | Version (installed 2026-10-26 in a user venv) | Wheel sha256 | Licence |
|---|---|---|---|
| IREE `iree-compile` (llvm-cpu) | iree-base-compiler 3.13.0 (`3.13.0rc20261016 @ 89ece87b`) | `230aec9b5d63c49f3f1ba228c35a7ce950075ce7b7780b786c85fa6fb0be3dbb` | Apache-2.0 |
| IREE `iree-run-module` (`--equality_mode=exact`) | iree-base-runtime 3.13.0 | `2a46a517c7dbdaa8701f9ee324dd94089af51a8251f36bd689949ee37e30d1d1` | Apache-2.0 |
| XLA CPU PJRT client, via `pjrt-oracle.py` | jaxlib 0.12.0.1 | `40812201c3bcd4cb104b88f71f58fec4cbd0c32ba90e5c92b3fa8f40819f6774` | Apache-2.0 |
| (driver dependency) | jax 0.12.0.1 / numpy 2.5.4 | `e9dfd256…` / `fe4d21ab…` | Apache-2.0 / BSD-3-Clause AND 0BSD AND MIT AND Zlib AND CC0-1.0 |

`pjrt-oracle.py` is a foreign-language oracle driver: it only compiles and runs an existing module and
compares numbers. Its Omega replacement is a PJRT C API caller written in `.om`, which needs shared
object and C ABI support (FR-7 in `docs/one_layer/OM_COMPLETION.md`, absent at this pin).

IREE is called with `--iree-input-demote-f64-to-f32=false --iree-input-demote-i64-to-i32=false`;
without them IREE 3.13 rewrites f64 functions to f32 by default.

## What is established, and what is not

- IMPLEMENTED: the translator for one-block functions over one element type: BIN add, subtract,
  multiply (all types), divide (f32/f64 only), and, or, xor (integers), integer CONST, MOV, RET.
  Types ui8..ui64, i8..i64, f32, f64. Wire v1 (as `omc ir` writes it) and wire v7 for f32/f64.
- EXERCISED on the oracles: i32, ui32, i64, multiply-add and constant rows, plus f32 and f64 add,
  all matching exactly on both oracles at N = 8.
- f32/f64 IR is a hand-written wire v7 fixture in the gate, because `omc ir` refuses float words at
  this pin. No Omega producer of float IR exists yet, so the float rows have no native cross-check.
- NOT_RUN: any TPU. No result here is hardware qualification.

Refused (with the class on stderr and nothing on stdout): integer div, rem, shl, shr; UN, CMP, CAST,
CALL, TRAP, BR, CBR; more than one block; requires/ensures contracts; bool; float constants; mixed
element types; structs, regions, refs; f32/f64 tags in wire v1.

### Semantics

| Omega rule | StableHLO (spec, read 2026-10-26) | Status |
|---|---|---|
| Signed add/sub/mul trap 1 on overflow | Integer overflow is implementation-defined ("Errors") | NOT preserved. The gate shows Omega exiting 101 while both oracles return -2147483648 |
| Unsigned add/sub/mul wrap | Implementation-defined | Same on both oracles; not a spec guarantee |
| IEEE-754 float add/sub/mul/div | IEEE-754, exceptions give the default result, no flags | Preserved; NaN payloads are not compared |
| div/rem trap 2, shift trap 7 | No trap | Refused |

## Openness of the Google TPU stack (read 2026-10-26)

| Layer | Status | Source |
|---|---|---|
| StableHLO opset and spec | Open, Apache-2.0 | https://github.com/openxla/stablehlo (`docs/spec.md`, main `44151da3`) |
| XLA compiler, PJRT C API | Open, Apache-2.0 | https://github.com/openxla/xla |
| JAX, jaxlib (CPU PJRT client) | Open, Apache-2.0 | https://github.com/jax-ml/jax |
| Mosaic TPU dialect (Pallas TPU) | Dialect definition public (`jaxlib/mosaic/dialect/tpu/tpu.td`), but it reaches the chip only as a `tpu_custom_call` that libtpu compiles | https://github.com/jax-ml/jax/blob/main/jaxlib/mosaic/dialect/tpu/tpu.td |
| libtpu (TPU compiler backend and runtime) | Closed binary; PyPI licence field "Google Cloud Platform Terms of Service"; x86_64 wheels only (0.0.51) | https://pypi.org/project/libtpu/ |
| TPU instruction set | No public ISA document found | — |
| StableHLO reference interpreter | Open, but prebuilt `stablehlo-translate` wheels are linux x86_64 only | https://github.com/openxla/stablehlo/releases |

So the lowest level Omega can write without closed tools is StableHLO text (or Mosaic TPU-dialect MLIR,
which only libtpu can lower). Everything below that is inside libtpu.

## Path to a TPU receipt (NOT_RUN)

1. Get a Cloud TPU VM through the TPU Research Cloud (https://sites.research.google/trc/). It needs a
   Google Cloud account and acceptance of Google's terms; libtpu ships on the VM.
2. Copy `build/seed/stablehlo/case-*.mlir` (made here, sha256 values from the gate output) to the VM.
   Do not regenerate them there.
3. Run `pjrt-oracle.py` unchanged with `PJRT_ORACLE_PLATFORM=tpu`: the same `client.compile_and_load` on the
   TPU PJRT client, with the `cases.tsv` inputs and expected vectors.
4. Record the libtpu version, TPU generation, module sha256 values and output. Even then the label is
   "EXERCISED on TPU via closed libtpu": the kernel code below StableHLO is Google's, not Omega's.

f64 is not native on TPUs; expect f64 rows to be refused or emulated, and record which.
