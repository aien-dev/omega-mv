# L494 first native exact/symbolic math milestone

PASS. This is a working native Omega module and standalone AArch64 executable. Scope: bounded multi-limb exact integers and reduced rationals; canonical univariate polynomials; expand/collect, differentiation, polynomial antiderivative, exact rational substitution, monic polynomial GCD, a single linear equation and 2x2 rational systems. Source and integration contract: `seed/math/CONTRACT.md`.

| Gate | Executed result | Raw evidence |
|---|---|---|
| Source bootstrap | Qualified omc hash reproduced from readable seed sources; self-build bytes equal | bootstrap.log |
| Frozen public capabilities and refusals | 42/42 | golden.log |
| Independent BigInt rational/polynomial reference | 1048/1048 | oracle.log |
| Parent's actual Wolfram fixtures | All 9/9 executed, included in the 1048, not additional | oracle.log, wolfram-oracle.json |
| Existing calculator oracle | 11/11, exact receipts preserved | calc-oracle.json, calc-receipts/ |
| Callable module, domains, arena/fuel/overflow, transaction/reset | 23/23 | unit.log |
| Wrong-rule mutants | 13/13 compiled valid AArch64 executables detected; 546/546 requests executed | checks.json, mutants/ |
| Streaming and transport | 6/6, including original 300-line regression | checks.json |
| Malformed inputs and recovery | 600 refusals plus 600 successful following requests | robustness.log |
| Reproducibility/environment independence | Identical executable rebuild; correct result with empty environment | checks.json |
| Existing seed rungs 0,1,2,3,4 and omc5 | All PASS | legacy/gate*.log |
| Existing omegese, strict IR decoder, checker2 suites | Exit 0, including their sanitizer and mutant runs | native-suites.log |
| Standalone path | No interpreter/dynamic segment; trace contains initial execve, read, write, exit only | elf.log, syscalls.log |

Commands actually run from the worktree on Spark:

```
quiet-build sh seed/math/gate.sh
quiet-build make -j2 test-omegese test-osc-ir-decode test-checker2
quiet-build sh seed/math/legacy-gates.sh
strace -qq -o evidence/math-native/syscalls.log build/math-native/omega-math < build/math-native/trace.input
readelf -h -l -d build/math-native/omega-math
```

The final math wrapper exited 0 (`gate.exit`). Legacy suites run in `build/math-native/seed-suite`, preserving historical evidence in the main worktree. Compiler/reference C programs are host test tools only. The math executable is built exclusively by the source-rebuilt Omega compiler. `manifest.json` and `SHA256SUMS` identify source, seed, tools, binary and logs; no generated executable is committed.

RED/GREEN history is preserved at the public-capability level: the initial compiled stub gave 7/40 matches; the core before the 2x2 implementation gave 37/40; the implemented first cut gave 40/40; two additional carry/borrow cases now give 42/42. Initial stub source hashes match `red-baseline/`. Private helpers are covered by module tests, arbitrary-precision reference cases and compiled mutants. Compiler failures and failed test-harness assumptions are recorded in FAILURES.md; none counts as a detected math mutant.

Limits: 128 decimal digits per integer magnitude including intermediates, degree 16, one polynomial variable x, rational substitution, polynomial-only integration, fixed workspace and request budgets. Rational functions, radicals, transcendental functions, general equation solving, broader linear algebra, intervals, arbitrary precision, general expression DAGs, concurrent foreign ABI calls, aien-calc grammar/receipt parity, and bridge alias wiring remain out of scope. Capacity refusal can occur even when the final mathematical value would fit. All unsupported-domain and budget results are explicit, with no approximate fallback.

No omega-bridge edit, GPU/service change, live18091 access, extra agent, PR creation, or visibility change. Branch publication is feat/native-symbolic-math on aien-dev/omega-mv. L494 first milestone is ready for the independent review under parent L493; parent L493 remains open. No Wolfram-equivalence claim.
