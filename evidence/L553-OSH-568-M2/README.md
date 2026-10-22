# L553 #568 physical-line result-reader boundary

Continuation of M1, existing HOST TEST TOOL only. No new compiler/parser or
native adapter. Native-looking controls are synthetic. All native AIENOS,
QEMU, hardware/current35B, independent review and whole-base integration NOT_RUN.
Existing module move L396/omega-host and PR572 dependencies remain.

Criterion frozen in L553-handoff before test: consume physical serial lines;
ignore/drain nonprotocol console noise; protocol lines cannot conceal trailing
bytes with NUL or exceed16383bytes before LF (optional terminalCR included).
Complete final EOF line remains accepted; end count is nonempty unsigned decimal
and equals actual rows. File read errors refuse. No authenticity claim.

Commands, all under quiet-build from assigned worktree:

- `node tests/osh/diff/result_reader_controls.mjs build/L553/pins-final/osh/osh_diff build/L553/pins-final/osh/osh_toy tests/osh/diff/fixtures.txt build/L553/framing-red framing build/L553/pins-final-green/actual-host.result`: exit1,4pass10fail. Eight real wrong acceptances0: fourNUL, overlongNUL protocol, chunk-injected records, plus-prefixed count and space-prefixed count. Two other count refusals had wrong diagnostic.
- `make -s -j2 OUT_DIR=build/L553/framing-fixed build/L553/framing-fixed/osh/osh_diff build/L553/framing-fixed/osh/osh_diff_asan build/L553/framing-fixed/osh/osh_toy`: exit0. Physical-line reader correction.
- Controls with framing-fixed normal/sanitizer binaries and fresh framing-final2 / framing-final2-asan directories: exit0,17/17 each. Includes same synthetic complete result, CRLF/noise, very long nonprotocol line, EOF termination,16383/16384 boundary, actual directory read error, NUL/framing/count refusals. Comparator-only legs reuse actual M1 host results; no native or new host execution inferred.
- `node tests/osh/diff/result_reader_mutants.mjs build/L553/pins-final-green/actual-host.result framing`: final exit0. Four successfully compiled mutants killed at exact failing assertion sets: removeNUL4; remove unsigned-count syntax4; ignoreferror1; old chunk reader7. No unrelated failure, sanitizer, signal or timeout credited. Raw compile/test logs retained.
- Pin-mode controls against corrected reader: exit0,22/22 comparator assertions.
- Unchanged original `make -s -j2 OUT_DIR=/tmp/L553-osh-regression.DAxToC test-osh-diff`: exit0,66plainPASS/64sanitizerPASS+2originalRLIMIT_ASNOT_RUN/21originalcontrols. Actual native33NOT_RUN unchanged.

Development evidence retained: extra exact-size fixture initially inserted an
unknown line before the header but expected unknown-record diagnostic; actual
first-header refusal correctly failed one assertion (16pass1fail). Fixture moved
after header; no source/gate relaxed. First mutation harness using the faulty
fixture refused its verdict and was not counted. Corrected scripts/source pinned
in source-sha256. No retry-until-green, no energy/timing measurement.

M1 raw evidence immutable. L53163a92c55 carry and parent decoder/trace/600 review
remain owner-specific dependencies; no issue closure or merge approval.
