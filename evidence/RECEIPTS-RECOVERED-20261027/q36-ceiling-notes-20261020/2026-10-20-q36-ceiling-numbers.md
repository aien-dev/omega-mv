
## Numbers, checked in Wolfram from the real safetensors headers (coordinator, 2026-10-20)
Qwen3.6-35B-A3B FP8 on disk: 37.45 GB. Routed experts 33.02 GB (256 per layer x 40 layers, 3.22 MB each), lm_head 1.017 GB, embed 1.017 GB (row lookup, ~0 bytes at decode), linear attention 1.017 GB (30 layers), full attention 0.300 GB (10 layers), shared experts 0.129 GB, norms etc 0.042 GB, MTP head 0.018 GB, vision tower 0.893 GB (never read for text).
Bytes touched per token at decode (top-8 of 256 per layer): routed 1.032 GB + trunk 1.488 GB + lm_head 1.017 GB = 3.537 GB FP8. With W4 experts and W4 head: 2.512 GB. The lm_head is 29 percent of the FP8 bytes per token: a head lever is worth as much as the experts.
KV cache: 10 full-attention layers x 2 KV heads x 256 dim x K and V = 20,480 B/token bf16 (10,240 FP8); 32k context = 0.67 GB bf16. KV is small on this model; the weights are the whole story.
Memory ceiling, one head per stroke (measured GB10 207 to 237 GB/s; 273 on paper):
  FP8: 58.5 to 67.0 tok/s (77 on paper)      beat at 237 GB/s: 14.9 ms
  W4 experts+head: 82.4 to 94.3 tok/s (109)  beat at 237 GB/s: 10.6 ms
Reference, the 27B dense at W4 (13.65 GB/token): ceiling 15.2 to 17.4 tok/s; measured 14.29 = 0.82 of ceiling. The 35B MoE moves the ceiling 4 to 5.4x by reading fewer bytes, nothing else.
Math ceiling (118.81 TFLOPS bf16, 2 flop per weight): about 16,800 tok/s, so the stroke stays memory-bound up to about 250 heads; the knee for N heads per stroke will come from something else first (expert union, KV reads, launch cost), which is what to measure.
Expert union with N independent sequences on one stroke (8 of 256 per layer, expected distinct experts per layer): N=1: 8; N=2: 15.8; N=4: 30.5; N=8: 57.4; N=16: 102; N=32: 163. So routed bytes per stroke grow almost linearly with N until about N=8 and then saturate toward 256; the trunk and head (2.5 GB of the 3.5) are read once regardless of N. At N=8: routed 7.4 GB + trunk/head 2.5 GB = 9.9 GB per stroke for 8 tokens = 1.24 GB/token, ceiling about 190 tok/s total at 237 GB/s. At N=32: 21.1 + 2.5 = 23.6 GB per stroke for 32 tokens = 0.74 GB/token, about 320 tok/s total. These are the numbers H6 must meet or explain; real routing is correlated (domains share experts), so the measured union should sit BELOW these independent-draw figures; if it sits above, something is wrong.

## Order (Drake 2026-10-20: "there's no real reason it has to go in any particular order, right?")
Right, with one exception that is physics, not habit. Within ONE token, layer L+1 reads the residual that layer L wrote, so the 40 layers run in order for that token (and token t+1 of one sequence waits for token t, unless a candidate head guesses it and the stroke verifies the guess). Everything else is free and the design should use that freedom: the 8 experts of a layer run in any order or all at once; the weight READS for a beat land in any order as long as they are in the cylinder before it fires (so disk and RAM layout follow the engines, not the model); different heads sit at different layers at the same time (head A at layer 30 while head B is at layer 3), which is how the pistons overlap; the next head's bytes load during this head's compute; the lm_head of one token overlaps the first layers of the next. Write the schedule so that only the true dependencies (residual chain within a token, token chain within a sequence, buffer full before fire) are edges, and every other order is left to the engine that wants it. Test: a host check that the schedule has exactly those edges and no others.

## CORRECTION + max heads (coordinator, Wolfram, 2026-10-20 later)
The N-heads figures above (N=8 ~190, N=32 ~320 tok/s) left out the per-head cost. Each head carries its own DeltaNet state: 30 layers x 32 value heads x 128 x 128 f32 = 62.9 MB (f32 in our engine: omega src/omega_gpu_gdn_api.h OMEGA_GPU_GDN_STATE_FLOATS, sc qwen38_omega.rs states: Vec<f32>), read AND written every token = 126 MB per head per token, plus its KV read (20,480 B x context). Corrected, at 237 GB/s:
  N heads | total tok/s 4k ctx | per head | memory   || 32k ctx total | per head | memory
  1   | 63  | 63.3 | 0.1 GB || 55  | 54.7 | 0.7 GB
  4   | 130 | 32.5 | 0.6    || 98  | 24.6 | 2.9
  8   | 164 | 20.4 | 1.2    || 116 | 14.6 | 5.9
  32  | 250 | 7.8  | 4.7    || 155 | 4.8  | 23.5
  128 | 491 | 3.8  | 18.8   || 221 | 1.7  | 94
  512 | 849 | 1.7  | 75.2   || does not fit
The stroke never becomes math-bound (math ceiling ~16,800 tok/s). Total tok/s climbs toward a wall set by the per-head bytes, not the weights: 1,130 tok/s at 4k (f32 state), 297 at 32k; bf16 state raises 4k to 1,614. Heads that fit in a 60 GB budget (assumed budget, set it from MemAvailable after the 35B swap): ~408 at 4k, ~81 at 32k.
What this means for the engine: past about 8 heads the DeltaNet state is the biggest mover of bytes. Levers it names: state in bf16 (measure the drift), and candidate heads on ONE sequence (MTP / tree verify) read and write the state once for k tokens, so they amortize the state as well as the weights; independent sequences do not.
