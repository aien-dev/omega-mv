# GB10 ceilings, measured by Omega (L272, 2026-10-20)

Question (Drake, 2026-10-20): "how do we keep it on the math cores without in-memory?" This file gives the chip's
real limits as our own engine measures them (not datasheet numbers), then accounts for the gap between the live 27B
decode (7.63 tok/s on 18091) and those limits, then ranks every remaining lever by its predicted gain.

Tags: OBSERVED (measured or read here, with command and exit code), INFERRED (arithmetic from observed parts, with a
calc: receipt), UNKNOWN. Builds on the L123 speed audit (omega `doc/speed-audit`, docs/SPEED_AUDIT_2026-10-19.md) and
the L274 work count (omega `feat/math-before-compute`, docs/speed/WORK_PER_TOKEN.md); neither is repeated here.

## How the numbers were taken

Probes (this branch): `src/omega_blackwell_codegen_ceil.{c,h}` (stream, chase, resid, empty, grid barrier) and
`tests/gpu_ceilings_test.c`; the existing tensor-core probes `tests/gpu_tc_peak_test.c` and
`tests/gpu_qmma_probe_test.c` are reused unchanged.

```
quiet-build make -j4 OUT_DIR=build/l272 PHYSICS_DIR=$HOME/workspace/physics-bytes-6d7cf0d4 \
    build/l272/gpu_ceilings_test build/l272/gpu_tc_peak_test build/l272/gpu_qmma_probe_test
build/l272/gpu_ceilings_test --host-only          # every probe in the warp simulator, values checked
tools/l272_ceilings_run.sh build/l272 evidence/L272-CEILINGS-<UTC>   # each run under quiet-measure flock /tmp/aien-gb10.lock
```

PENDING: tables below are filled from the evidence directory once the chip runs finish.
