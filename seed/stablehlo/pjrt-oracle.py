# SPDX-License-Identifier: Apache-2.0
# Interop oracle, not Omega-native; never generates kernels.
# Compiles an existing StableHLO MLIR text file (written by irstablehlo.om) with
# the XLA CPU PJRT client shipped in jaxlib, executes @main on the host CPU and
# compares every result element bit-for-bit with an expected vector.
# jax.jit and every jax tracing API are unused: the module text is the only input.
# Host dependency: jaxlib (Apache-2.0), version printed on every run. Omega
# replacement: a PJRT C API caller in .om once Omega can load a C ABI plugin
# (FR-7 in docs/one_layer/OM_COMPLETION.md).
# Usage: pjrt-oracle.py MODULE ELEMTYPE N EXPECTED INPUT...
# Lists are comma-separated decimals. Exit 0 match, 1 mismatch, 2 module refused
# by the oracle, 3 oracle unavailable, 4 bad arguments, 5 execution error.
import os
import sys

os.environ["JAX_PLATFORMS"] = "cpu"

TYPES = {
    "i8": "int8", "i16": "int16", "i32": "int32", "i64": "int64",
    "ui8": "uint8", "ui16": "uint16", "ui32": "uint32", "ui64": "uint64",
    "f32": "float32", "f64": "float64",
}


def main(argv):
    if len(argv) < 5 or argv[2] not in TYPES:
        print("usage: pjrt-oracle.py MODULE ELEMTYPE N EXPECTED INPUT...", file=sys.stderr)
        return 4
    try:
        import numpy as np
        import jax
        import jaxlib
        from jax._src.lib import xla_client as xc
    except ImportError as exc:
        print(f"ORACLE_MISSING {exc}", file=sys.stderr)
        return 3
    dtype = np.dtype(TYPES[argv[2]])
    count = int(argv[3])

    def vector(text):
        words = text.split(",")
        if len(words) != count:
            raise ValueError(f"expected {count} elements, got {len(words)}")
        if dtype.kind == "f":
            return np.array([dtype.type(w) for w in words], dtype=dtype)
        info = np.iinfo(dtype)
        values = [int(w) for w in words]
        if any(v < info.min or v > info.max for v in values):
            raise ValueError(f"value outside {dtype}")
        return np.array(values, dtype=dtype)

    try:
        expected = vector(argv[4])
        inputs = [vector(t) for t in argv[5:]]
    except ValueError as exc:
        print(f"BAD_ARGUMENT {exc}", file=sys.stderr)
        return 4
    devices = jax.devices("cpu")
    client = devices[0].client
    print(f"PJRT jax={jax.__version__} jaxlib={jaxlib.__version__} "
          f"platform={client.platform} platform_version={client.platform_version}")
    with open(argv[1], encoding="utf-8") as handle:
        module = handle.read()
    try:
        executable = client.compile_and_load(
            module, xc.DeviceList(tuple(devices[:1])), xc.CompileOptions())
    except Exception as exc:  # the oracle's own refusal is the evidence here
        print(f"MODULE_REFUSED {type(exc).__name__}: {str(exc).splitlines()[0]}")
        return 2
    # jax_enable_x64 keeps 64-bit element types; device_put would otherwise
    # narrow them to 32 bits. It changes buffer transfer only, not the module.
    jax.config.update("jax_enable_x64", True)
    try:
        buffers = [jax.device_put(x, devices[0]) for x in inputs]
        results = executable.execute_sharded(buffers).disassemble_into_single_device_arrays()
    except Exception as exc:
        print(f"EXECUTE_ERROR {type(exc).__name__}: {str(exc).splitlines()[0]}")
        return 5
    if len(results) != 1:
        print(f"MISMATCH result count {len(results)}")
        return 1
    observed = np.asarray(results[0][0])
    print("OBSERVED " + ",".join(repr(v.item()) for v in observed))
    if observed.dtype != dtype or observed.shape != expected.shape:
        print(f"MISMATCH type {observed.dtype}{observed.shape} expected {dtype}{expected.shape}")
        return 1
    bits = np.dtype(f"u{dtype.itemsize}")
    diff = np.nonzero(observed.view(bits) != expected.view(bits))[0]
    if diff.size:
        i = int(diff[0])
        print(f"MISMATCH index {i} observed {observed[i]!r} expected {expected[i]!r}")
        return 1
    print("MATCH bitwise")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
