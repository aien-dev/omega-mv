# FP8 refill door + eight-byte NaN scan (sc #677, FORTRAN idea 5 READ DRUM), 20261019T0301Z

Branch fortran/5-fp8-refill on qwen38/prefill-graph 6e24f7fd. Archive built fresh: make OUT_DIR=<fresh>
PHYSICS_DIR=~/workspace/physics-6d7cf0d4 (sha256 in archive.sha256).

| Check | Command | Result |
| --- | --- | --- |
| host battery + scan equivalence | gpu_fp8_api_test --host-only | host-only.log: 1614 checks PASS; scan 15.67 GB/s eight-byte vs 3.25 GB/s byte loop over 64 MiB |
| scan test can fail | mutant: skip words with (i & 64) in omega_fp8_pack_scan_nan | FAIL "scan differs: len 72 fill 0x00 pos 64 code 1" (plus 2 older checks red); restored, PASS |
| chip refill door | flock /tmp/aien-gb10.lock gpu_fp8_api_test --refill | chip-refill.log: 9 checks PASS; y bit-identical to the uploaded twin; one changed byte caught; NaN refused; gate\|up commit 178,257,920 B in 10.7 ms (16.69 GB/s) |

Why: the sc #677 read drum measured Fp8Tensor upload at 2.3 GB/s (70% the byte-loop NaN scan, 18% the allocation),
below the NVMe's 10-12 GB/s. With alloc once + O_DIRECT straight into the weight mapping + commit, nothing is copied.
