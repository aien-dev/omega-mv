# L537: shared-output Physics build concurrency

Host correction READY for independent review. Source commit
`ab070f60fcb9d31bf8d1caa6caeec435571cf5c0`, branch
`issues/physics-build-concurrency-fix-20261022`, based on L532's safe test milestone
`52dbe78bbcb81d776a2d7ce96b1976dba3527ebb`. This base includes remote main
`a8ffd2b912c84197d08f963b05f9c7d8b30a3f8a` (ancestor check exit0).
No runtime, arithmetic, allocator, driver, service, model or hardware changes.

## Result and design

The original exact PR495 Makefile still fails the forced interleaving:
A captures inputs and pauses; B completes stamp and object; A publishes late;
next B accepts A's object without compiling. `original495-concurrency-red.log`
records exit1 at `no_mismatched_output_accepted_as_current`.

Atomic stamp rename alone cannot bind later object publication. Private names
for Physics objects alone would leave shared header consumers, archives and
executables exposed. Moving all caller-visible targets to identity-dependent
paths would change existing direct target and output path contracts. The chosen
correction therefore serializes **the entire make lifetime for each resolved
OUT_DIR**, before mk includes, .d reads, stamp comparisons or object decisions.
The inner make keeps parallel compilation and the GNU jobserver. Builds using
different output directories proceed independently. A symlink spelling of the
same directory shares its lock.

`tools/with_build_lock.sh` holds a kernel flock on an adjacent persistent inode.
Keeping it outside OUT_DIR prevents the existing clean rule from removing the
lock inode. The descriptor survives through make/compiler descendants. A killed
make cannot release exclusion while its compiler could still publish. Normal
exit and errors release it through descriptor closure. There is no idle holder,
global quiet/chip lock or service change. Never unlink an active lock file.
Recursive makes for the same output inherit the held scope and do not deadlock.
`.DELETE_ON_ERROR` removes partial targets from a failing compiler so the next
build cannot mistake a failed output for a cache hit.

The PR495 stamp/dependency delta was carried onto the assigned **current**
Makefile, preserving every existing rule/source list. `source-preservation.json`
records exact byte equality with the original after removing only the declared
insertions. No historical Makefile replaced the current file. This is a
follow-on derived from PR495 `a1896dcf68a899fe58ffbc32559fd4b3fa5bd4b3`, not
ancestry through that branch and not a new owner for PR495/546.

## Acceptance

`quiet-build node tests/physics_build_concurrency_controls.mjs build/L537/acceptance`
exited0. Exact commands, actual exits, source hashes and assertion names are in
`acceptance-command.log` and `acceptance.<sha256>.json`.

| Case | Actual verdict |
|---|---|
| Existing original serial header oracle | PASS: directory/revision/dirty header/dirty driver with older mtimes, Physics alias, switchback, no-op hits |
| Concurrent different identities | PASS: actual kernel waiter; B builds matching object after A releases; next B valid hit |
| Concurrent same identity | PASS: actual waiter, only A recompiles, B reuses matching object |
| Concurrent stamp refresh | PASS: A candidate remains private; B waits, then publishes complete B stamp; unchanged refresh preserves cache |
| Real object lifecycle | PASS: tiny header-reading C objects linked/executed as A11/B12; failed partial object removed; compiler/recipe recovery; nested make two recipes overlap without jobserver warning; independent outputs progress; output alias shares lock; killed inner make's surviving compiler retains exclusion; final B object/stamp match |
| Remove exclusion | KILLED: original exact mismatched-output assertion, exit1 |
| Release exclusion before running make | KILLED: original exact mismatched-output assertion, exit1 |
| Remove failed-target cleanup | KILLED: `failed_partial_object_removed`, exit1 |

