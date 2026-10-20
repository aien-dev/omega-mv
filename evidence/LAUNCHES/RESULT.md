# L137 result: what the 436 GPU jobs per token cost, and what fusing two pairs saved

CALC-EXEMPT: chip times copied from run.log in each folder; every derived figure carries its calc id.
Binary: feat/fewer-launches cf3712cf. Synthetic weights at the real Qwen3-30B-A3B shapes, 48 layers, median of 20.
Box: both runs took the quiet lock and the GPU lock (quiet-measure + flock), but load average was about 63 during them
(spark-receipt load1 63.5 and 62.0) and no spark-window id was set. Absolute times are inflated by that load; the
fused-vs-unfused comparison ran back to back in one process, so it is the fairer number. Energy: runs were 4.5 s and
6.6 s; the UPS resolves about 20 J, so incremental J (0 and -31) is noise, not a result.

## 1. Launch tax (launch-tax-48l/)
| | chip time per token |
|---|---|
| (a) real list, 436 jobs | 28,047.1 us |
| (b) same list, every job an empty kernel (same QMDs, grid, banks, barriers) | 826.2 us |
| (c) one empty launch alone | 107.6 us |
| share (b)/(a) | 0.0295 (calc:9a9e0a81) |
| cost per extra launch (b-c)/435 | 1.65 us (calc:a3240dd9) |

Prediction (PREDICTION.md, written before the run): share 0.09, range 0.05 to 0.15. Measured 0.0295: below the range,
so the prediction was WRONG on the high side. Its expected reading held: the bare act of launching is small (about
1.65 us a job); the 20-28 us per dependent job seen in the 10-14 barrier study lives inside the real jobs (start-up and
tail of a grid that waits for the previous one), not in the launch itself.

## 2. Fusion map
FUSION_MAP.md. Bytes saved are small (all four candidates 0.33 % of the token's weight bytes); what fusion buys is jobs.

## 3. Fusions built (bit-identical gate)
- F2: gate_up GEMV + SwiGLU in one job (removes 48 jobs). F1: o_proj GEMV + residual add in one job (removes 48).
- Per-row arithmetic unchanged; the SwiGLU ending is the glue kernel's own instruction sequence, the residual add its
  single FADD. Default OFF (AIEN_OMEGA_DGRAPH_FUSE=1/2/3); nothing installed in the live server.
- Gates: simulator 8/8 bit-identical; chip 2 layers 29/29 (fuse-correctness-2l/); chip 48 layers every fused check
  passes (fused list bit-identical to the unfused list in all 20 reps, both planted mistakes caught).
- F3 (down + combine, first by bytes) not built: it removes no job and needs the down kernel reorganised across experts.

## 4. After fusion (fuse-48l/), same process, back to back
| list | jobs | chip us/token | tokens/s |
|---|---|---|---|
| unfused | 436 | 27,511.1 | 36.35 (calc:bffc76d4) |
| fuse 1 (SwiGLU) | 388 | 27,363.0 | |
| fuse 2 (residual add) | 388 | 27,234.1 | |
| fuse 3 (both) | 340 | 26,900.5 | 37.17 (calc:98c70a2e) |

Saved 610.6 us per token = 2.22 % (calc:f77656c5), 6.36 us per removed job (calc:09fb84f5): about four times the
bare launch cost, because each removed glue job also carried its own memory round trip and its wait for the job before.

## 5. One check FAILED in both 48-layer runs, not caused by fusion
"logits A vs C within 1e-04 of max|logit|": A is the older CPU-assisted path, C the unfused GPU list. Its tolerance was
derived for 2 layers (tests/gpu_decode_graph_test.c TOL_REL comment); the test refused more than 8 layers before L137.
At 48 synthetic layers the gap is 0.000113 of max|logit| (calc:286913ad), just over 1e-4. Gap by depth (tol-by-depth/):
2 layers 1.19e-6, 8: 3.37e-5, 16: 3.23e-4, 24: 1.66e-4, 32: 1.26e-4 (all PASS), 48: 7.27e-4 (FAIL). Rounding
build-up between two paths documented as not bit-identical (RMSNorm f32 vs f64, MUFU vs expf). The tolerance was
NOT loosened; whether it should scale with depth is for the test's owner to decide.

## Distance to the ceiling
Weight bytes per token 3,340,238,848 B (FUSION_MAP.md, calc:02ed0088). The ceiling for this token is set by memory
bandwidth, not by launches: launches are now about 3 % of token time, so further fusion has at most that much left.
