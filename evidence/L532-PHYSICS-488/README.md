# #488 header consumer and concurrent stamp oracle

L532 additive host oracle over immutable existing Makefiles. No production
Makefile, driver, sibling worktree, service or hardware state changed. Compiler
spy outputs explicitly marked synthetic non-object JSON; no marker is linked.
Existing PR495 implementation owner retains all fixes. Independent review of
these new tests NOT_RUN. Real driver/full build/chip/current35B gates NOT_RUN.

Command executed from exact base04aab149 plus the two new test files:

```
quiet-build node tests/physics_header_identity_controls.mjs build/L532/controls
```

Exit0 means the nine-row matrix obtained its exact declared results, including
an actual candidate concurrency FAIL. It does not mean candidate acceptance.
The individual concurrent probe exits1. Every expected failure must match its
named assertion; crashes, make errors, timeouts and unrelated failures do not
count as killed controls. Fresh private Git fixture trees for each row.
Source refs and Makefile SHA256s are in controls.log; candidate495 is
`a1896dcf68a899fe58ffbc32559fd4b3fa5bd4b3`, SHA256
`d53730d0eddb1da9d568a010db0bafd9f8d5eab3407ae15007bac66d14aa0af8`.

The existing real `omega_accelerator_world.o` recipe receives a private
compiler-style .d closure naming m16/m16_native.h. The spy reads its actual
-I header path and bytes, captures Git HEAD and stamp, records hashes, and
rewrites that .d just as a compiler would. Header and driver input mtimes are
forced back to2000-01-01. Reused output directory and target. No test Make rule
supplies the dependency under test.

| Source / control | Actual result |
|---|---|
| Original85d8343f | RED directory switch reuses wrong header output |
| Existing5468a4f15a6 | Same header-consumer RED; its direct driver coverage remains valid |
| Existing495 serial | PASS directory, revision, dirty header, dirty driver input, alias, switch-back and valid cache hits |
| Existing495 refresh | PASS controlled publication boundaries, complete stamp and unchanged-cache preservation |
| Existing495 overlapping builders | FAIL mismatched output accepted as current |
| Remove header stamp dependency | Killed at directory switch |
| Remove dirty diff hash | Killed at dirty older-mtime header |
| Always rewrite stamp | Killed at valid-cache-hit boundary |
| Incomplete stamp identity | Killed at complete-stamp validation |

## New bounded failure for existing owner

```
git show a1896dcf68a899fe58ffbc32559fd4b3fa5bd4b3:Makefile > PRIVATE/Makefile
quiet-build node tests/physics_header_identity_test.mjs PRIVATE/Makefile PRIVATE/out concurrent
```

The oracle first builds A. A changes revision, and its next compiler request
captures A inputs then pauses on a Unix socket. While A is paused, B finishes
its real stamp and object recipe in the same OUT_DIR. Release A: its late output
replaces B's object. B's stamp remains complete and names B. A subsequent B build
rewrites no stamp and invokes no compiler, accepting A's output as current.
`concurrent-state.json` captures both actual identities, hashes and the cache
hit. No sleeps select the schedule. Deadline is only a failing watchdog.

This is a host structural reproduction, not proof about actual compiled driver
bytes or hardware effects. Atomic stamp replacement protects the stamp's file
contents but does not serialize stamp-plus-output publication across independent
make processes sharing OUT_DIR. Concurrency acceptance remains FAIL/open. Desk
should route the reproduction to the existing495 owner and decide the supported
shared-output concurrency contract. No fix or warning weakening was made here.

The separate refresh test pauses A's actual mv after its private stamp candidate
is complete, checks the old published stamp, lets B refresh, then releases A's
real /bin/mv and checks the exact complete candidate appears. Two unchanged
refreshes preserve the valid cached object. This bounded schedule checks atomic
publication; it does not prove every possible concurrent source mutation safe.

Actual regressions (both exit0):

```
quiet-build sh tests/physics_build_identity_test.sh build/L532/sources/pr495.Makefile build/L532/direct-regression dirty
quiet-build make --no-print-directory -f build/L532/sources/pr495.Makefile OUT_DIR=build/L532/host-regression PHYSICS_DIR=/nonexistent test-gpu-wait test-language
```

No lock override, skip or retry. Full logs published. Host machine/tool pins are
in environment.txt. Original earlier FAIL/INCONCLUSIVE evidence is untouched.
