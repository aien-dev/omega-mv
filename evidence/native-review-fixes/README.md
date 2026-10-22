# L518 review-fix evidence

Worktree: omega-native-review-fixes. Initial merged source: 37295939.
No live integration edits, services, model/GPU requests, or agents.

Red/green gates executed before/after each relevant fix:

| Defect | RED command | RED result | GREEN evidence |
|---|---|---|---|
| Production relabeling spoof | `python3 tools/retirement/test_receipts.py` | exit 1, assertion: PARTIAL/errors=[]/verified_production_retirements=1 accepted despite isolated deployment | retirement-green.log, status INVALID on spoof |
| Rational operand sign coverage | `quiet-build node seed/math/oracle.mjs` | exit 1, executed coverage ++=0,+-=0,-+=0,--=400 | signs-green.log: 100 per pair; all 1048 passed incl. nine Wolfram |
| Command CR whitespace | `quiet-build node seed/math/whitespace.mjs` | exit 1, eval CR 1 -> REFUSED 1, 18/28 probes failed | whitespace-green.log: 28/28 passed |

The exact raw failures are retained in the corresponding *-red.log files.
`generator-freeze.sha256` records the corrected generator before the one-line
math source change; the core hash there is deliberately the original source.
`sign-fixtures.log` adds 57 independent BigInt and explicit zero/domain probes.
`bootstrap.log` records source-only seed ancestry and the compiler fixpoint at
191b776ef98821597c3e30655da6d680a9b66edbc9ceaca3c39c61ea6df098e9.
Math build commands returned zero and readelf checked nonempty AArch64 ELFs.
No native binary is committed. Full-gate results will be added after execution.

Only consistency is verified for production receipts. The production-positive
fixtures are TEST-ONLY temporary files, removed by the test, never real receipts.
The actual retirement count is zero and --require-retired must still exit 1.

Final qualified source: `c32810710b9efabc9dd0d3c2b7ddbbc75aa818b4`.
Math last changed in `508ddd7e02c303d0e2182721f9501c7a540c758f`; its source
and test scripts stayed identical during the final retirement logging correction.
`final-source.sha256` lists every changed source/contract/gate file.
`manifest.json` is the machine-readable result; `commands.txt` records commands.

| Full gate | Result | Current raw evidence |
|---|---|---|
| Math `gate.sh --full` | exit 0 | math-full.log, math-full.exit, math-full/ |
| BigInt/Wolfram reference | 1048/1048, nine Wolfram included | math-full/oracle.log |
| Generated rational signs | 100 per quadrant, 25 per operation/quadrant | math-full/oracle.log |
| Explicit sign/denominator/zero fixtures | 57/57 | math-full/sign-fixtures.log |
| CR boundary/CRLF/trailing/EOF/limits/recovery | 28/28 | math-full/whitespace.log |
| Frozen vectors, module, mutations | 42 vectors, 23 module checks; 13 valid compiled AArch64 mutants, 546 executed requests | math-full/golden.log, checks.json, mutants/ |
| Streaming and malformed recovery | Six stream checks; 600 malformed refusals + 600 following successes | math-full/checks.json, robustness.log |
| Independent calculator | 11/11 exact results | math-full/calc-oracle.log, calc-receipts/ |
| Existing compiler/checker host suites | exit 0, 104 existing guard mutations, 2799 truncation probes, no sanitizer report | math-full/native-suites.log |
| Seed gates 0 through omc5 | PASS; 177 final compiler cases | math-full/legacy/ |
| Retirement `verify.sh` | exit 0 after correcting mutant log field semantics | retirement-full.log, retirement-full.exit |
| TRN1 reference | 1284/1284, original 77, independent C 1260 | retirement-full/trn1-summary.json, green.log |
| TRN1 transport/deployment | 10 transport checks, eight isolated deployment/rollback checks | retirement-full/transport.json, deployment.json |
| TRN1 mutation controls | Four compiled, nonempty AArch64 ELFs, wrong results detected | retirement-full/mutants.json |
| Production receipt content checks | 59 named checks, including TEST-ONLY consistent positive | retirement-full/receipt-tests.log |
| Existing policy controls | 12 checks | retirement-full/policy-tests.json |
| Actual and retained inventories | PARTIAL/errors=[]/production retirements=0; require-retired exit 1 | retirement-full/policy.json, retained-inventory-policy.json, *require-retired* |

The first retirement full-run failure was caused by reusing `source_sha256` for
mutant bytes. Existing deployment qualification requires that field to bind the
original source. The correction preserves it and adds `mutant_source_sha256`,
with compiler exit, bytes and ELF validation recorded separately. Failure logs
remain under retirement-full-first*. No production or count gate was weakened.

Fresh math outputs were collected into math-full/, plus the unchanged
parent-supplied Wolfram fixture. The copied calculator receipt directory also
retains the two historical capacity/limb-bound fixtures; the newly executed
calculator gate consists of the 11 numbered receipts. retirement-full/ snapshots the full verification
working evidence directory and also includes inherited historical files; only
outputs named by verify.sh are newly executed. Raw manifest paths describe the
run location and are not rewritten. Reproduce using verify.sh in this checkout.
Historical tracked evidence roots were restored after collecting task outputs;
the retained original inventory was then rechecked against the stricter policy.

Limits: receipt checks verify internal consistency, not cryptographic attestation
or an actual production cutover. Math remains bounded by the existing contract.
Fixpoint checks establish repeatability, not universal compiler correctness.
The optional host tokenizer gate was skipped by the existing suite because its
environment was not configured. Timings in existing gates are diagnostic shared
host measurements; no performance claim is made. No binaries are committed.
L514 integration remains untouched and requires parent repinning and verification.
L515 remains open for parent review. No universal CAS or full-retirement claim.
