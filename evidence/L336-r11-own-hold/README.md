# L336: OMEGA-R11-OWN-HOLD fails under load

Cause (measured, not guessed): the R11 binary refuses the living run when the 1-minute load
average is above 2 (tests/runtime/rx_r11_aien.c, other_load). The judge box sits at load 40+ on
20 cores, so the owner case (hold=q1-abc, "want ran") printed `refused` and the gate failed:
38 of 45 runs since 2026-10-18. The hold-ownership logic itself was right.

Fix: the test now asserts the property (who owns the flag), which is load-independent. The binary
decides the flag before the load, so each non-owner case must be refused BY THE FLAG, and the
owner case must NOT be refused by the flag (it either ran or was held back by load; the line says which).

| file | what it shows |
|---|---|
| 01-old-test-red-at-load43.log | old script on origin/main a8ffd2b9, load 43: `hold='q1-abc': refused, want ran`, rc=2 |
| 02-new-test-green-at-load52.log | new script, load 52: 3 refused-by-flag + owner not-flag-held-back-by-load, rc=0 |
| 03-mutant-m1.log | binary says every flag is mine: 3 failures, rc=2 |
| 03-mutant-m2.log | binary says no flag is mine: owner refused-by-flag, 1 failure, rc=2 |
| (m3, not saved) | boundary check removed: hold='q1-ab' not refused by flag, 1 failure, rc=2 |

Not covered here: the living-run timing itself needs a quiet machine; it runs only when the box
is quiet (and then the owner line reads `ran`).
