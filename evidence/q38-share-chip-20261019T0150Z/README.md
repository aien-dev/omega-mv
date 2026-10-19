# omega#569 FORTRAN idea 1 (EQUIVALENCE): lifetime-shared decode graph buffer, chip run

- Command: `OUT_DIR=/tmp/f1-equivalence-scratch/build JOBS=4 REPS=10 nice flock /tmp/aien-gb10.lock tools/run_gpu_q38_graph_chip.sh evidence/q38-share-chip-20261019T0150Z ~/workspace/physics-6d7cf0d4`
  at commit 586b47dc (worktree clean, see worktree.diff). Verdict: GPU_Q38_GRAPH_CHIP: PASS (main.log 2 layers, layers4.log 4 layers).
- Shared layout (SHARE | VERIFY) vs the unshared layout, 3 tokens each shape: x, logits and token bit-identical.
- Negative controls, all red as required: mutant 5 (xq forced onto mix) refused before submit; mutant 6 (same, unchecked) values differ;
  mutant 7 (undeclared gather-table read) refused by the command-list check. lifetime-red-before-green.log: the host test against two
  broken checkers (rule 1 off, replay off), both FAIL; unmodified 33/33.
- lifetime-with-prefill-plan.log: host test at the next commit (45/45) with the computed prefill-chunk plans (not run on the chip).
- Full model plan (64 layers, vocab 248320, host): buffer 4,158,208 B flat -> 3,760,384 B shared; saved 397,824 B (calc:f56e3c33),
  9.567% of the graph buffer (calc:f32b2548); 240 B above the peak-live floor (calc:ffaafd4c).
