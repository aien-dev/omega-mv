#!/usr/bin/env python3
"""Planted bugs for the negative controls (evidence/L401-cubin-reader/README.md).
usage: make_reader_mutants.py REAL.osc OUTDIR -> OUTDIR/mut_arch.osc, OUTDIR/mut_fits.osc"""
import pathlib, sys

real = pathlib.Path(sys.argv[1]).read_text()
out = pathlib.Path(sys.argv[2])
# mut_arch: ignore the ABI version, always read the SM from e_flags bits 0..7
a_old = """    if b[8] == 8 {
        return (f >> 8) & 255;
    }
    return f & 255;"""
# mut_fits: the naive bound o + z <= n, which wraps for offsets near 2^64
f_old = """    if o > n {
        return 0;
    }
    if z > n - o {
        return 0;
    }
    return 1;"""
f_new = """    if o + z > n {
        return 0;
    }
    return 1;"""
assert real.count(a_old) == 1 and real.count(f_old) == 1
(out / "mut_arch.osc").write_text(real.replace(a_old, "    return f & 255;"))
(out / "mut_fits.osc").write_text(real.replace(f_old, f_new))
print("mutants written to", out)
