W-L2i (2026-10-14): does occupancy explain why resident2 is slower than the launched decode path?
r1-occ:    original resident2 (72 declared registers), grid 48/96/128/144 x cached/uncached x 3 runs. Stamped medians (--tl-reps 30).
r2-regs64: base (72 regs) vs v1 (64 regs) at 128/144, v1 at 192, x cached/uncached x 3 runs; red-base-grid192.log = base refuses 192.
Each log: gpu_decode_graph_test --layers 6 --vocab 4096 --reps 30 --resident --resident-grid G --tl-reps 30 [--cached-weights].
summary.txt per run dir: tools/tlsum.sh (investigation folder) over the TLMED lines. Units: us per layer, ON minus OFF (negative = resident faster).
r3-rows2:  one binary (rows-1 kernel byte-identical to r2 v1): rows 1 at 128/192, rows 2 (--resident-rows 2, 73 peak, 80 declared,
           3 CTAs/SM) at 96/128/144, x cached/uncached x 3 runs. smoke-r2-128.log = first chip run of rows 2 (10 reps).
chip-summary.txt = the L2h method (RESIDENT PER TOKEN, host-timed submit-to-done median). It is quantized by the host completion poll
(medians cluster near 2850 / 2885 / 2950 us per token) and cannot resolve < ~6 us/layer; summary.txt (GPU stamp span) is the measure used.
