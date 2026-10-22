# L516 command register

All commands run from `/home/drakestapleton/workspace/omega-trace-review-fixes-20261022`. Base `ed6ddfb485dd8401e770ad0750713fa007122670`; explicit destination `https://github.com/aien-dev/omega-mv.git`, branch `issues/trace-review-fixes-20261022`. Build directory `/home/drakestapleton/workspace/targets/L516/build` owned by L516. No GPU/model/native engine execution. Raw test elapsed times are incidental, not performance measurements.

Authoritative final commands:

- `OMEGA_TRACE_BUILD_DIR=/home/drakestapleton/workspace/targets/L516/build quiet-build timeout 120s tests/trace/run.sh --under-quiet` -> `final-host.log`, `.exit`: exit0, 50/50 tests (original40 plus10), zero skipped/cancelled; SearchTrace verify/hookcheck2; native encoder oracle2 records/12 assertions; deliberate socket EPERM and original offline8 checks.
- `OMEGA_TRACE_BUILD_DIR=/home/drakestapleton/workspace/targets/L516/build OMEGA_TRACE_MUTANT_EVIDENCE_DIR=evidence/L516/original-mutant-assertions quiet-build timeout 120s node tests/trace/mutants.mjs` -> `final-original-mutants.log`, `.exit`, per-mutant logs/exits: original17/17 killed by targeted assertions.
- `OMEGA_TRACE_REVIEW_MUTANTS=evidence/L516/final-review-mutant-assertions quiet-build timeout 120s node tests/trace/review-mutants.mjs` -> `final-review-mutants.log`, `.exit`, per-mutant logs/exits: nine/nine killed by assertions. No timeout/import/syntax failure counted.
- `OMEGA_TRACE_BUILD_DIR=/home/drakestapleton/workspace/targets/L516/build quiet-build timeout 60s strace -f -qq -e trace=network -o evidence/L516/network-syscalls.log /home/drakestapleton/workspace/targets/L516/build/network-deny env OMEGA_TRACE_NETWORK_DENIED=1 node tests/trace/offline.mjs` -> `network-result.log`, `.exit`: exit0,8 checks,0 network syscalls; only SIGCHLD process notifications.
- `quiet-build timeout 60s strace -f -qq -e trace=network -o evidence/L516/review-network-syscalls.log /home/drakestapleton/workspace/targets/L516/build/network-deny env OMEGA_TRACE_NETWORK_DENIED=1 node tests/trace/review-offline.mjs` -> `review-network-result.log`, `.exit`: exit0,14 checks, empty network trace. Exact spans preview/package, unknown-loss packaging refusal/new recovery, residual purge warning, unchanged source snapshots.

RED and intermediate receipts retained, never overwritten:

- `quiet-build node --test --test-timeout=30000 tests/trace/review-fixes.test.mjs` on original source -> `red.log`, `.exit`: exit1, T1 missing metadata disclosure; T2 stale/active lock falsely PASS, startup root absent, queued cut lacks writer-loss disclosure; T3 residual falsely PASS. Two clean controls pass. No product changes existed for this run.
- Same targeted command after initial fix -> `green-initial.log`, `.exit`:7/7.
- `full-initial.log`, `.exit`:47/47 plus existing host/native/offline checks; intermediate source, not final receipt.
- `green-expanded.log`, `.exit`: failed test-authoring attempt used24 receipt references, exceeding existing schema limit16. Own runner stopped after a worker remained open; not a passing gate. Corrected fixture to16 valid references and installed cleanup; `green-expanded-fixed.log`, `.exit`:9/9.
- `targeted-final.log`, `.exit`:9/9 before the additional worker-adoption failure regression.
- `accepted-host.log`, `.exit`: intermediate49/49. `accepted-review-mutants.log`, `.exit`, `review-mutant-assertions/`: intermediate8/8.
- `quiet-build timeout 30s node --test --test-name-pattern 'T2 adopted worker' tests/trace/review-fixes.test.mjs` -> `adopt-failure-red.log`, `.exit`: real worker fsync EIO made reserved marker disappear, falsely PASS. Same command after preservation fix -> `adopt-failure-green.log`, `.exit`:1/1.

`session-reconciliation.txt` is read-only local app-server thread metadata queried from state_5.sqlite, checked alongside git worktrees, live L516/L517/L511/L513 ledger/handoffs. No worker restarted, stopped or reassigned. Only L516 progress notes changed. Source-language/native files and historical evidence/L489 unchanged.
