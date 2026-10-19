# Symbolic rewrite layer: verdict from measurements

Branch symbolic/rewrite-layer. Code: src/runtime/rx_symbolic.[ch], rx_symbolic_cache.[ch], rx_cascade.[ch],
rewrite pass in rx_graph.c (rx_graph_optimize), src/omega_gpu_q38_graph.c. Tests: rx_symbolic_test (11386 checks, 0 fail),
rx_cascade_test (6 checks, 0 fail), gpu_q38_symbolic_test. Every candidate is verified (bit for bit or refuted) before use.
All timings ran through quiet-measure at load average 70 to 100 (the box was busy; medians, interleaved arms).

| Milestone | Where | Result |
|---|---|---|
| M1 typed terms as SEMANTIC_ID objects | rx_symbolic_test | PASS |
| M2 verified rewrites in rx_graph_optimize | action-graphs-20261019T1731Z | PASS, 53 -> 48 reactions over 7 graphs, all same result as baseline |
| M3 physics budget filter | same run | PASS |
| M4 AIENOS evidence traces | trace-*.evidence.json, q38-checkpoint-host/aienos-explain.txt | explain reads all three |
| M5 three real workloads | action-graphs, cascade, q38 | measured below |

## Does it cut measured cost?
- Existing action graphs: YES on 2 of 7. diamond 54.4 -> 36.8 us median per run (-32%, 6 -> 4 nodes);
  redundant 56.6 -> 44.7 us (-21%, 8 -> 5 nodes beyond what the old optimizer already removed).
  The other 5 graphs: no change within noise (the parallel graph moved 7.0 -> 8.0 ms with 0 rewrites applied, so that is box noise, not an effect).
- Qwen3.8-27B decode graph: NO measurable cut. The gather fold is byte-identical on the chip but removes 5.1 us of a 19.7 ms span
  (span 19738 vs 19825 us, difference is noise). On the real checkpoint the fold side condition holds in 0 of 16 attention layers
  (56 of 112 new blocks would need two scales), so it cannot be applied there at all. The FP8 head candidate is refuted
  (248319 logits differ, worst 0.255). Barriers: 40 recorded, 37 needed, 0 droppable. Glue is 1.54% of the span, which is the most any glue rewrite could ever remove.
  The chip run used 4 layers (--layers 4 --mask 0x8); glue and weight kernels both scale with layer count, so the share should hold for 64 layers, but a 64-layer chip run was not made.
- Cascade selector (per-request kernel and draft source): NO. Mean 160.97 ms/token selected vs 156.67 today (2.7% worse);
  best arm chosen after the fact would be 150.17 (4.2% better than today). Cost model error on measured arms 4.9% mean.
  ngram arms are replays, not end to end.

Receipts: ~/yardsticks/receipts/2026-10-19-symbolic-*-20261019T17*.json
