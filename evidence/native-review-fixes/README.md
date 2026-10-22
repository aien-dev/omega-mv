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
