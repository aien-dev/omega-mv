# Native bounded buffer contract

Tracks https://github.com/aien-dev/omega-mv/issues/644 under architecture295. This is a CPU-host-only bounded native module, not the completed cross-adapter application pipeline, GPU residency or a new runtime. Contract revision1.1 is in CONTRACT.md.

All new implementation and executable test logic is Omega. The initial worker's newly authored shell harness was removed from the final source; it remains visible only in historical commits. Use the existing qualified compiler or existing `omega check/build/run` installer described in the sibling profile module. Host `cat`, hash, ELF inspection and process invocation are explicitly retained build/inspection tools.

From an unrelated private output directory, set MODULE to this source directory and OMC to the immutable qualified compiler:

```sh
printf '%s  %s\n' 191b776ef98821597c3e30655da6d680a9b66edbc9ceaca3c39c61ea6df098e9 "$OMC" | sha256sum -c -
cat "$MODULE/buffers.om" "$MODULE/tests/test_globals.om" "$MODULE/tests/test_buffers.om" > suite.om
"$OMC" < suite.om > suite
# Continue only after compiler exit0 and a nonempty AArch64 EXEC with no INTERP.
readelf -h suite
readelf -l suite
chmod 700 suite
./suite
cat "$MODULE/buffers.om" "$MODULE/tests/rights-regression.om" > rights.om
"$OMC" < rights.om > rights
chmod 700 rights
./rights
cat "$MODULE/buffers.om" "$MODULE/tests/read-regression.om" > read.om
"$OMC" < read.om > read
chmod 700 read
./read
```

Expected native exits: suite0, rights0, read0. Rights/read regressions print PASS lines. `tests/exit-wrap-control.om` is a deliberately failing version of the full native suite; concatenate it after buffers.om and test_globals.om, compile successfully and require run exit1. An exit0 is a test-framework defect, not a pass. The original defect returned256, which the process interface truncated to0.

Historical author-run and original three compiled mutants remain under `evidence/UNIVERSAL-ADAPTER/L717/`; they describe source98844b3, not the corrected source. Parent correction evidence adds two current-source mutants for cancellation authority and read-status loss. No old receipt is rewritten or promoted to a fresh run. The original module path began with an accidental extra `omega/`; final canonical path is `native/universal/buffers/`.

Opt-in only: no configuration, installed files, services, drivers or shared compiler files are changed. Rollback is to stop linking this module. Preserve the caller's original adapter until integrated behavior qualifies. Normal judge enrollment, independent final correction review, same-application transfer integration and measured latency/throughput/memory remain separate gates. No speedup or zero-copy claim.
