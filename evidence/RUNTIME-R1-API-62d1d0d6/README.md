# R1 Runtime API check rerun on omega-mv main 62d1d0d6 (#594)

Branch `w2/594-runtime-api`: `runtime/r1-api@164fb76f` (three commits) cherry-picked onto main
`62d1d0d668f897386a0e311374f8dbcc5c32dde8`. Host only (aarch64 Linux, cc), no GPU, no physics checkout.

| File | Result |
|---|---|
| `red-old-manifest-on-main.log` | exit 1, 9 FAIL lines: the 164fb76f manifest against main's headers. Main raised `OMEGA_GPU_HANDLE_SLOTS` 1024 -> 4096 (7c61dc28) and `OMEGA_GPU_CMDLIST_MAX_LAUNCHES` 2048 -> 4096 (52dd18a8), so `OmegaGpuHandleTable` grew. The check refuses the drift. |
| `test-runtime-api.log` | exit 0: PASS (96 functions, 28 structs, 237 fields, 159 constants) and all 10 negative controls caught. |

Manifest changes on this branch: the two constants and the handle-table layout above; new EXPERIMENTAL entries for main's
C5/C4 additions (`omega_gpu_session_recover`, `omega_gpu_session_recoveries`, `omega_gpu_session_extra_queue`,
`omega_gpu_cmdlist_submit_pair`, `omega_gpu_cmdlist_wait_for`) and the field `OmegaGpuCmdListOpts.first_barrier_none`
(added inside the old struct padding, so the size check could not see it: the known blind spot named in API.md section 11).
`omega_gpu_session_latch_uncertain` and `omega_gpu_session_note_module_latch` are listed INTERNAL.
New control `removed-recover` proves a new EXPERIMENTAL entry is checked, not only the STABLE ones.
