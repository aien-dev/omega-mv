# L137 fusion map: one decode token, which jobs hand data to the next through memory

Source: src/omega_gpu_decode_graph.c record_layer / record_final, buffer layout lines 106-110 (all intermediates f32).
Per layer 9 launches; per token 436 (calc:cbd8f08b). Weight bytes per token (FP8 layers + bf16 lm_head):
3,340,238,848 B (calc:02ed0088).

| # | producer -> consumer | intermediate | saved if fused (write + read) | per token | launches removed | bit-identical possible? |
|---|---|---|---|---|---|---|
| F3 | down GEMV -> RES COMBINE | dn[8][2048] f32 | 128 KiB/layer | 6,291,456 B (calc:7a2e3a01) | 0 (the RMSNorm half still needs every row: a whole-vector reduction) | yes, only if one warp owns row i of all 8 experts and sums g = 0..7 in the combine's own FMUL/FADD order; needs the down kernel restructured across experts |
| F2 | gate_up GEMV -> SWIGLU | gu[8][1536] f32 | 96 KiB/layer | 4,718,592 B (calc:20d8e83e) | 48 | yes: one warp computes gate row j and up row 768+j (per-row dot order unchanged), lane 0 applies the SwiGLU instruction sequence unchanged |
| F1 | o_proj GEMV -> RES ADD | o[2048] f32 | 16 KiB/layer | 786,432 B (calc:3e38ee93) | 48 | yes: lane 0 does x[i] = x[i] + acc, the same single FADD |
| F4 | lm_head GEMV -> ARGMAX stage 0 | logits[151936] f32 | the 607,744 B read (calc:2e4df33d); the write stays (sampling needs logits) | 607,744 B | 1 | yes (argmax is exact, lowest index on ties) |
| -  | RES NORM / COMBINE NORM -> qkv GEMV | xq | - | - | - | no: every qkv CTA needs the whole normalised vector (a block-wide reduction first) |
| -  | qkv -> attention, attention -> o_proj, router -> gate_up | - | - | - | - | no: each consumer row needs the producer's whole vector |

All four together save 0.33 % of the token's weight bytes (calc:28392ec6, F3 + F2): the bytes are small. What fusion
buys is launches (F2 + F1 = 96 of 436).

Choice for step 3 (brief: top two by bytes): F3 is first by bytes but removes no launch and needs the down GEMV
reorganised across experts; F2 is second and removes 48 launches. Built: F2, then F1 (the next fusion that removes
launches). F3 is reported, not built, with the reason above.
