# REWRITE-249 AMENDMENTS (append only; FREEZE.md is never edited)

## A1 (2026-10-19, after the run of record): OEV1 records relabelled with stable op ids and a base op

What was wrong. The `account` step of the run of record wrote one OEV1 file per accepted rewrite that
removed operations. It named the surviving steps by their position in the output (0..n-1), while the
`removed` lines name steps by their stable id in the input. The OEV1 evaluator (dagsim, omega-research-583
@ d8c74201) refused 468 of the 637 files: 449 with "removed op is still present in this DAG" (a surviving
step happened to share a number with a removed one) and 19 with "empty DAG" (every step was removed, so
the file listed no operation at all). The other 169 were accepted. The search results, derivations,
verdicts, execution timings and every count in `runs/*/runs.csv` are unaffected: only the OEV1 files were
wrong.

What changed. `tools/rw249/rwsearch.c` `mode_account` (no other mode touched):
1. `results.tsv` does not store the stable ids of the output, so `account` now replays the same
   deterministic arm on the recorded input under the budget read from `summary.txt`
   (nodes 512, evaluations 4096, CPU 20 ms; ranking by the frozen blob for arm B) and takes the ids from
   the replay. If the replayed output differs from the recorded output it refuses (exit 2) before writing
   any file for that expression.
2. Each file now starts with an op `<id>_base` (call, width mask and return; t_ns from the BASE arm of
   the frozen blob, 0.513 ns, prov=fitted) that every chain depends on, so a program whose steps were all
   removed is still a valid one-op DAG.

Frozen evidence kept. The run-of-record dirs and `runs/*/rep1/results.tsv` are unchanged. Only
`oev/{M,G,Bi,B}/*.oev` were regenerated, from `record/<arm>/rep1`, with
`rwsearch account --dir <rep1> --verdicts <rep1>/verdicts.txt --oev <out> --build de779dff5e03ba9b50c6f1839cbc6ca1613ccbb6 --model evidence/REWRITE-249/calibration/model.blob --load1 10.52 --proof evidence/REWRITE-249/runs/<arm>/rep1/derivs.rwd`.

Build of the amended tool. Not a build of record: `nice -n 10 gcc` with the same flags and sources as
`mk/rw249.mk`, into `~/workspace/targets/omega-rw-249/amend/`, sha256
`340277305f9852992a073c131869dddd407769a7faa75f5dad0969a5efe4dab7`. The rwcheck and rwprobe binaries of
record were reused unchanged.

Checks.
- All four replays matched the recorded output for every expression; counts equal the record
  (M 135 files 196 removed ops, G 164 / 351, Bi 169 / 374, B 169 / 374).
- dagsim `eval <file> --asof 2026-10-19 --model ...`: 637 of 637 status OK (was 169 of 637).
- Mutant for the new check (red as required): one recorded output in a copy of `record/G/rep1`
  (S249-0000, `and 0x0` changed to `and 0x1`) makes `account` exit 2 with "replay differs from the
  recorded output: refusing to label ops" and leave no file behind.
- `tests/research/rw249/test_rw249.sh` with the amended binary: exit 0, all checks PASS, M1..M8 RED.

claim_strength: measured (dagsim verdicts, replay equality). The replay assumes the arm is deterministic
under a CPU-time limit; it matched in every case here, and the refusal covers the case where it would not.

## A2 (2026-10-19): break-even figures recomputed

The break-even medians first drafted in the working notes did not match `runs/*/runs.csv` for arms M, Bi
and B (G matched). They were recomputed from the CSVs through aien-calc; the README carries only the
recomputed values and their calc ids. No measurement changed.
