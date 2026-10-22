# L489 raw command register

All fixtures are SYNTHETIC. Test-runner durations are incidental output, not qualified performance measurements.
Host correctness only; hardware/energy/instrumentation/query timing NOT_RUN.

- RED discovery: `quiet-build node --test tests/trace/archive.test.mjs`, archive-red.log, exit 1 (missing deliverable).
- Initial implementation: same command, archive-initial.log, exit 1 (test construction mistakenly validated an invalid unit before assert. Corrected test to pass raw malformed metric).
- Behavioral RED: same command, archive-behavior-red.log, exit 1: recovery omitted a complete orphan frame (1 recovered vs 2 required).
- GREEN: same command, archive-green.log, adjacent exit file. Includes independent SHA-256/frame oracle and four byte-tampering controls.
