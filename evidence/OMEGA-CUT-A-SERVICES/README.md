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
| 16 existing host gates (engine, freestanding, codegen, session platform, wait, sim, serving-alloc, elementwise, cmdlist, matmul-api, divsqrt, ldst, code-alloc, decode-graph, both mutant sets) | exit 0 | exit 0 |
| `test-gpu-attention` | exit 2, 16 of 107 failed | exit 2, the same 16 (fail lists identical: `*/test-gpu-attention-fail-list.txt`) |
| R12, R13, R14 silicon binaries (build only, not run) | exit 2 in the gate run (default lock repo lacks b84c0a67), then built with the lock repo set (`before/silicon-and-seat-build.log`) | exit 0 |
| `test-r16-authpath-linkmap-silicon` | not run | exit 0 |
| Freestanding partial link | 14 files, only memcpy memset strcmp strlen undefined | 16 files, the same four |
| `test-gpu-encoding` | new | exit 0: 1090519040 headers and 83886096 GPFIFO entries equal physics; mutant control finds both planted differences (`after/test-gpu-encoding.txt`) |
| `test-platform-services` | new | exit 0 after the review fixes: hosted 150 checks, freestanding objects 67 checks, 0 failed (`after3/`) |
| `test-rx-seat-fake` | old seat 23/23, stdout and stderr identical over 2 runs (`before/seat-fake.*`) | exit 0, 23/23. After the review fixes (`after3/`): old seat 20 of 20 and new seat 20 of 20 exit 0, all 20 pairs byte-identical, new trace equal to the baseline. Earlier rerun: old seat 20 of 20 and new seat 20 of 20 exit 0, all 20 old/new pairs byte-identical on stdout and stderr (`after2/compare.txt`, `after2/runs.txt`); the new trace also equals the original baseline (`after2/seat-fake-new.*` vs `before/seat-fake.*`) |
| Object code, all 82 `build/*.o` | disassembly `before/objdump.sha256`; bytes: clean build of main a8ffd2b9 (`objbytes.sh`) | disassembly `after/objdump.sha256`, and the object files themselves byte for byte from a clean build of branch b17df591 (`after2/object-bytes-cmp.txt`, `after2/object-bytes.sha256`): 80 identical. Different, as intended: `omega_gpu_elementwise_api.o` (moved onto session services) and `omega_gpu_fmt.o` (body moved into `omega_gpu_vfmt`) |

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

## Done-review fixes (Codex GPT-6 Astra review, `~/handoffs/L202/advisor-done.txt`)

Fable was at its usage limit, so the done-review ran on Codex. Its findings and what was done:

- Fixed: a failed `pthread_join` no longer retires the task handle.
- Fixed: the image copy the seat hands back is allocated by the world (`rx_world_own_coherent`),
  which frees it; before, an installed non-libc heap would have crossed allocators.
- Fixed: the seat's `atoi` replacement returns what glibc returns, saturation included (errno not set;
  nothing reads it)
  (`after2/atoi-check.*`: 20000026 strings, 0 differ).
- Fixed: the one seat line carrying user text (the pin value) writes it whole, in one write, through the new
  `omega_psvc_diag` when it is too long for a 511-byte line.
- Documented (header and contract doc): 64 live tasks and locks in the hosted table; the
  511-byte `diagf` line; an interrupted wait is a short wait for bounded polling loops.
- Answered with evidence: object identity is now byte for byte, not only disassembly; the
  contract test receipt is `after3/summary.txt` (exit 0).
- Scope: the dependency map gives cut A's "hide `NvrmMem`" to the persistent session (row S2,
  done on main) and the numeric launchers and resident runtime to cut E; recorded in the doc.
- Not testable on the host without undefined behavior: the failed-join path (a valid joinable
  thread does not fail `pthread_join`). INFERRED from code reading.

Rerun after the fixes (`rerun3.sh`, `after3/summary.txt`): the contract, seat, freestanding,
engine, elementwise and encoding gates, the R16 link map and the R12, R13, R14 silicon builds,
10 of 10 exit 0. The freestanding link: 16 files, only memcpy memset strcmp strlen undefined.

## Final receipts on the pushed commit 4427e2f1 (`final.sh`, `final/`)

- `gates.sh final` with the four new targets: 23 of 24 exit 0; `test-gpu-attention` exit 2 with the
  same 16 failures as main (`final/test-gpu-attention-fail-list.txt`).
- Disassembly of all 82 objects equal to the after run (`final/objdump.sha256`).
- Object files byte for byte, clean builds of main a8ffd2b9 and 4427e2f1 (`final/object-bytes-cmp.txt`):
  80 identical; `omega_gpu_elementwise_api.o` and `omega_gpu_fmt.o` differ, as intended.

## Second review and final receipts on 52331f76 (`final2.sh`, `final2/`)

Codex's second pass (`review-2-codex.txt`) marked the first review's points resolved or documented
and raised two: the long pin line was written in three pieces (could interleave with other stderr
writers; now built on the heap and written once) and "glibc-exact" overstated the parser (it
returns what glibc returns; errno is not set, and nothing reads it; wording fixed). The seat test
gained `life_badpin` (a 20-digit value, atoi saturation) and `life_badpin_long` (700 digits).

- `gates.sh final2`: 23 of 24 exit 0; `test-gpu-attention` exit 2 with the same 16 failures as main.
- Seat: 43 checks, 0 failed. The old seat (rebuilt with the same test) and the new one, 20 runs
  each, all exit 0; all 20 pairs byte-identical on stdout and stderr, including the 733-byte
  "could not pin" line (`final2/seat-fake-old.err`, `final2/seat-fake-new.err`).
- Contract test: hosted 150 checks, freestanding objects 67, 0 failed.
- Freestanding link: no closure source changed after `after3/freestanding-link.txt` (16 files, only memcpy
  memset strcmp strlen undefined), so make did not relink it in this run.
- Object files byte for byte vs main a8ffd2b9: 80 of 82 identical; the elementwise API and the
  formatter differ, as intended (`final2/object-bytes-cmp.txt`).