The original forced B-completes-before-A schedule is **infeasible** under the
correct serialization and is not claimed to have run on the candidate. The new
explicit interleaving starts A, observes its compiler/stamp barrier, starts B,
observes an actual blocked kernel lock request on the output's inode, releases A,
then completes B. Deadlines always fail. They never count as exclusion evidence.
Both removed/early-release controls still execute the original schedule and
fail its unchanged safety assertion. No assertion in the original tests was
removed, skipped or edited.

The original immutable nine-row controls were rerun: exit0 with all nine exact
historical expected outcomes, including the original PR495 concurrency FAIL.
Original tests and `evidence/L532-PHYSICS-488/` are unchanged. Four historical
mutants remain caught. `historical-results.json` and raw `historical/` logs record
that distinction. Serial direct driver dirty/missing-source regression exited0.
Actual unaffected host build `make -j2 ... test-gpu-wait test-language` exited0:
130 wait checks,12 required mutant kills,407/407 language assertions.

Synthetic compiler-spy rows remain `SYNTHETIC_NON_OBJECT`; none is linked or run.
The separate real lifecycle harness invokes the real C compiler on tiny fixture
sources, links and executes them. Their source, object, depfile, probe and tool
hashes are in `pins.json`; exact compile output is in `real-objects-make.log`.
Fixture Makefile parsing prints a preexisting missing unused aienos.lock notice;
it is not treated as a refusal or a successful safety assertion.

## Reproduction, pins and limits

See `commands.txt` for actual top-level commands/exits, `environment.txt` for
machine/kernel/tool versions and measured box load, `pins.json` for source,
compiler/test/binary/input hashes, and `SHA256SUMS` for this packet. Tests ran
under actual shared-box load with bounded waits; there is no performance claim,
quiet timing result, estimate of speed or current35B measurement.

Development failures are preserved explicitly: the first extracted stamp rule
had incorrect dollar quoting (watchdog exit2); an early termination harness hit
its watchdog (exit2), then make-exit observation and barrier-disconnection/pipe
handling were corrected. They are not acceptance runs. No timeout, unrelated
error, or crash is counted as a killed mutant. Final source has no test hook in
production recipes and no diagnostic instrumentation.

Scope: cooperating invocations of this Makefile with a shared output directory
and stable source trees during each build. This does not snapshot files edited
mid-compile, coordinate older unwrapped Makefiles or outside writers, or promise
that a binary stays unchanged after its make invocation has finished. Separate
output directories remain the way to retain independent results. Linux flock
and realpath are host-tool prerequisites. The normal root Makefile entry point,
fixed target names, relative/absolute OUT_DIR and recursive make paths were used.
Arbitrary supplemental externally supplied Makefiles were not qualified.

Independent source review: **NOT_RUN**, required before integration. Full real
driver/libomega_gpu build, GPU/chip gates, current canonical artifact/current35B,
whole-model quality/performance, and shared-train qualification: **NOT_RUN**.
Existing canonical/shared-train owner retains those gates; this host correction
does not close #488. L487/L488 desk should review this exact source with L532
safe-test and PR495-derived stamp dependencies; keep original PR495 concurrency
FAIL historical and prefer this source over its production Makefile proposal
for this concurrency boundary. No PR/issue writes, merges, closures, extra
agents, stopped-session interactions, or hardware loads were performed.

Final gate strengthening: gate-only commit
`5d819b58a7e25a44c87f2563ec115a5ebd703d4b` requires precisely one JSON FAIL
row with the expected assertion name from each mutant, with no acceptance on a
matching PASS name plus an unrelated failure. All eight cases rerun exit0;
`exact-acceptance/` and `exact-acceptance-command.log` are the final gate records.
`exact-pins.json` pins every tiny fixture input/artifact and the final gate source.
Production Makefile/helper are byte-identical to ab070f60; this changes only the
result checker. Both source milestones were pushed and exact refs verified.
Existing real physics-lock refusal also tested: missing PHYSICS_DIR returns2
through the dispatcher, unchanged refusal diagnostic, no override.
