# L601 combined exit review

Recommendation: **HOLD** for the requested combined lifecycle acceptance. One preexisting lifecycle defect is newly reproduced; it is not a regression introduced by the two reviewed source changes. Exact candidate be37dccaf5e73da735256a6cdae96a9b4f7c19ef; source deltas 5583bea09cb38cb3cac48fa893f76998746f400b and 47aaea70323016f59f1306aafebc1aa4a9311624; baseline f2dbbb241cd9b3c0302d1bc3630d61f617d12c2c. Independent reviewer did not author either source change. Review-only branch, no production or original-test edits.

## Finding L601-F1, P2: nonzero prior status lost after count error

Location: src/osh/host/osh_exec.c:786, related driver abort handling src/osh/host/osh_shell.c:265 and builtin src/osh/host/osh_builtin.c:670-674. A parent count error returns1 and sets abort_list. Execution assigns res->status to s->last_status; the driver clears abort_list and continues the next script/stdin list. Actual Bash instead preserves a prior nonzero status for this continuation. With prior0, Bash and Omega both expose1. Command mode stops with1 in all tested versions. The numeric/delimiter deltas preserve this preexisting path.

Minimal supported reproducer:

```sh
printf "" | exit 37
exit 7 9
printf "%s\n" "$?"
```

As script or stdin, actual Bash prints37; both pinned baseline f2dbbb24-derived and candidate be37dcca-derived ELFs print1. All exit0 and emit the count diagnostic. Replacing the final printf with operandless exit gives Bash exit37 versus Omega exit1. Removing the prior-status pipeline gives a matching control (prior0 becomes1); adding or removing a skipped same-line command does not remove the prior37 mismatch. No delimiter is needed. The corresponding -c controls end1 and do not execute the next line.

Actual finding_repro.mjs records120 observations across five programs, three modes, baseline/candidate, plain/ASan and both cores:16 mismatches for the prior37 case,104 matching controls. minimize.mjs separately records24 plain observations across three programs, script/stdin, both versions/cores:16 mismatches for preserved-status output and operandless exit,8 matching zero-prior controls. Together they establish the prior nonzero status as the trigger. The main independent matrix independently reports12 failures per build (24 total), all valid-extra parent script/stdin continuations with prior37, and passes1212/1224 per build. No abnormal process, sanitizer error or deadline was involved. The baseline ELF hashes are rechecked against the original L577 Git packet.

The author522/168 corpora test later-line execution but do not inspect the status entering that line after a count error with a nonzero prior status. Their passing counts therefore leave this lifecycle behavior uncovered. This is a real coverage gap, not a packet hash failure. The main failing assertions are retained unchanged; they are not treated as mutant kills.

Parent should preserve the observed nonzero-prior continuation behavior while retaining the zero-prior and -c controls, diagnostics, current-list abortion and child isolation. Parent owns the fix and regression. No production source was edited. HOLD applies to requested combined lifecycle acceptance; no newly introduced defect was found in the two numeric/argument deltas themselves. Parent can explicitly disposition this preexisting finding before accepting a narrower claim.

## Source review

Only osh_builtin.c changes among host implementation files. At lines 657-658, exactly one initial delimiter advances the operand index. Lines 661-668 clear errno, convert decimal signed long, preserve the original conversion end to reject no digits, consume trailing space/tab only, and reject conversion overflow or remaining bytes before considering count. The Linux LP64 restriction makes long the intended signed 64-bit range. Leading whitespace follows C-locale strtol. Empty, signs without digits, pure whitespace, nondecimal/junk, and repeated delimiters are numeric failures. Lines 670-679 preserve count errors as abort_list/return1, successful and invalid numeric operands as exit_requested/exit_status in the parent, and no session flag writes in children. Masking a signed LP64 value produces the required modulo256 status on this pinned host.

Caller review: osh_exec.c:441 invokes a standalone builtin with in_parent=1. Pipeline children call with0 at line697 and exit through the child path; parent state is not mutated. osh_shell.c:263-265 distinguishes whole exit from abort_list and clears the latter after use. At lines314-315, a count error stops an exact -c input; script/stdin sources can continue with the next list. Missing operand and delimiter-only retain the prior status. This is existing replaceable Linux C host scaffolding under ADR0024. No language/compiler/front-end or native runtime change.

