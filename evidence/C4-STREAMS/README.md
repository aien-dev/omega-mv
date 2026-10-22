# C4: streams and events on the GB10 (docs/OMEGA.md row C4)

Row C4: two independent queues of the same work finish in under 0.75 of the sum of their solo times; an event wait
orders two dependent jobs, checked by data, on the chip.

What this branch adds:
- L136's second GPFIFO queue (physics feat/gpu-channels 7476de71, omega 449be575 + 0cf570dd): `omega_gpu_session_extra_queue`
  and `omega_gpu_cmdlist_submit_pair` (list A on queue 0, list B on queue 1, both submitted before either is waited).
- The event: `omega_gpu_cmdlist_wait_for(cl, other, i)`. The next launch added to `cl` starts only after launch `i` of
  `other` has released its semaphore: a host semaphore ACQUIRE (the chip-proven omega-mv#580 ORDER_SEM words) before the
  launch's barrier, so its cache invalidate comes after the wait.
- `first_barrier_none`: no WAIT_FOR_IDLE before the first launch of a submission (a WAIT_FOR_IDLE may also wait for the
  other queue's grids; the test measures both).
- `tests/gpu_streams_test.c`: half-chip spin kernel (24 CTAs x 128 threads, FFMA chain, exact value) and copy kernel,
  both from Omega's IR and our own sm_121 encoder.

| log | what | result |
| --- | --- | --- |
| 01-host-only.log | codegen + encode of both kernels and their mutants; wait_for refusals | PASS |
| 02-sim.log | warp simulator: exact values in both scheduling orders, mutants caught | PASS |
| (chip, pending slot) | overlap ratio solo vs pair (both barrier modes), serial red check, event + no-wait control | NOT RUN |

Chip command (GB10 lock; timings need an exclusive window):
`build/gpu_streams_test` (sets AIEN_OMEGA_GPU_CHANNELS=2 itself). PASS needs: pair / (solo A + solo B) < 0.75, the
same two spins in one list on one queue >= 0.75 (red check), every Y right with the wait, and at least one Y wrong
without it (negative control).
