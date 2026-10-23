# L776 bounded public admission evidence

Action `L776-654-public-admission-01`, resumed by explicit user authorization on
2026-10-23. This is a bounded, in-memory public record consumer. It is neither a
generated curriculum pilot nor full Omega #653/#654 acceptance.

Tested source is `d8d14c47647e49774f64b58e3c138335656f175d`. The exact dependency
is L775 `84d03c5523f39ca1045d1c313c5f62597779462b:src/learning/benchmark/core.om`.
The dependency's owner-supplied freeze is preserved as `profile-ready.json`;
its actual core hash was independently recomputed before implementation.
`artifacts.sha256` binds the consumed sources, composed translation units and
native executables. Executables remain in the ignored local build directory.

The first compile failed with error12 at the compound oracle/template check.
Pinned compiler `alloc` refuses after exhausting16 temporary registers. The
same three checks were split into individual branches, retaining error43.
`attempt1.om`, stderr, exit and cost preserve this failure. `attempt2.om`
preserves the successful composed source. Evidence source snapshots are not
new production implementations of the benchmark core.

The production build compiles, passes the existing static AArch64 ELF verifier
and exits0 with `CURRICULUM_ADMISSION_PASS\n`. Both deliberate replacement
modules also compile and pass ELF verification. The provenance bypass fails
assertion33, exit83; the source-identity bypass fails assertion43, exit93.
These controls show that the metadata guards are exercised. The caller still
supplies identities and observed bytes: authenticated source measurement and
custody remain external dependencies.

Tests cover accepted and quarantined indices; a two-example hardcoded answer
failing a third predeclared input; changed manifest after begin; source first
and last digest words; schema, profile, license, provenance, oracle/template,
evidence class and sample-count errors; reserved words; syntax and timeout;
empty, short, wrong-trap and oversized outputs; changed ELF identity; missing
costs; duplicate submissions; incomplete runs; protected final split; task
count and budget overflow; and clearing readiness after a failed new begin.
Observation bytes and metadata in this suite are synthetic. Its three affine
observations are public test data, not a held-out model evaluation campaign.

Four compiler invocations and three native invocations were measured, including
the failed compile and both failing controls. All report wall/user/system time
0.00 seconds at GNU time's0.01-second resolution; this is below measurement
resolution, not zero cost. Peak RSS was1516KiB per measured invocation. Three
ELF-verifier invocations, source assembly, editing, hashing, review and publication
were not timed. Agent implementation tokens, energy and total lifecycle cost
remain unknown. No model requests occurred in these native tests. No speedup,
training improvement or complete-lifecycle-cost result is claimed. The action
remained within its eight-compile,16-run, five-second-per-invocation budget.

Existing host `git`, `cat`, `timeout`, GNU `time`, `sha256sum`, `quiet-build` and
the existing `verify-elf.sh` were development orchestration tools. No new shell
program or foreign-language executable/test was added. Native launch, wait,
timeout, output capture, file loading and durable resume still belong to the
existing compiler/runtime owner L408. Public/hidden custody and authenticated
receipt integration remain with their existing owners. No generated corpus,
repair loop, hidden data access, model/GPU operation or consumer switch ran.

The new Xerj workflow was read before further exploration. This session has no
loaded Xerj MCP method, so current GitHub and immutable local sources were used
directly. Index coverage was not established; no shared index was changed.
Existing independent-review and Renda publication ownership is preserved.

Reproduction data is in `commands.json`; it describes external orchestration,
not a delivered test harness. Resolve the two exact source commits, assemble
the dependency/module/test in that order, compile with the pinned compiler,
verify each nonempty ELF before executing, then compare exit and stdout.
