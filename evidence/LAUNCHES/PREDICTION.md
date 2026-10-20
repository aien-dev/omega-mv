# L137 launch-tax prediction (written 2026-10-20 before any L137 chip run)

Model: Qwen3-30B-A3B decode token as one command list, 9 launches per layer x 48 + 4 = 436 (calc:cbd8f08b).
Measured with tests/gpu_decode_graph_test --layers 48 --launch-tax (synthetic weights at the real shapes, shared by
every layer, GPU-uncached as the live path uses by default; the real model's own token is cited from earlier receipts).

- (a) real list, chip time: 24 to 27 ms (prior real-model receipts: 24.49 ms/token before the COAL stores, which are not
  on omega main; ~/handoffs/2026-10-16-speed-finish-state.md).
- (b) the same list, every kernel an empty one: about 5 us per launch (prior: glue launches 3-10 us, barrier-free
  saves only 2-5 us), so about 2.2 ms.
- (c) one empty launch alone: 10 to 25 us (one submission round trip plus one launch).
- Share (b)/(a): about 0.09 (calc:a2f17d5d, 436 x 5 us / 24.5 ms); range 0.05 to 0.15.
- Expected reading: the fixed cost that the earlier barrier study found (20-28 us per dependent GEMV launch) is mostly
  NOT in (b); it sits inside the real GEMVs (start-up and tail of a grid that must wait for the previous one).
  If (b) comes out near 436 x 20 us = 8.7 ms, this prediction is wrong and the launch itself is the cost.
