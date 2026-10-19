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

## Handle slots 1024 -> 4096 (second commit)

The streamed Qwen3.8 model (sc#677 READ DRUM) keeps one graph and one state object per 4-layer group per
conversation: 12 buffers per group, 192 per conversation, plus about 400 for weights. With 1024 slots the
batch-4 run was refused at open (`gdn alloc: CHIP_FAIL: dma_alloc: REFUSED_LIMIT`). Slots now match the
physics nvrm live table (`NVRM_MAX_LIVE 4096`), which refuses cleanly ("live allocation table full") past
that point. Docs: Context7 /nvidia/open-gpu-kernel-modules is silent on a per-client RM object cap (the only
cap found is 128 handles per dma-buf export call, a path we do not use).

| check | command | result |
|---|---|---|
| platform + session tests | `make PHYSICS_DIR=... test-gpu-engine-freestanding test-gpu-session-platform` | handle-slots-4096-tests.log: 78 PASS lines, session 13/13, linux reference 8/8, 6 of 6 mutants killed; HANDLES_TYPED_GENERATIONED_LIMITED fills all 4096 slots and is refused on the next |
| archive | `make OUT_DIR=<fresh> PHYSICS_DIR=... <out>/libomega_gpu.a` | archive-slots4096.sha256 |
