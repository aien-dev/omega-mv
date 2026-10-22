# L519 independent host review of existing #450 PR499

REVIEW PASS for issue450's bounded host follow-ups on exact existing head
1e4cdbe91b444efe51af4e745f366e07d54de030. This is not an integration/merge verdict
for current main or later compiler formats. No compiler/front-end/source change.
Existing source owner retained; no worker started or resumed.

## Source review

- `src/compiler/osc_ir.c` rcount disables the maximum only under compile-time
  MUTANT(9); normal builds retain the refusal before the count is consumed.
  The unknown-opcode default refuses RANGE except under MUTANT(10).
- `mk/compiler.mk` lists1..10; each executable must fail and produce an explicit
  FAIL line. A crash alone cannot count as a killed mutant.
- `tests/compiler/test_osc_ir_decode.c` admitted-case oracle comment correctly
  discloses that byte-identity shares encode_unit with the decoder backstop;
  validity is separately checked and hand-written counterexamples pin refusal
  classes. Do not claim the whole round-trip oracle is independent.
- encode_unit comment describes versions1..5 and their backward-compatible
  layouts. No language semantic change was introduced by these comments/tests.

## Actual independent execution

Extracted only exact pinned Makefile, compiler/tests, sha256 and quiet helper
files with `git archive 1e4cdbe9 ... | tar -x -C build/L519/pr499-source`.
No owner checkout edited; no remote source fetched around the rate guard.

Command:
`quiet-build make -C build/L519/pr499-source OUT_DIR=build/L519-review test-osc-ir-decode`

Exit0. Actual full gate, not the quick smoke:

- 55 corpus units round-trip, 22/22 named refusal examples;10 of11 refusal
  classes exhibited (NONCANONICAL remains the unreachable canonical backstop).
- Plain:10,000 distinct valid objects,173,503 malformed cases.
- ASan/UBSan:2,000 distinct valid objects,173,510 malformed cases; no sanitizer
  errors. Existing declared ASAN_OPTIONS disables fake stack as documented by
  the unchanged owning test target; no test/gate setting was weakened here.
- Five planted defects caught by the oracle's own checks.
- All10 decoder mutants caught with actual failing assertions, including9
  (oversized function name becomes INVALID instead of OVERSIZED) and10
  (opcode0 becomes OVERSIZED instead of RANGE). Raw logs attached.

This confirms the requested host hardening controls on the proposed source.
Other compiler targets, later IR formats, full Omega gates, integration CI and
silicon NOT_RUN. No silicon gate is needed to establish these host refusal
assertions, and no hardware result is implied. Desk may reuse/review existing
PR499; no competing implementation is proposed.