## Evidence review

Both complete author handoffs, public packet readmes, corpus and mutation drivers, actual source/call paths, and issue344/568 discussions were read. The current inventory contains84 issues and63 PRs. Ledger, recent CLI/app-server SQLite session metadata and worktrees were reconciled read-only; existing parent/desk/canonical-module owners retained.

Actual Git-object packet checks pass: L5779payloads/4782nested raw members/260source pins, L59210payloads/16077raw/262source pins. All49 retained author ELF pins match the published hash, including plain and sanitizer candidate and controls. Current executable sources match the tested L592 source; generated .crumb metadata is excluded only from the current-tree executable comparison and remains verified at its pinned Git source by the original verifier. Source/test/Make/tool bytes are unchanged between47aaea70 andbe37dcca, excluding metadata. Current author tool hashes also match, and own compiler, libc, tools and binary hashes are recorded.

All12 original compiled control result sets were independently checked for their exact named failures, successful compile, normal assertion exit1, and raw status/stdout/diagnostic-class comparisons. Numeric60/48 and lexical54/84/60/12 are separate plain and sanitizer observations. Selected count-first and repeated-delimiter corpora are actually replayed below, rather than merely accepting counts.

The unchanged original public Make regression run is inspected, not rerun: actual exit0, host1564plain+1564ASan; signal actions254+254; e2e104cases/320checks; trace20; differential66plain and64ASan plus2 original RLIMIT_AS NOT_RUN; comparator21controls. Its invocation, source/build closure, binary and tool pins and raw log are retained. This is author qualification reused with matching inputs, not an independent rerun or a whole-module acceptance claim.

Historical failures remain failures: the first prior-status cd fixture produced114 mismatches with extraneous diagnostics, while the corrected supported no-diagnostic fixture gave108 actual implementation mismatches in each build. The inherited-source verifier initially rejected metadata and was corrected to compare executable source. L577's initial Make include ordering failure and aggregate sanitizer timeout remain preserved in the original immutable packet, never credited as behavioral kills. Our own initial metadata comparison, broad binary selection, fault-expectation and operandless-exit minimization expectation setup errors are likewise retained and not credited as source findings or kills.

## Independent execution

Actual independent result:1212PASS/12FAIL plain and1212PASS/12FAIL ASan/UBSan from68 operand cases across three modes, three process positions and both real cores. The overall driver exits1 on its original correct assertions, with the exact24 discrepancies preserved. No crash, watchdog or sanitizer failure is involved. Direct state checks38/38 in each build PASS; compiled arithmetic fault12 exact failures per build with normal process exit0 and all other checksPASS. Actual original count-first replays60 exact failures per build out of168; repeated-delimiter12 per build out of522; each corpus exits1 normally with the exact published failing identity set.  Independent test source and actual commands/raws are published with this review. The new arithmetic oracle uses BigInt modulo, signed-boundary expectations and explicit numeric/count classes, separately from the host's strtol. Actual Bash in LC_ALL=C and private0700 HOME/state supplies lifecycle stdout/status/diagnostics for both real compiled cores, command/script/stdin and parent/first/last pipeline positions. Direct inclusion of the unchanged builtin checks return value and exact exit_requested/exit_status/abort_list/last_status state for parent and child contexts. A separately compiled fault clamps negative nonzero remainders to zero and must fail exact expected arithmetic assertions with a normal process exit, in both plain and ASan/UBSan.

## Scope and disposition

HOLD exact be37dcca for requested combined lifecycle acceptance pending parent disposition of P2 L601-F1. Independent normal gate status is FAIL, not NOT_RUN or a mutant kill. Known preexisting exit --help behavior remains OPEN: Bash prints help and continues, while baseline/candidate numerically refuse it. The27 preserved option observations keep this separate from numeric/delimiter acceptance. No new regression is inferred from unchanged out-of-contract help. Unsupported subshell grammar remains a named refusal; pipeline isolation is tested directly.

Native AIENOS/QEMU, hardware/current35B/shared training NOT_RUN; performance/energy UNMEASURED. No universal Bash, whole344/568 closure, canonical module deployment, L563 prompt acceptance or L570 integration claim. ParentL487/deskL488 owns disposition and canonical carry. L601 remains OPEN pending parent disposition; a desk REVIEW record is not merge approval.
