# L541 executed host commands

Run on Spark, machine/tool versions in machine.txt. Review branch initially at exact L538. No timing/performance result is inferred. quiet-build is the installed shared protocol. Absolute output names are fresh and caller owned.

Let S=/home/drakestapleton/handoffs/omega-issue-sweep-20261022 and R=/home/drakestapleton/workspace/omega-parent-source-review-20261022. These abbreviations describe the actual paths used.

1. `git archive dc24fcf27bbc6c36ca6dfcac020452bb06004bae | tar -xf - -C "$S/L541"` (fresh own export).
2. `git ls-remote https://github.com/aien-dev/omega-mv.git refs/heads/fix/ex600-horizon-guard refs/heads/fix/decoder-corpus-closure refs/heads/main` (author-remote-refs.txt).
3. Copy parent qualified native guard from `/home/drakestapleton/workspace/targets/ex600-direct-L523/final/guard` to `$S/L541-guard`; SHA256 ea34d1c9bf3ab5bf817dbde3481efbe8eba556f7a8fbc8644cb6f2d0c6e48b93. Reused seed SHA256 01b1545b23e8ff40cb5691e20b5df910ea4acaefde3495f0d62d270465b98cd6, source57f5f4258cebf3d5b2c22853cd062e351265c525; no compiler rebuild. See seed-binary.sha256.
4. `quiet-build python3 "$S/L541/research/exploration-600/continuation/test_guard.py" "$S/L541-guard"` (guard-original-tests.*, exit0).
5. `python3 tests/compiler/test_decoder_corpus_closure.py "$R" "$R/tests/compiler/verify_decoder_corpus.py" "$S/L541-corpus-tests"` (corpus-tests.*, exit0).
6. `quiet-build bash tests/compiler/reproduce_pr499_decoder.sh "$S/L541-decoder-full"` (decoder-full.*, exit0; full compiler commands, flags and mutant assertions in decoder-gate.log; output copied to decoder-plain.out/decoder-asan.out).
7. `quiet-build python3 evidence/L541/adversarial.py` (adversarial.*, exit0; reports protocol discrepancies and static-link acceptances as observations, not all-PASS).
8. `SEED_OMC=/home/drakestapleton/workspace/omega-issues-locality-602-606-20261022/research/locality_l491/out/seed-src/seed/out/omc quiet-build python3 "$S/L541/research/exploration-600/continuation/mutants.py" "$S/L541-mutants"` (guard-mutants.*, exit0; all five compile0/test1). Compilations use exact guard source and reusable pinned native compiler.
9. `quiet-build python3 "$S/L541/research/exploration-600/continuation/replay.py" "$S/L541-guard" "$S/L541-replay"` (replay.*, exit0).
10. `quiet-build python3 evidence/L541/replay-oracle.py` (replay-oracle.*, exit0; 600rows/4800field comparisons).
11. Reused original Cgen regression executable pinned in original-binary.sha256. Initial `checks`/`diagnostics` invocation from R refused exit2: relative FREEZE/AMENDMENTS reads absent there. Logs original-checks.* and original-diagnostics.* retained. Corrected only cwd to `$S/L541`, used new output directories `$S/L541-original-checks-cwd` / `$S/L541-original-diagnostics-cwd`, and executed `quiet-build /home/drakestapleton/workspace/targets/ex600-direct-L523/final/regression-build/ex600 checks research/exploration-600/policy.osc "$S/L541-original-checks-cwd"` and corresponding `diagnostics`. Logs original-*-cwd.*, exits0. No retry of a failing test assertion, source edits or suppressed gate.
12. `python3 evidence/L541/check_link_boundary.py "$R" "$S/L541-link-red"` (link-boundary-red.*, EXPECTED FAIL exit1 on exact L538; direct verifier accepted linked ancestor exit0).
13. `python3 evidence/L541/corpus-extra-boundaries.py` (corpus-extra-boundaries.*, exit0, 7 checks).
14. `sha256sum -c evidence/DECODER-CORPUS-CLOSURE/SHA256SUMS` (parent-B-hashes.*, exit0).
15. `python3 evidence/L541/export-pins.py` (export-pins.*, exit0). Full historical exported build/corpus sources checked after execution against pinned Git blobs; all original paths match. Binaries pinned in decoder-export-pins.json.

The parent RED 421/423 for A and64/67 for B was inspected in immutable published records, not rerun here. Parent six original experiment fault controls and their logs were inspected, not independently rerun. L541 reran five native behavior mutants and all ten original decoder mutants. No hardware, models, services, live ports, driver or repository integration gate ran.

Publication formatting: the raw replay CSV uses CRLF (Python csv default), which git diff --check reports as trailing whitespace. The byte-identical raw result is stored losslessly as replayed-rows.csv.gz; decompression was checked byte-for-byte. No measurement/result rows were normalized or dropped.
