# L705 independent corrective review

Runtime fix: **ACCEPT_HOST_SCOPE**. Dispatch verification: **HOLD**. Exact-coverage collector and independent-matrix partition: ACCEPT_HOST_SCOPE within that narrower scope. Whole qualification and canonical integration: **HOLD, NOT APPROVED**. One P2 test-orchestration finding; no runtime finding. L705 remains OPEN for parent disposition; no parent or other item is closed.

Exact reviewed head 8d2afd87289ce457c04104a5b9b180796a956d2c against be37dccaf5e73da735256a6cdae96a9b4f7c19ef. Runtime milestone 26b15c01480cd3617ca99a84c9d9c05be89bf430. Prior independent finding/review 7ccc00f8bc4c9e6438c75448cb6b71e985a3160d was read from local Git. This review authors no production, original test, Make orchestration or language changes. Added C/Node files are bounded host review probes and evidence tooling only.

## Finding L705-F1, P2: required compiled-control phase does not complete

Location: tests/osh/e2e/exit_abort_status_mutants.mjs:28-29, invoked by mk/zz_osh_abort_status.mk:25. The remaining compiled-control runner still executes each complete504-comparison corpus serially with a180000ms aggregate spawnSync timeout. The positive sanitizer corpus on the same corrected source actually took283.48s in the parent's current qualification. The positive independent matrix was sharded, but this required control path was left serial.

While this review was closing, the actual parent new-controls gate failed: Make exit2 and qualification-wrapper exit1. Its log reports ENOENT reading no-restore-sanitizer-cases/results.json at line29. Both control builds succeeded. The plain no-restore control completed its exact112 expected failures with normal assertion exit1. The sanitizer control has partial workload/journal files and no complete results.json. It receives NO behavioral-kill or sanitizer-PASS credit. The remainder of the parent qualification did not run.

The180s aggregate deadline is the strongly supported cause, inferred from source, partial output and the independently completed283s positive run. The driver does not preserve the failed spawn's p.error/status/signal before unconditionally reading results.json, so an actual ETIMEDOUT field is unavailable; the missing file itself is the observed failure. No new heavy rerun was launched. The initial source comment about600s positive batching remains true for that earlier attempt; this is a separate current failure.

Actual reproduction is the parent's preserved command: timeout1800s quiet-build make --no-print-directory -j2 OUT_DIR=/home/drakestapleton/workspace/targets/L611-qualification-8d2afd87/build test-osh-exit-abort-mutants. Public raw copies bind the exact invocation, actual exits, both compile receipts, plain112-failure result, incomplete sanitizer inventory and final logs.

Parent should split or shard this compiled-control path while preserving all504 identities, exact expected failing sets and20s individual-command limits. Record abnormal spawn outcomes before trying to open a result file. Then finish every required control and remaining original regression phase. Review-only authorization prohibits implementing that repair here. Runtime correction retains ACCEPT_HOST_SCOPE; dispatch and integration are HOLD.

## Runtime and actual reproduction

At src/osh/host/osh_shell.c:257-271, the snapshot is immediately before each real execution request. Restoration requires both a parent list abort and a resumable source, and restores only nonzero prior status. The flag is consumed before the next list. src/osh/host/osh_shell.c:319 passes the private source-mode parameter from exact_end; no public ABI changes. Runtime source at 8d is byte-identical to 26b; only osh_shell.c changed among executable host sources since be37. Metadata changes are checked separately from executable source.

Original supported F1 reproducer:

```sh
printf "" | exit 37
exit 7 9; printf "LEAK\n"
printf "seen:%s\n" "$?"
```

Actual C-locale Bash prints seen:37 in script/stdin; the pinned old ELF prints seen:1. Fixed plain and ASan/UBSan ELFs print seen:37. The skipped same-line command does not execute. Following operandless exit returns37 in Bash/fixed and1 in old. Exact -c ends1 and discards the remaining source; prior zero becomes1; invalid numeric exit still ends2. All tested processes terminate normally.

Fresh independent probe:18 programs x3 source modes x2 real cores =108 comparisons per executable. Old be37-derived binary has exactly28 disagreements: F1-print, F1-exit, same-line-update, consecutive errors, EOF, OR connector and blank/comment continuation, script/stdin in both cores. Corrected plain108/108 and sanitizer108/108 PASS. Both cores execute the actual compiled OSC units through the existing driver. No unsupported grammar or replacement parser is used. No build error, crash, sanitizer failure or timeout is credited as a behavioral kill.

Additional boundaries cover same-line reset to success, same-line external failure, updates from12 to37, consecutive count errors with and without a successful intervening command, end without newline, child count errors in both pipeline positions, AND/OR continuation, later valid/numeric exit, and next-list connector status. Six new real controlling-terminal sessions compare Bash and both cores for same-line nonzero preservation and same-line reset-to-zero. All six PASS. PTY prompts and the count diagnostic are observed before requesting status; each owned child exits0 and is reaped.

The independently compiled embedded observer checks nine transitions on one initialized session in each core (18 total): script to command to script separation, same-line reset/update, consecutive errors, interactive script/command entry points and a clean run after abort. All PASS with abort_list=0 and exit_requested=0 on return. Signal actions are restored. The count request/result remains1 while resumable session status is37 or12; command mode session status remains1. The callback sees the pre-update staged status, then run_list stages the restored session value. A subsequent EOF read resets temporary workspace; persistent session status remains correct. Actual subsequent expansions are checked by the separate Bash differential probe.

