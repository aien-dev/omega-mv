# L553 #568 host result reader, milestone 1

Reuse: exact PR57241c83a71a49828c369ba82624aea43eabed11a66,
original observeree138918, runnerf362edd9, signal9d02dff5. These three
source commits were cherry-picked onto assigned c100a6eb; no new shell or
compiler. Scope: existing HOST TEST TOOL only. Native AIENOS, QEMU,
hardware/current35B, whole-base integration and independent review NOT_RUN.
L396's omega-host extraction requires desk/owner routing of this correction;
this older descendant does not re-establish Omega ownership of OSH.

Criterion before test: one occurrence of each identity key, token-local `=`,
nonempty bounded values. An entire malformed file must be invalid, never a
partly green comparison. Metadata is not authentication. All native-looking
inputs in this packet are explicitly SYNTHETIC comparator controls.

Actual commands run from assigned tree, under quiet-build:

1. `make -s -j2 OUT_DIR=build/L553/baseline osh build/L553/baseline/osh/osh_diff build/L553/baseline/osh/osh_toy`: exit0.
2. `node tests/osh/diff/result_reader_controls.mjs build/L553/baseline/osh/osh_diff build/L553/baseline/osh/osh_toy tests/osh/diff/fixtures.txt build/L553/pins-red`: exit1, 5 pass/18 fail. All16 duplicate-key cases wrongly accepted0; two malformed-token cases refused under wrong reason. Actual66 host/interpreter runs PASS; missing native remains33NOT_RUN.
3. Token-local parse and duplicate refusal correction: first plain23/23PASS. First sanitizer comparator probe then actually FAILS22/22 with 14,166 leaked bytes in66 imported row strings; pins-asan.log and cases preserved. Leak detection was not disabled.
4. Release imported strings on normal/invalid-host/emission-failure returns. `make -s -j2 OUT_DIR=build/L553/pins-final build/L553/pins-final/osh/osh_diff build/L553/pins-final/osh/osh_diff_asan build/L553/pins-final/osh/osh_toy`: exit0.
5. Same controls using pins-final binaries and fresh pins-final-green output: exit0,23/23; sanitized reader with final argument `pins build/L553/pins-final-green/actual-host.result`: exit0,22/22 comparator-only controls. Actual host execution belongs to the plain leg; no missing native leg counted as execution.
6. `node tests/osh/diff/result_reader_mutants.mjs build/L553/pins-final-green/actual-host.result pins`: exit0. Both mutants compile. Removed duplicate guard fails exactly16 duplicate assertions; cross-token equals search fails exactly1 missing-equals assertion. Other failures, signals, sanitizers or deadlines never count as kills.
7. Unchanged original `make -s -j2 OUT_DIR=/tmp/L553-osh-regression.<unique> test-osh-diff`: exit0 both before and after cleanup. Original21 controls pass; host plain66PASS, sanitizer64PASS/2explicitNOT_RUN due to original RLIMIT_AS fixture, unchanged original acceptance. Actual AIENOS33NOT_RUN in both legs. The full raw final log is retained. No original test/gate was weakened or retried until green.

Binary, tool, source and actual input/output hashes are alongside. The small
control script's host metadata defaults to `unknown` when no make --pin was
supplied; it is not qualification evidence. The original make gate separately
records its actual source/tool/harness pins; source-sha256.txt binds exact
working source, including cleanup. No performance/energy measurement was made.

Inherited dependencies remain: L53163a92c55 carry is pending for the old
L511-derived base. Historical55decoder packet remains partial; corrected59
and parent B1/trace/600 work remain their owners'. Original495 concurrency
unsafe; c100 correction preserved. No issue closure or integration approval.
