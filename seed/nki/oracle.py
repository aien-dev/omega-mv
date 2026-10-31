# SPDX-License-Identifier: Apache-2.0
# interop oracle, not Omega-native; never generates kernels.
#
# Host dependency (oracle only): CPython 3.12 + nki 0.7.0 (Apache-2.0, AWS) + numpy 2.2.6.
# Omega replacement: none yet. The kernels come from seed/nki/irnki.om; the expected
# values come from a native Omega program built by gate-irnki.sh. This script only
# (1) writes deterministic input lanes and (2) runs the emitted kernels on the public
# NKI CPU simulator (nki.simulate) and compares the result bits with the Omega values.
#
#   oracle.py version
#   oracle.py inputs uint32|float32 > in.bin          a lanes then b lanes, little-endian
#   oracle.py check kernels.py in.bin refdir name...  exit 0 all equal, 1 any differ
#   oracle.py limit kernels.py name                   exit 0 if a (128, 500) call is refused
import importlib.util
import os
import sys

ROWS, COLS = 256, 1024
N = ROWS * COLS


def die(msg, code):
    sys.stderr.write("oracle: " + msg + "\n")
    sys.exit(code)


try:
    import numpy as np
    import nki
except Exception as ex:  # the gate maps exit 3 to NOT_RUN
    die("cannot import the simulator: %s: %s" % (type(ex).__name__, ex), 3)


def load(path):
    spec = importlib.util.spec_from_file_location("omnki", path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod.KERNELS


def inputs(dtype):
    rng = np.random.default_rng(20261026)
    a = rng.integers(0, 2 ** 32, N, dtype=np.uint64).astype(np.uint32)
    b = rng.integers(0, 2 ** 32, N, dtype=np.uint64).astype(np.uint32)
    if dtype == "uint32":
        edge = [0, 1, 2, 0x7FFFFFFF, 0x80000000, 0x80000001, 0xFFFF, 0x10000, 0xFFFFFFFE, 0xFFFFFFFF]
    elif dtype == "float32":
        # +0 -0 +inf -inf qNaN max -max min-normal smallest/largest subnormal 1 -1 1+ulp 2^24
        edge = [0x00000000, 0x80000000, 0x7F800000, 0xFF800000, 0x7FC00000, 0x7F7FFFFF,
                0xFF7FFFFF, 0x00800000, 0x00000001, 0x007FFFFF, 0x3F800000, 0xBF800000,
                0x3F800001, 0x4B800000]
        # the rest: half raw bit patterns (all exponents), half normal values near 1
        k = N // 2
        a[k:] = rng.standard_normal(N - k).astype(np.float32).view(np.uint32)
        b[k:] = rng.standard_normal(N - k).astype(np.float32).view(np.uint32)
    else:
        die("inputs: dtype must be uint32 or float32", 2)
    e = np.array(edge, dtype=np.uint32)
    m = len(edge)
    a[: m * m] = np.repeat(e, m)
    b[: m * m] = np.tile(e, m)
    sys.stdout.buffer.write(a.astype("<u4").tobytes() + b.astype("<u4").tobytes())


def check(path, inpath, refdir, names):
    kernels = load(path)
    if sorted(kernels) != sorted(names):
        die("kernel set %s differs from expected %s" % (sorted(kernels), sorted(names)), 1)
    raw = np.frombuffer(open(inpath, "rb").read(), dtype="<u4")
    if raw.size != 2 * N:
        die("input size %d lanes, expected %d" % (raw.size, 2 * N), 1)
    target = os.environ.get("NEURON_PLATFORM_TARGET_OVERRIDE", "default")
    bad = 0
    for name in names:
        fn, dtype = kernels[name]
        a = raw[:N].copy().reshape(ROWS, COLS)
        b = raw[N:].copy().reshape(ROWS, COLS)
        if dtype == "float32":
            a, b = a.view(np.float32), b.view(np.float32)
        elif dtype != "uint32":
            die("%s: unknown lane dtype %s" % (name, dtype), 1)
        got = np.asarray(nki.simulate(fn)(a, b))
        if got.shape != (ROWS, COLS) or got.dtype.itemsize != 4:
            die("%s: result shape %s dtype %s" % (name, got.shape, got.dtype), 1)
        g = got.reshape(N).view(np.uint32)
        r = np.frombuffer(open(os.path.join(refdir, name + ".bin"), "rb").read(), dtype="<u4")
        if r.size != N:
            die("%s: reference size %d lanes" % (name, r.size), 1)
        diff = g != r
        nanpair = 0
        if dtype == "float32":
            # NaN payload and sign are not part of the IEEE result; compare NaN as a class.
            gn = (g & 0x7F800000 == 0x7F800000) & (g & 0x007FFFFF != 0)
            rn = (r & 0x7F800000 == 0x7F800000) & (r & 0x007FFFFF != 0)
            nanpair = int((diff & gn & rn).sum())
            diff = diff & ~(gn & rn)
        n = int(diff.sum())
        state = "PASS" if n == 0 else "FAIL"
        print("%s target=%s kernel=%s dtype=%s lanes=%d mismatches=%d nan_class_only=%d"
              % (state, target, name, dtype, N, n, nanpair))
        if n:
            i = int(np.flatnonzero(diff)[0])
            print("  first lane %d a=%08x b=%08x simulator=%08x omega=%08x"
                  % (i, raw[i], raw[N + i], g[i], r[i]))
            bad += 1
    sys.exit(1 if bad else 0)


def limit(path, name):
    fn, dtype = load(path)[name]
    a = np.zeros((128, 500), dtype=np.dtype(dtype))
    try:
        nki.simulate(fn)(a, a)
    except Exception as ex:
        print("REFUSED kernel=%s shape=(128, 500) %s" % (name, type(ex).__name__))
        sys.exit(0)
    die("%s accepted shape (128, 500)" % name, 1)


def main(argv):
    if len(argv) == 2 and argv[1] == "version":
        print("nki %s numpy %s python %s" % (nki.__version__, np.__version__, sys.version.split()[0]))
    elif len(argv) == 3 and argv[1] == "inputs":
        inputs(argv[2])
    elif len(argv) >= 6 and argv[1] == "check":
        check(argv[2], argv[3], argv[4], argv[5:])
    elif len(argv) == 4 and argv[1] == "limit":
        limit(argv[2], argv[3])
    else:
        die("usage: see header", 2)


if __name__ == "__main__":
    main(sys.argv)
