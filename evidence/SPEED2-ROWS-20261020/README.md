# SPEED2 rows mode: chip test (ledger L126), 2026-10-20

What was tested: the q38 decode graph's rows mode (`omega_gpu_q38_graph_rows`, one command list per verify
pass of up to 32 rows) and its on-chip DeltaNet rollback (`omega_gpu_q38_graph_rows_commit`), branch
speed2/specdecode 4b20a9c5, test binary `gpu_q38_graph_test --rows R` built from that commit (sha in command.txt).

Window 20261020T025433Z-3c3b7d (spark-window, quiet lock; this window was opened before spark-window took the chip
lock itself, so each run also took `flock /tmp/aien-gb10.lock`). Box busy: load 51 to 62 at the starts (receipts).

## Result: PASS in all four configurations

| run | layers (mask) | rows | head | checks | rows(R) pass | R one-row tokens |
|---|---|---|---|---|---|---|
| l2-r9 | 2 (0x2) | 9 | FP8 | 9 of 9 | 15.79 ms | 142.74 ms |
| l4-r9 | 4 (0xA) | 9 | FP8 | 9 of 9 | 20.35 ms | 159.46 ms |
| l4-r5 | 4 (0xA) | 5 | FP8 | 9 of 9 | 20.37 ms | 92.79 ms |
| l2-r9-bf16 | 2 (0x2) | 9 | bf16 | 8 of 8 | 117.47 ms | 169.14 ms |

Times are chip medians of 10 (the test's own timing line). One-row tokens are the same graph in tv mode, one
command list per token.

Checks, each bit for bit against the one-row tv tokens of the same graph:
- R1: every row's token and residual from one rows(R) pass equal the one-row reference.
- R2: commit(keep of R) restores the DeltaNet state and replays the kept rows; the following one-row tokens equal
  the reference. Also with keep = all rows and keep = 1.
- R3: a second rows(R) pass from the committed position equals the reference.
- Negative controls, each must break a check and did: 5 (commit without the restore), 6 (replay one row too few),
  7 (every row attends at the first slot).

What the bf16 row shows: without the FP8 head each row pays its own bf16 lm_head GEMV, so the 9-row pass gains
little. The FP8 head goes through the multi-row GEMM with the rest, which is what makes a pass cost close to one row.

These are slices of the model (2 or 4 of 64 layers), so the times say how the pass scales with rows, not what a
whole-model pass costs. The whole-model numbers are in sc evidence/speed2-specdecode-20261020 (qwen38_spec suites).

Files: rows-*.log (test output), receipt-rows-*.json (spark-receipt: wall joules, load, lock holders), command.txt.

CALC-EXEMPT: every number here is a measured median copied from the test logs, none is derived.
