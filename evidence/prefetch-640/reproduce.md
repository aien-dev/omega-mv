# Host reproduction

Run from this repository root on Linux AArch64. `OMC` below names the qualified
compiler whose SHA256 is recorded in `receipt.json`. Check that digest before
building. `OUT` must name a fresh directory. These are developer orchestration
commands; the classifier and fixture generation are native handwritten Omega.
No command below executes a GPU test.

```sh
mkdir "$OUT" &&
quiet-build sh -c '"$OMC" < native/prefetch/classify.om > "$OUT/classify"' &&
quiet-build sh -c '"$OMC" < native/prefetch/review-fixtures.om > "$OUT/fixtures"' &&
quiet-build sh -c '"$OMC" < native/prefetch/review-nested.om > "$OUT/nested"' &&
chmod 700 "$OUT/classify" "$OUT/fixtures" "$OUT/nested"
```

Export `OMC` and `OUT` for the build commands. Only execute outputs if every
build exits zero; compare the classifier digest to `native/prefetch/classify.sha256`.
For mode0 (argmax mismatch), generate a complete log and receipt:

```sh
quiet-build "$OUT/fixtures" 0 L > "$OUT/0.log"
quiet-build "$OUT/fixtures" 0 J > "$OUT/0.json"
quiet-build "$OUT/classify" 1 "$OUT/0.log" "$OUT/0.json"
```

Expect `PREFETCH_CONTROL: CAUGHT`, exit0. Fixture mode1 is logits-only,
mode2 both mismatches, mode3 complete pass (supply observed child exit0,
expect UNCAUGHT/exit1). The other modes and observed exits are recorded in
`results.txt`. For modesL..X, use the selected mode's generated log with the
normal mode0 receipt, except Q uses mode1 receipt, preserving tok_same.
Their expected return is65 except P/Q, which correspond to numerical checks.
Each classifier invocation must be a fresh process.

Generate a contradictory nested flag without damaging JSON syntax:

```sh
quiet-build "$OUT/nested" 0 < "$OUT/0.json" > "$OUT/bad.json"
quiet-build "$OUT/classify" 1 "$OUT/0.log" "$OUT/bad.json"
```

Expect INVALID/exit65. Native nested modes0..9 cover all cases in
`nested-results.txt`; all must return65. Generation itself must exit zero.
The final independent review covers53 invocations and an independent
byte-identical classifier build. `initial-finding.txt` documents the prior
version's real defect and is superseded by `review.txt`.

The shell admission cases in `parent-checks.txt` deliberately fail before
the script reaches any device command. Do not run the full GPU script merely
to reproduce host qualification. Hardware experiment remains NOT_RUN.
