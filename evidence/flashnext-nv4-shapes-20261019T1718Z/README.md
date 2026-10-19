# NV4 GEMV: shapes Flash-Next needs, and the two test fixes (2026-10-19T17:18Z)

## Shapes the model needs (nvfp4-tensors.txt, from the checkpoint headers)
The only NVFP4 tensors are the routed experts (48 layers x 512, plus one PLE scalar that is no GEMV). The shared
expert has the same shapes. OMEGA_NV4_STEP = 512 columns.

| GEMV | n (rows out) | k | full steps | tail cols | rows per warp in production |
|---|---|---|---|---|---|
| gate+up fused (routed and shared) | 1280 | 2560 | 5 | 0 | 2 (omega_gpu_nv4_spec_for default) |
| down (routed and shared) | 2560 | 640 | 1 | 128 | 2 |

Refused shape: rows 8 with a k tail. It would need two 8-row step bodies, more than the 255 registers a
thread can hold (the allocator fails). The only tail shape is down at k 640, and it runs at rows 2, so no Flash-Next
launch hits the refused case. Rows 8 at k 2560 (no tail) still builds: 924 insns, 104 GPRs (host-only.log). The
refusal is explicit in omega_nv4_gemv_spec_ok, and the host test CHECKs it. It is not a silent skip.

## Mutant 2 at k 640 ("caught (rc 0, 0 bad rows)")
The mutant WAS applied: the scale pointer increment becomes 0. It is equivalent to production at k 640.
The loop reads the scale pointer once per full step and only then advances it. With 1 full step, nothing reads the
advanced value. The tail step recomputes its scale address from the row base (ps = slt + psr0). The earlier sim
proved this: output bit-identical, 0 bad rows. It is not a check blind spot.
Fixes:
- the spec refuses mutant 2 when k has fewer than 2 full steps;
- the battery CHECKs that every mutant 1..7 is caught on at least one shape. Mutant 2 is caught at k 2560 (5 steps);
- "caught" now means ran and changed the result, or a kernel fault. A refused build or a simulator configuration
  error no longer counts. The old test scored rc != 0 as caught.
Mutant 7 (tail mask removed) is refused at k 2560 for the same reason: no tail exists there. It is caught at k 640.

Result: host 55/55, sim 29/29 PASS (host-only.log, sim.log, loadavg recorded). Chip run is pending the GPU lock.
