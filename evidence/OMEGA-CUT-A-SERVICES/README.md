# OMEGA-CUT-A-SERVICES (L202): host evidence

Cut A of aienos `docs/GB10_NATIVE_DEPENDENCY_MAP.md`: the platform services table,
the shared push-buffer encoding header, typed seat buffers and the freestanding
closure growing to 16 files. Contract and file list: `docs/numeric/OMEGA_PLATFORM_SERVICES.md`.

Host only. No GPU, no device node, no silicon run. Base omega main a8ffd2b9,
pinned physics `~/workspace/physics-pf-6d7cf0d4` (physics.lock 6d7cf0d4),
`AIENOS_LOCK_REPO=~/workspace/pipeline/clones/aienos` (holds b84c0a67).
Compiler: `compiler.txt`. Every gate ran through `quiet-build`.

## Commands

- `gates.sh before` on the untouched base, `gates.sh after` on this branch with
  `EXTRA_TARGETS="test-gpu-encoding test-platform-services test-rx-seat-fake test-r16-authpath-linkmap-silicon"`.
  Each line of `before/summary.txt` and `after/summary.txt` is `<make target> <exit code>`.
- `rerun.sh`: the two new gates after their test-side fixes, then the seat over the
  simulated driver 20 times with the old seat code and 20 times with the new, compared
  pair by pair (`after2/`).

## Results (OBSERVED)

| Check | Before | After |
|---|---|---|
| 17 existing host gates (engine, freestanding, codegen, session platform, wait, sim, serving-alloc, elementwise, cmdlist, matmul-api, divsqrt, ldst, code-alloc, decode-graph, both mutant sets) | exit 0 | exit 0 |
| `test-gpu-attention` | exit 2, 16 of 107 failed | exit 2, the same 16 (fail lists identical: `*/test-gpu-attention-fail-list.txt`) |
| R12, R13, R14 silicon binaries (build only, not run) | exit 2 in the gate run (default lock repo lacks b84c0a67), then built with the lock repo set (`before/silicon-and-seat-build.log`) | exit 0 |
| `test-r16-authpath-linkmap-silicon` | not run | exit 0 |
| Freestanding partial link | 14 files, only memcpy memset strcmp strlen undefined | 16 files, the same four |
| `test-gpu-encoding` | new | exit 0: 1090519040 headers and 83886096 GPFIFO entries equal physics; mutant control finds both planted differences (`after/test-gpu-encoding.txt`) |
| `test-platform-services` | new | see `after2/summary.txt` |
| `test-rx-seat-fake` | old seat 23/23, stdout and stderr identical over 2 runs (`before/seat-fake.*`) | see `after2/` |
| Object code (objdump of all 82 `build/*.o`) | `before/objdump.sha256` | `after/objdump.sha256`: 80 identical. Different, as intended: `omega_gpu_elementwise_api.o` (moved onto session services) and `omega_gpu_fmt.o` (body moved into `omega_gpu_vfmt`) |

Token-path objects with identical machine code: `omega_gpu_session.o`,
`omega_gpu_cmdlist.o`, `omega_gpu_matmul_api.o`, `omega_gpu_attention_api.o`,
`omega_gpu_platform_linux.o`, `omega_gpu_decode_graph.o` and the rest of the 80.
The removed `bw_mthd` copies compiled to the same instructions as `omega_gpu_mthd`.

## Seat test race (found and fixed in the test)

The first after run of `test-rx-seat-fake` failed one check, "lease thread still
running after kill", in about half the runs. The old seat code built against the
same test failed the same check 6 of 12 runs, so the race was in the test, not the
seat. INFERRED (high): after `pthread_join` returns, the joined thread can stay
listed in `/proc/self/task` for a short time. The check now polls for up to 500 ms.

## Not run, not claimed

- GB10 golden-token run and the R12 to R16 silicon gates: NOT_RUN. The L202 brief
  forbids device access. Substitute evidence: identical object code on the token
  path and identical simulated-driver traces for the seat.
- Speed: no claim. Nothing was measured with `quiet-measure`.
