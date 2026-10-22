# C6 receipt: Omega's own disassembler

Row C6 of `docs/OMEGA.md`: our own sm_121 disassembler agrees with nvdisasm on every library kernel, and every check
that called nvdisasm calls ours. Produced by one script run on the Spark (2026-10-21), physics reference pinned at
`~/workspace/physics-l386`. Each log ends with the exit code of the command it records.

| Log | What it checks | Needed | Result |
|---|---|---|---|
| 01-test-sm121dis | all 331 library kernels (59,152 words) word for word against nvdisasm, spacing included | exit 0 | exit 0, 0 kernels differ |
| 02-reduce-on-sm121dis | the E1 reduction listing check, now on `build/sm121dis` | exit 0 | exit 0 |
| 03-divsqrt-on-sm121dis | the divide and square root listing check, on ours | exit 0 | exit 0 |
| 04-ldst-on-sm121dis | the load and store listing check, on ours | exit 0 | exit 0 |
| 05, 06, 07 (red) | the same three checks with a decoder carrying one planted fault | must fail | all fail |
| 08-gpu_* | the GPU tests whose listing checks now link our decoder | exit 0 | exit 0 |
| 08-gpu_attention_test-hd128 | head size 128, recorded for completeness | | exit 1, 14 failures, none from the disassembler: a known kernel-size bug, fixed separately (ledger L407) |
| 09-prime-race-host | the sieve benchmark's host test, its listing check on ours | exit 0 | exit 0 |
| 10 (red) | the previous decoder, which printed `NOP ;` where nvdisasm prints `NOP;` after the last EXIT | must fail | fails on all 331 |

nvdisasm stays only as the reference in 01 and the red checks. No kernel and no other check depends on it.
