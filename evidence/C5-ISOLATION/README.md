# C5: two processes, and a never-ending kernel (docs/OMEGA.md row C5)

Row C5: two processes on one GPU are both correct, or the second gets a named refusal and the first is unharmed. A
never-ending kernel times out, then a correct launch succeeds in the same process.

What this branch adds (on c4/streams):
- `omega_gpu_session_recover()`: the way back from a latch without restarting the process. Runs the on-close hooks
  (still latched, so nothing is released while the old grid may run), closes the device (Linux reference: the RM client
  close destroys this process's channels and the grid on them, then frees and unmaps every buffer), and only then lifts
  the latch and clears the parked list. The next open is a new device. `omega_gpu_session_recoveries()` counts them;
  a command list latched before the latest recovery no longer blocks.
- The Linux platform close now reports a driver teardown fault (it used to drop it): recover then stays latched for good
  (the old grid may still run; the process must exit) instead of claiming success.
- Fake-platform cases TIMEOUT_RECOVER_SAME_PROCESS, RECOVER_TEARDOWN_FAULT_STAYS_LATCHED and four new session mutants
  (recover keeps the latch, recover
  without the close, recover when not latched, recover ignores a teardown fault): each one is caught.
- `tests/gpu_isolation_test.c`: the chip test (hang + recover; two processes; a hang beside a working process).

Driver docs: the vendored NVIDIA headers document NVA06C_CTRL_CMD_PREEMPT ("This command preempts a channel group") but
are silent on freeing a channel group whose grid never ends (RM source not vendored). The chip run is the proof.


Review fixes (independent Opus review, 2026-10-22):
- The never-ending launch now passes one argument word (a launch with none is refused before submit, so the hang part
  would never have hung).
- The two processes are started together after both are set up and each runs at least 3 s; their time spans must
  overlap by at least half of that. Beside a hang, process 1 starts at process 2's "hang launched" signal and keeps
  going until process 2 is done; it must have launches inside that stretch.
- "Grid destroyed" is not claimed from timings: it rests on the driver's clean close (a teardown fault keeps the
  session latched, case RECOVER_TEARDOWN_FAULT_STAYS_LATCHED).
- A command list made before a recovery is refused (its buffers are gone); the chip test checks it.
- A command-list-only latch (check launch completed) is recoverable: omega_gpu_session_note_module_latch, case
  RECOVER_MODULE_LATCH, mutant recover-ignores-module-latch. Session tests 16/16, mutants 11/11.
- C4 parts fixed here too (so merge c5/isolation, which contains c4/streams): a launch that waits always gets the full
  WAIT_FOR_IDLE + invalidate; a list with waits runs only in submit_pair with the list it waits on.

| file | what | result |
| --- | --- | --- |
| 01-host-only.log | codegen + encode of the work, mutant and never-ending kernels; simulator exact, mutant caught | PASS |
| 02-session-platform.log | the session on a fake GPU, both recover cases included | PASS 16/16 |
| 03-session-mutants.log | 11 broken copies of the session, each caught | 11/11 killed |
| 05-platform-linux.log | the Linux platform on a simulated driver | PASS 8/8 |
| 04-never-ending-kernel.sass.txt | our disassembler: `IADD3; BRA 0x20` (branch back for ever) | as designed |
| (chip, pending slot) | hang + recover + right values; two processes right; hang beside a working process | NOT RUN |

Chip command (GB10 lock): `build/gpu_isolation_test`. Safety note for the slot: the never-ending kernel is one warp;
its teardown is the RM channel free, which must preempt it; this is the first chip run of that path.
