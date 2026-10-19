# two-front arena chip gate (omega#571), 2026-10-19T02:24Z

Code: omega c78ccbcc (fortran/3-two-front-arena, base qwen38/prefill-graph 6e24f7fd); the runner script
tools/run_gpu_two_front_chip.sh was untracked at run time (committed next, unchanged). Physics 6d7cf0d4.
Command: `OUT_DIR=/tmp/f3-two-fronts-scratch/b1 MAX_POS=131072 REPS=15 nice flock /tmp/aien-gb10.lock tools/run_gpu_two_front_chip.sh evidence/two-front-chip-20261019T0220Z`
Box: the live Qwen3.8-27B server was up; MemAvailable 64.1 GB at start; the test refuses any allocation that leaves < 12 GiB.

Verdict: GPU_TWO_FRONT_CHIP: PASS (chip.log: 327 checks, 0 failed; host-only 35/35; sim 55/55;
red-break-overflow.log: the suite FAILS with the overflow check skipped, as required).

Exact-recall gate (plane cache vs a cache bound to the arena, same rows, same inputs):
- decode attention: 11 of 11 positions bit-identical (outputs and appended K/V rows), positions 0 .. 131071.
- prefill attention: 4 of 4 chunks bit-identical (outputs and written rows), chunks crossing block edges, up to 131072.
- DeltaNet: live set in the arena == own memory for 6 tokens x 2 layers; snapshot at token 3, restore, replay: identical,
  final tails and states identical.
Controls (each must differ / fail, each did): decode mutant 6 (in-block mask dropped), attend mutant 3 (no block jump),
overflow check skipped (KV front overruns the snapshot: snapshot corrupted, omega_tf_check reports the crossing).
Overflow: fronts meet with gap 0 after 1024 positions; the 1025th..1280th block request refused ("short by 16777216"),
snapshot intact after decoding to the last granted position.

Driver bytes (session counter): old 16-layer cache at 131072 positions 8,737,394,688 B in 26 allocations; the shared
cache without KV (RoPE table, norms, partials, staging) 146,993,152 B; 48-layer DeltaNet in own memory 173,154,304 B.
Decode attention chip time, median of 15 (plane / block): pos 1699 204,210 / 203,986 ns; pos 29999 1,821,775 / 1,829,311 ns;
pos 131071 7,713,534 / 7,652,173 ns. Arena zeroing (host memset, 8,617,472,000 B): 135.9 ms.