osh_exec.c:777-786 completes journal outcomes and assigns the raw request status before restoration in the driver. Executor, builtin, journal and exported APIs are byte-unchanged. The observer receives the unchanged OshResult status1. Child builtins cannot set the parent's abort flag. Valid/numeric exits and interrupt paths cannot simultaneously generate this parent count-abort flag under the existing call paths. No leaked action or stale abort was observed.

## Dispatch and evidence verification

Both complete L611 packets verify:34 manifested payloads,20 lossless compressed raw records,16 source-pin entries checked at their exact Git commits. Retained author RED504/112FAIL and independent1224/12FAIL remain failures. Green independent1224 and focused504 assertions are inspected, not credited as fresh reviewer executions. The old aggregate600s actual exit124 remains an incomplete run; it is neither an assertion failure nor a mutant kill.

The normalized independent arithmetic/lifecycle body is byte-identical to L601 review.mjs. All generators, assertions, program text and20s per-command bounds survive. Only explicit modulo case selection and selected-case count metadata change. Four selectors cover indices0..67 exactly once,17 cases per shard. Separate Make leaves preserve both focused builds, both independent builds, PTY, collector controls and the compiled semantic-control target. No original gate is removed. A leaf's overall bound belongs to the outer qualification runner; the dispatcher itself does not impose an additional whole-batch timeout.

The fresh real ASan pilot is independently rechecked from published observations:1224 unique identities,68 programs, exact program text and actual Bash status/stdout/error class against the pinned earlier complete observed matrix, normal candidate/Bash observations, expected executable leg and recomputed candidate equality. Pilot ELF hash matches its dispatch receipt. All four child commands name the actual Node test driver and distinct selectors, with exit0. Source inspection confirms actual Bash still executes for each generated case/mode; the reference fixture is used only for membership and oracle comparison, never as an execution substitute.

The fixture decompresses byte-identically to the M1 complete actual result, with matching raw checksum, length and source provenance. The actual collector rejects missing/duplicate/changed-text/changed-Bash/false-ok/abnormal/wrong-leg rows and requires complete membership; the dispatcher checks child exits AND this collector result. Eight published collector controls and their intentionally permissive RED counterpart were read. Nine separate reviewer controls independently verify the same refusal boundaries plus sanitizer-diagnostic rejection. These are SYNTHETIC mutations of replayed observed rows, not native program executions or compiled behavioral kills. The actual Python collector/dispatcher was source-reviewed and its published complete execution verified; this reviewer did not rerun the heavy full matrix or execute the Python tools.

Inherited L577/L592 Git-object closure passes:9/10 payloads,4782/16077 raw members,260/262 source pins. Twelve archived compiled control sets are independently recomputed with exact expected failing names, successful builds and normal assertion exit1: numeric60/48 and lexical54/84/60/12 in each build. The original complete regression receipt has exit0:host1564+1564, action254+254, e2e104/320 checks, trace20, differential66 plain and64 sanitizer with the original two RLIMIT_AS NOT_RUN, and21 comparator controls. These are inspected pinned author results.251 inherited nonmetadata inputs excluding the changed driver are byte-unchanged; retained binary files are hash-checked. This does not qualify the changed source against the entire original suite.

## Remaining gates and publication limits

At the captured qualification snapshot, the parent-owned durable run has completed coverage, focused plain, focused sanitizer, independent plain, independent sanitizer and PTY leaves with actual exit0. The new compiled-control phase subsequently failed with Make2/wrapper1 as L705-F1. Remaining original regression phases are NOT_RUN in this attempt; independent full-suite rerun is also NOT_RUN. Earlier positive-leaf snapshots and the final failed snapshot are both retained. L611 owns the correction/combined qualification; L487/desk488/canonical570 and existing owner396 retain intake and module integration. Canonical8f8570e6 remains NOT_READY and does not yet carry F1. No integration approval follows from the runtime host-scope acceptance.

Ordinary legacy Linux LP64 C host scaffolding under ADR0024 only. Known preexisting exit --help behavior remains OPEN and out of this contract; no universal Bash claim. Native AIENOS/QEMU, current35B/model, GPU/driver/shared-train qualification are NOT_RUN. Performance and energy are UNMEASURED. No live18091 changes, model loads, module/driver/service changes, issue/PR writes, extra agents or worker/session actions occurred.

Review setup failures are retained explicitly: metadata initially counted as executable source, Make's recursive wrapper dropped an extra review Makefile, an incorrect post-EOF workspace expectation, and a summary filter omitted lowercase passed lines. Each is a review-tool setup error corrected at its actual cause, never a runtime finding or behavioral kill. Old author logs remain untouched.

Public packet contains exact commands/exits, source/binary/tool/machine pins, test inputs and raw observations. SHA256SUMS binds every payload; verify_packet.mjs reads staged AND committed Git objects. External L705 handoff records the final commit, explicit aien-dev/omega-mv push and exact remote-ref readback without a circular self-pin. Desk entry is REVIEW only. L705 stays OPEN for parent disposition.
