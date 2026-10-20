# L137 fusion correctness on the chip (2 layers, GPU lock only, NOT a timing receipt)

build/gpu_decode_graph_test --layers 2 --fuse 1 --fuse 2 --fuse 3 --reps 3, taken under /tmp/aien-gb10.lock only
(no measurement window, box shared), binary from feat/fewer-launches 8701626c.

Result: chip 29 checks, 0 failed: PASS (run.log, receipt.json).
- fuse 1 (gate_up + SwiGLU in one job), fuse 2 (o_proj + residual add in one job), fuse 3 (both): every rep
  bit-identical to the unfused list (x, hidden, logits, token).
- Negative controls: mutant 4 (SwiGLU scale 1) and mutant 5 (o_proj adds into o, not x) each break the identity.
- The times in run.log (3 reps, 2 layers, outside a window) are not evidence of speed either way; the timing receipt
  is the 48-layer run in a measurement window.
