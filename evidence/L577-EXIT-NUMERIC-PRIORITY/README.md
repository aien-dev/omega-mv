# L577: exit numeric validity takes priority over argument count

Tested source commit:5583bea09cb38cb3cac48fa893f76998746f400b, parent f2dbbb241cd9b3c0302d1bc3630d61f617d12c2c. This repairs only L568F2 in the existing replaceable Linux host builtin for Omega344. No compiler, parser, language unit, shell-driver, host test fixture, existing controls, Makefile or lock helper changed. The new Make include and tests have numeric-priority-specific names. C remains host scaffolding under ADR0024, not an Omega-native foundation.

## Criterion and actual results

The first numeric argument is validated before excess arguments are rejected. An invalid value (including empty, sign-only and signed overflow) exits2; a valid value plus excess arguments returns1 and aborts the current list. Parent exit_requested vs abort_list ownership stays distinct, and pipeline children cannot exit their parent.

Actual `/bin/bash --noprofile --norc` decides status, stdout and diagnostic class on every test invocation. Fresh0700 HOME/state/journal directories, PATH=/usr/bin:/bin, LC_ALL=C and TZ=UTC; only builtin commands and granted pipeline effects. Both actual compiled Omega cores, command/script/stdin,28 programs/168 comparisons. Cases cover eight invalid-extra forms, invalid single args, signed and64-bit boundaries, valid-extra list stopping, ordinary valid/missing exits, modulo256, and first/last pipeline-child exits. No authenticated-native or physical-execution claim follows from metadata.

- Before source change:108PASS/60FAIL plain. Same60 exact failures under ASan/UBSan. Includes the original six clean parent failures.
- Fixed source:168/168plain and168/168ASan/UBSan PASS through new public `make test-osh-exit-numeric-priority`.
- Compiled count-first control:60 exact priority failures; invalid-extra-continues control:48 exact whole-exit failures. Complete plain and sanitizer verdicts are recorded in commands/results and raw control logs. Compile errors, crashes, sanitizer findings and deadlines never count as kills.
- Fresh original public Make gates under deliberately inherited ignoredQUIT and actual quiet/build locks:host1564plain+1564ASan, prompt-action254plain+254ASan, e2e104cases/320checks, trace20, differential66plain/64ASan with the original2RLIMIT_AS NOT_RUN and21original comparator controls. All executable gates exit0.
- Existing actualBash36 comparisons PASS. Existing two compiled exit controls fail exactly4/8 assertions and two prompt-action controls exactly95/2, all compile0 and terminate normally.

## Explicit open validation limits

No universal Bash equivalence is claimed. `raw/validation-scope/results.json` compares actual baseline and candidate with Bash:

- Trailing whitespace in a quoted number: `exit "7 "` is a preexisting validator mismatch (Bash7; baseline/candidate2). For `exit "7 " 9`, the old count-first result1 happened to match Bash; moving priority exposes the existing validator defect and the candidate returns2. This **new combined-form mismatch remains OPEN**, explicitly outside this numeric-validation-order repair. No acceptance assertion was dropped to hide it.
- `exit -- 7` option handling was already incorrect (Bash7, baseline1, candidate2) and remains OPEN.
- Subshell syntax remains a named SUBSHELL refusal before effect. This repair tests supported pipeline-child ownership; it adds no subshell grammar.

The packet retains both unsuccessful infrastructure attempts: initial new Make include sorted before osh.mk (`/osh` prerequisite, exit2), corrected by naming the include z-osh-exit-numeric-priority.mk; and the first expanded sanitizer control aggregate90s runner timeout, which is NOT_A_KILL. A complete sanitizer corpus exceeds that budget on this host. Outer runner budget corrected to240s, while all assertions, exact expected failure sets and inner20s command deadlines remain unchanged. The original successful plain-only controls and incomplete expanded attempt are retained separately. No production gate was weakened.

## Reproduction and pins

From the exact tested source checkout on a Linux AArch64 host with GCC, GNU Make, Node and Bash:

```sh
quiet-build make -s -j2 OUT_DIR=/absolute/fresh/output test-osh-exit-numeric-priority
quiet-build node tests/osh/e2e/exit_numeric_priority_mutants.mjs /absolute/fresh/control-output
env --default-signal=INT --ignore-signal=QUIT quiet-build make -s -j2 OUT_DIR=/absolute/fresh/regression test-osh-host test-osh-prompt-signals test-osh-e2e osh-trace test-osh-diff
quiet-build node tests/osh/e2e/exit_boundary_controls.mjs /absolute/fresh/regression/osh/osh /absolute/fresh/exit-cases
quiet-build node tests/osh/e2e/exit_boundary_mutants.mjs
quiet-build node tests/osh/host/prompt_signal_mutants.mjs
```

Source/tool/machine/binary pins, commands and exits bind actual execution, not an estimated result. `raw.tar.gz` contains lossless raw outputs, actual Bash per-case stdout/stderr/status/commands, fixtures, compiled-control source and build records, original gate outputs and retained unsuccessful attempts. ELF binaries are hash-pinned rather than embedded. Generated journal/key files are local fixture state and are not evidence payloads. `raw-members.json` binds every archived regular file. `verify_packet.mjs` verifies SHA256SUMS and every member directly from staged or committed Git objects, avoiding the historical ignored-build-path delivery failure. See commands.json for exact paths and statuses.

## Ownership and qualification

L553 four source deltas + L562 entry fixture + append-only F1 publication repair remain intact. L568 independent review accepted those bounded sources; this new L577 change has independent review NOT_RUN. No L563 prompt-race source was imported or edited. L396/L487/deskL488/L570 must combine exact qualified patches in canonical omega-host; this old descendant must never re-add retired OSH to canonical Omega. L531 source carry remains a separate integration dependency.

READY HOST applies only to this bounded correction and published host evidence, pending independent review and canonical module integration. Native AIENOS/QEMU, hardware/current35B/model/GPU/shared-train qualification NOT_RUN; energy/performance UNMEASURED. Historical FAIL/INCONCLUSIVE and original NOT_RUN rows stay unchanged. No PR/issue writes, merge, issue/parent closure, live18091 changes or other-agent session actions.
