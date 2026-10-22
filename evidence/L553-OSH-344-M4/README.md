# L553 #344 interactive caller signal-action restoration

Exact source86a403305f46077b4c81037f5cd0be2e4a07baeb, existing host adapter.
No parser/front-end/language unit change. M3 command-string behavior retained.
New additive make target `test-osh-prompt-signals`; root Makefile and L546
lock helper unchanged. L396 omega-host routing and PR572 carry remain required.

Criterion before tests: interactive run_string/run_script/run_fd returns restore
all five caller handler/mask/flag actions. Noninteractive calls do not alter them.
Same process repeatedly embeds actual native and interpreter cores. Setup failure
rolls back installed actions before any core execution; restore failure is loud70
and attempts other restorations. Process-wide signal-action changes require caller
serialization; this does not fix the separate prompt-read SIGINT race.

Actual commands from own tree under quiet-build:

1. Baseline test replaces only executable main with `tests/osh/host/test_prompt_signal_restore.c` via existing OSH_BIN_SRCS, retaining actual OSC core and old3695dd59 host source, OUT_DIR=build/L553/prompt-red; build0, test1.150pass90fail: every interactive return changes all5caller actions across18cases. Normal C test inputs empty/exit7/syntax refusal; actual backend counters distinguish native/interpreter.
2. Correct scoped setup/teardown around existing run loop; initial normal probe240/240PASS.
3. `make -s -j2 OUT_DIR=build/L553/prompt-final3 test-osh-prompt-signals`: exit0.254/254plain and254/254ASan/UBSan, no skips. Fault wrapper exists only in test binaries and nm confirms absence from shipped osh. Injected setupcall3 refuses70 before core and rolls back two prior actions; injected first restorecall6 refuses70, restores other four, and leaves failed action explicitly unresolved until test cleanup. Empty input isolates that restore path from execution adapter calls.
4. `node tests/osh/host/prompt_signal_mutants.mjs`: final exit0. Both compile; remove final restoration fails95exact assertions (90ordinary plus5restore-failure checks), remove partial setup rollback fails2exact caller-action checks. No signal/crash/compile failure/deadline or unrelated assertion credited.
5. `node tests/osh/e2e/exit_boundary_controls.mjs build/L553/prompt-final3/osh/osh build/L553/exit-final-regression`: exit0,36/36actualbash/native/interpreter comparisons. M3 behavior preserved.
6. Unchanged original `make -s -j2 OUT_DIR=/tmp/L553-osh-regression.DAxToC test-osh-e2e osh-trace test-osh-diff`: exit0.104e2e cases320checks,20trace scripts,66plain differential runs,64sanitizer differential runs+2originalRLIMIT_ASNOT_RUN,21originalcontrolsPASS. Native33NOT_RUN remains, no native-looking synthetic text promoted to execution evidence.

Development failures retained: first fault probe's call6 hit execution adapter's
nested sigaction during exit builtin, not driver restoration;252pass2fail. Corrected
probe uses empty input. Next mutant verdict rejected because fault-case snapshots
inherited contaminated actions from the deliberately broken earlier cases; test
now reestablishes caller actions before each fault case. These were oracle setup
fixes, not production relaxation. The two rejected control-run logs share filenames
in the reduced retained directory; complete originals remain /tmp/L553-prompt-mutants-bDlq30
and /tmp/L553-prompt-mutants-VAhFFg. Final accepted control logs are separate and complete.

Original public make test-osh-host FAIL5 from M3 is still preserved. Same unchanged
plain+ASan host binaries passed1564each under documented cleanstdio/defaultsignals;
that is a separate invocation, not a green public Make target. No build-lock helper
or assertion changed to hide it. Independent review, nativeAIENOS/QEMU,
hardware/current35B, full-base integration NOT_RUN; energyUNMEASURED. Promptreadrace,
other344limits and pendingL531carry remain open. No issue closure/merge approval.
