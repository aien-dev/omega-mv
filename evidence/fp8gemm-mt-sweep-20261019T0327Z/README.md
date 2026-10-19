# fp8 gemm shape sweep for Qwen3.8-27B speculative verify (2026-10-19)

Which worker shape is fastest when the FP8 GEMM checks 2 or 5 rows at once on the
27B gate|up (k 5120) and down (k 17408) matrices. Every run also checks the GEMM output
against the per-row GEMV bit for bit (`9 checks, 0 failed: PASS` in every log).

Result: mt 2, 192 CTAs is the best or tied best at both row counts. The sovereign-core
verify path (qwen38/mtp-spec-decode) fixes that shape.

- gate|up, 2 rows: chip 983 us, 181 GB/s; per-row GEMV baseline 1738 us for the two rows.
- gate|up, 5 rows: chip 3041.5 us, 58.6 GB/s on the distinct weights. The per-row inner
  loop is compute bound past 2 rows: 5 rows cost about 3.1 times 2 rows, not the same
  weight stream. This is why checking 3 to 5 drafted tokens is slower per token than the
  plain one-row decode on this kernel.

Commands: command.txt. Archive built from 37034f03 (OUT_DIR under /tmp/f7-spec-decode-scratch).
Refs aien-sovereign-core-mv#679.
