Prefetch red control on GB10, 2026-10-31 (lane Chip677, shared spark-window 20261031T023828Z-39669b)

MEASURED: `tools/run_gpu_l2d_prefetch_chip.sh` on head 5a6cff4f (classifier sha256 7b8eef8d) ran
cached/uncached runs 1-5 PASS (13 checks, 0 failed) and the `--prefetch-red` control exited 1
with `chip: 13 checks, 6 failed: FAIL`, failing slots 3, 4, 5, 7, 10, 11 (red.log, receipt-red.json
here). The classifier printed `PREFETCH_CONTROL: INVALID` and the script ended
`GPU_L2D_PREFETCH_CHIP: INVALID prefetch control` (rc 2).

The two historical device red runs show the same six failing slots and receipt B identical=0:
evidence/OWN30B-L2D-PREFETCH-20261014/r1/red.log and
evidence/OWN30B-REQUAL-MAIN-710d438c-20261015/gates/l2d_prefetch/red.log.
The first classifier (7b8eef8d) returns 65 INVALID on all three.

Cause: the classifier accepted FAIL only at slots 4/5 and required B identical=1. The unordered
o_proj race makes every graph-vs-graph comparison fail too (C determinism, B vs C, C after the
mutants, timeline token), which tests/gpu_decode_graph_test.c checks against C0.

Fix (classifier b527a20c): FAIL allowed at slots 3 4 5 7 10 11; CAUGHT needs slot 4 or 5;
receipt B identical must be 0 exactly when slot 7 failed. Host replays (MEASURED):
- all three device red runs, exit 1: CAUGHT rc 0; same files with exit 0: INVALID 65.
- tonight's red run edited to pass slots 4/5 (tok_same 1, failed 4): INVALID 65; a mutant without
  the slot-4/5 requirement returns CAUGHT 0 on it.
- host gate (53 cases): PASS. Mutants: B-identical rule removed FAIL 99; first classifier with the
  new fixtures FAIL 99. The slot-4/5 requirement alone is not killed by the 53 gate cases (no
  fixture carries only race-slot failures with a consistent receipt); adding one changes the case
  count observed by tests/gates/omega-prefetch-native.gate, an operator change.

Device rerun with classifier b527a20c (rerun-b527a20c/, MEASURED): shared spark-window
20261031T041753Z-73d343, head 39e848fe, tools/run_gpu_l2d_prefetch_chip.sh rc 0, runs 1-5 PASS
13/13 with C0_logits_fnv 77e35a29071d8fa7, red control exit 1 (13 checks, 6 failed),
PREFETCH_CONTROL: CAUGHT, GPU_L2D_PREFETCH_CHIP: PASS. Timings in both runs were taken in a shared
window and are not qualified (timing needs exclusive window; Flint night run).
