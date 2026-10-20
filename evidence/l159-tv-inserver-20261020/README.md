# L159: tree-verify GEMM slow inside the server (2026-10-20)

Symptom: aien-serve-http batch door (sc speed5/batching 946f6fed, omega speed5/handle-slots c79e2fe1 = cascade/tree-verify-gemm
22446aef + 4096 handle slots), AIEN_Q38_BATCH_KERNEL=tree, 1 row/step: "gemm launches" (sum of OmegaGpuTvInfo.chip_ns over the 256
omega_gpu_tv_f32 calls of a step) 503 ms/step vs ~150 ms for the same launches in gpu_tcgemm_test --tv.

Cause (host side, not the chip):
1. chip_ns is host wall time from submit until the host sees the completion marker. The tv path reused omega_gpu_pf_launch_locked with
   PF_SPIN_US = 200 ms busy spin; a spinning nice-10 thread on a loaded box is descheduled and the run-queue wait is counted as chip time
   (base server decode thread, 10 s schedstat: cpu 1064 ms, run-queue wait 2169 ms = 67% calc:9e9d4b5e).
2. A launch whose spin_us is 0 inherits the process-wide window (omega_gpu_session_spin_us), and aien-serve-http sets that to 2000 us at
   start-up (crates/aien-omega-gpu DAEMON_DEFAULT_SPIN_US). The first commit (3208be4b) passed 0, so the tv GEMM kept spinning through
   every launch: window-3 camera on the decode thread = cpu + run-queue wait filling the interval, ~1.5 slices per launch.

Fix: 3208be4b (omega_gpu_pf_launch_locked_spin; Linux poll_word = 20 us nanosleep with 1 us timer slack; test prints min/max/mean)
+ 78c93b35 (OMEGA_GPU_SESSION_SPIN_POLL_ONLY = 1 us, the tv path passes it; host test POLL_ONLY_IGNORES_PROCESS_SPIN).
Host tests: test_omega_gpu_session_platform 14/0, test_omega_gpu_platform_linux 8/0.

Chip, window 4 (20261020T122040Z-a514c0, 12:22-12:32Z, box load 70-88 on 20 cores, chip4-1222Z/):
| run                         | wall ms/step | gemm launches ms/step            | decode thread per ~11 s                 |
| fix2 server nice 10 (L129)  | 207.4, 218.4 | 170.0 calc:0f5e9f0f, 177.8 calc:a23e8f35 | cpu 1.5-2.1 s, wait 0.9-3.0 s, 165-330k slices |
| fix2 server nice 0          | 210.2, 216.6 | 171.8 calc:a15ed72f, 174.0 calc:1bb0aeee | same signature                           |
| base server nice 10         | 218.7, 260.3 | 175.4 calc:9d5bf49e, 214.3 calc:653e08dc | cpu 6.4-8.4 s, wait 1.5-3.4 s, 5-13k slices |
Isolated gpu_tcgemm_test --tv, nice 0, 64 reps, mean us base vs fix2: gu 946/966, mix 439/491, amix 400/427, o 161/287, down 556/601;
weighted step sum (64 x (gu+down+o) + 48 mix + 16 amix, INFERRED composition): base 133.9 ms calc:be147389, fix2 149.1 ms calc:c4ced038.
Ratios: fix2 in-server / 150 ms brief reference = 1.19 calc:48e27c70 (worst prompt); fix2 in-server / its own isolated sum = 1.19
calc:52d8ec23; base in-server / its isolated sum = 1.60 calc:4ed158a4. fix2 vs base, same window, worst prompt: 17% fewer ms calc:858bc115.
Scheduling slices per launch: fix2 22.1 calc:3081a06b (sleep-poll), base 0.834 calc:580192e0 (spin). CPU-only control (spinwait_variants,
256 x 1 ms): spin 96-179 slices, sleep-poll 9.1-9.6k.

Earlier windows (same folder): chip-0759Z (3208be4b, inherited spin, nice 10, load 34-62): fix 629.7/732.9/533.6/734.2 ms/step vs base
698.2/817.9 (calc:bba74ab7 calc:191538bf calc:f533e0a2 calc:640889be calc:e7f98f1e calc:652cf37b). chip3-1152Z (3208be4b, nice 0, load
49-57): 210.1/235.8 ms/step (calc:a3dc3fc4 calc:1bd0602c), camera = spin signature; base at nice 0 refused (channel create 0x51).

Limits: one box, loads 34-88 and changing between windows (base at nice 10 was 698-818 ms/step in window 2 and 175-214 in window 4);
two prompts per server run; isolated o-shape median 227 vs 160 us with the sleep-poll at load 88 (wake-up latency), not seen in-server.
Files: run.log per window, camera.txt (schedstat deltas), test-*.txt, srv-*.spell.txt (server "spell over" lines), ground.txt, PINS,
spinwait_variants.c / spinwait_repro.c (CPU-only repro), run*.sh. Full record sealed in ~/workspace/investigations/2026-10-20-tv-inserver.
