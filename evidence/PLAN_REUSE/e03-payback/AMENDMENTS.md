# Amendments to 00-frozen-protocol.md (append-only; the protocol file itself is not edited)

## A1, 2026-10-19, after held-out run 1 (heldout/heldout-run1-receipt.json)
Reason: one reconciliation row (P0 task 27, arm A) was off by 2 us because the harness subtracted the
identity hash measured on a copy (72 us) where the in-place hash inside pla_generalize had taken 70 us.
Change: cost_A and the reconciliation subtract the clamped value min(hash on copy, generalize phase), stored
per row as hash_in_e2e. No definition in the protocol changes; the clamp removes a measurement artefact.
Run 2 is a new receipt beside run 1; run 1 stays.

## A2, 2026-10-19, after held-out run 1
Reason: the F2 floor (arm B plan length equals arm A plan length) failed on S2, S3 and S4 because the existing
adaptation path repairs a wrong-state template into a valid but longer plan (merge: 12 moves where search
finds 9; swapped reverse: 10 where search finds 7), stores it, and reuses it. This is a finding, not an
artefact; run 1 and run 2 keep the frozen arm B (reuse with adaptation).
Addition: one extra labelled ablation run, arm B without the adaptation path (PAYBACK_NO_ADAPT=1, receipt
heldout-run3-noadapt-receipt.json), the "reuse only" row of the #276 ablation ladder. It is reported beside
the frozen arms, never in their place.

## A3, 2026-10-19, after held-out run 1
Reason: per-task wall differences between arms have an interquartile range of about 200 to 400 us on
3 to 4 ms tasks (load average 16 during run 1) while the effects under test are 5 to 90 us; the frozen
cumulative sum carries every outlier (P0 task 0 alone was +2.3 ms).
Addition: the receipt also reports a robust figure per stream (task-0 net plus the median per-task net times
the remaining tasks, with the per-task IQR). The frozen cumulative sum remains the figure of record; the
robust figure is reported beside it and labelled.

## A4, 2026-10-19, after held-out runs 2 and 3 (heldout/heldout-run2-receipt.json, heldout-run3-noadapt-receipt.json)
Reason: both runs fell in a window with 25 to 28 runnable processes on 20 cores (load 15.8 to 16.5; run 1 had
4 to 6 runnable). Half the tasks carry wall outliers of 1 to 3 ms either way. The frozen cumulative wall sum then
shows the wrong sign on BOTH controls (P+ negative, P0 positive) in run 2 and run 3. The controls did their job:
the frozen wall figure cannot detect a 10 to 90 us per-task effect on this box under that load.
Change: the receipt also reports the same accounting on cognition CPU (thread CPU time of every cognition phase;
execution phases excluded because they are identical work in both arms, held by floors F2 and F3). The sign
controls and the net(H) prediction are run on this figure too (the dev receipt gains a cognition_cpu prediction
block, computed from the same development partition before held-out runs 4 and 5). The frozen wall figure stays
in every receipt and the verdict says which figure each claim rests on.
