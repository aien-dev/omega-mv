# R3 portability table rerun on omega-mv main 62d1d0d6 (#596)

Branch `w2/596-runtime-portability`: the recovered R3 candidate `qualification/runtime-r3-L636@0dbed9c1`
(original `runtime/r3-portability@92cd9145`) cherry-picked onto main `62d1d0d668f897386a0e311374f8dbcc5c32dde8`.
Host only (aarch64 Linux), no GPU.

| File | Command | Result |
|---|---|---|
| `red-old-table-on-main.log` | `sh tools/runtime_portability_check.sh . <0dbed9c1 table> mk/omega-gpu-engine.mk` | exit 1: 21 files that landed on main after a8ffd2b9 have no row (the check refuses main drift) |
| `test-runtime-portability.log` | `make test-runtime-portability` with the 123-row table | exit 0: check PASS (123 rows) and all 8 controls behaved |
| `review-claims-green.json` | `python3 evidence/RUNTIME-R3-RECOVERY-L636/check-review-claims.py` | exit 0: the six L637 review corrections still hold on main sources |
| `gpu-engine-freestanding.log` | `make -B gpu-engine-freestanding` | exit 2: every file compiles freestanding; the closure link refuses `abort mmap mprotect munmap pthread_*` from `src/omega_om_units.c` (OM-2 cutover 450bbefd, #747). Not caused by this branch. |

The 21 new rows are a source-reading judgement (20 CHIP_SPECIFIC, `src/omega_gpu_scan_ref.c` PORTABLE); the check proves
coverage and include agreement, not that each reason is right.
