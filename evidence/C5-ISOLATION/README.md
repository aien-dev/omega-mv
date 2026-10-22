# C5: two processes, and a never-ending kernel (docs/OMEGA.md row C5)

Row C5: two processes on one GPU are both correct, or the second gets a named refusal and the first is unharmed. A
never-ending kernel times out, then a correct launch succeeds in the same process.

What this branch adds (on c4/streams):
- `omega_gpu_session_recover()`: the way back from a latch without restarting the process. Runs the on-close hooks
  (still latched, so nothing is released while the old grid may run), closes the device (Linux reference: the RM client
  close destroys this process's channels and the grid on them, then frees and unmaps every buffer), and only then lifts
  the latch and clears the parked list. The next open is a new device. `omega_gpu_session_recoveries()` counts them;
  a command list latched before the latest recovery no longer blocks.
- Fake-platform case TIMEOUT_RECOVER_SAME_PROCESS and three new session mutants (recover keeps the latch, recover
  without the close, recover when not latched): each one is caught.
- `tests/gpu_isolation_test.c`: the chip test (hang + recover; two processes; a hang beside a working process).

Driver docs: the vendored NVIDIA headers document NVA06C_CTRL_CMD_PREEMPT ("This command preempts a channel group") but
are silent on freeing a channel group whose grid never ends (RM source not vendored). The chip run is the proof.

| file | what | result |
| --- | --- | --- |
| 01-host-only.log | codegen + encode of the work, mutant and never-ending kernels; simulator exact, mutant caught | PASS |
| 02-session-platform.log | the session on a fake GPU, recover case included | PASS 14/14 |
| 03-session-mutants.log | 9 broken copies of the session, each caught | 9/9 killed |
| 04-never-ending-kernel.sass.txt | our disassembler: `IADD3; BRA 0x20` (branch back for ever) | as designed |
| (chip, pending slot) | hang + recover + right values; two processes right; hang beside a working process | NOT RUN |

Chip command (GB10 lock): `build/gpu_isolation_test`. Safety note for the slot: the never-ending kernel is one warp;
its teardown is the RM channel free, which must preempt it; this is the first chip run of that path.
