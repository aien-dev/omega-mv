# 4-bit reading routine: load-ahead on the whole 27B model (L127, 2026-10-20)

Window 20261020T102817Z-3c60c3 (gb10 exclusive). Weights from the 4-bit store only (13,014 MB on the chip, 4-bit last step).
Build (hashes in ground.txt): omega 06e550f4 = a plain merge of this branch at 7905186f and L125's W4-only load d2d74041, nothing
else; sc 1bdab348 (L125) plus one local change e097fe29 that deletes the three-line refusal of a graph with no bf16 head (L125
made the same fix on its own branch). Arms: S0 = load-ahead off (AIEN_OMEGA_W4_PF=0), S1 = load-ahead on (=1).
Script win2c.sh: one GPU timeline per arm (qwen38_ab, 47 tokens), then qwen38_omega_full on the golden prompt (47 positions)
four rounds in rotated order (S0 S1, S1 S0, S0 S1, S1 S0), each under spark-receipt.

## Result
Load-ahead does not make the whole model faster. The default stays off.

| | S0 (off) | S1 (on) |
|---|---|---|
| ms per token, median of 188 steps (4 rounds x 47) | 70.325 | 72.245 |
| per round | 71.00, 69.02, 71.10, 69.28 | 72.87, 84.63, 71.91, 70.08 |
| tokens per second | 14.22 (calc:ecd801524f56) | 13.84 (calc:db14b90cc1ca) |
| fraction of the 54.82 ms floor (all weights at 273 GB/s) | 0.780 (calc:9f095efff611) | 0.759 (calc:f45a186bf128) |
| timeline span per token | 71.308 ms | 70.695 ms |
| five weight steps together, timeline | 205.1 GB/s, 0.751 of 273 (calc:59c960899bf2) | 206.9 GB/s, 0.758 (calc:ee24c9e150d3) |

S1 was slower in all four paired rounds (0.8 to 1.9 ms, and 15.6 ms in round 2, cause unknown; the receipts show no extra load there); its single timeline was
0.6 ms faster, all of it in the down step (195.3 to 201.6 GB/s, the same direction as the 2-layer test). Per step, GB/s and
fraction of 273, in steps-gbs.txt: S0 mix 203.3 (0.745), o_proj 170.4 (0.624), gate_up 218.5 (0.800), down 195.3 (0.715),
lm_head 212.7 (0.779).

Yesterday's comparison point for the same setup (4-bit weights and head, loaded by converting the 8-bit file, same lib kernels):
72.37 ms per token. Measured the same way, its five weight steps ran at 204.0 GB/s (calc:27e615a77cd6). The 209 GB/s quoted in
the brief (calc:832d43a73403) is the 8-bit-head setup, not this one. The L125 store run in its own window earlier today gave 69.957 ms.

## Output
Every run, both arms, produced the same 24 words, the same cosines and margins to every printed digit, and they equal
yesterday's 4-bit run word for word. The full-model check's verdict is FAIL for all eight, as it was yesterday: it demands the
exact words of the 8-bit model, and 4-bit makes one different choice out of 24 (step 18, a close call). That check was written
for 8-bit; the 4-bit fit check belongs to L125 and passed there. Load-ahead changes no output.

## Energy
J per token: UNMEASURED. The receipts (receipt-*.json) cover whole runs on a shared box: about 78 s each, of which decoding is
about 3.3 s and the rest is the host reference and the weight upload, and the meter is the whole UPS output (idle 310 W before
the first run against a 255 W mean during it, so other loads moved under it). The CPU load average was 32 to 50 in every run.
