# L568 independent review

**Combined candidate: HOLD (public evidence delivery).** Reviewed exact `fix/osh-host-test-entry@7177ad74cfb5215fa478c270f198af4e07cb33c5`. The four bounded L553 source deltas and L562 fixture implementation are acceptable within Linux host scope. The candidate cannot be accepted as a complete published packet: two checksummed L562 control results are absent from its Git tree. No author source or evidence was repaired by this reviewer.

| Delta | Verdict | Basis |
| --- | --- | --- |
| 4e1e87b2 identities and imported-row cleanup | ACCEPT_HOST_SCOPE | Token-local `=` search precedes value pointer; duplicate keys refuse before overwrite; cleanup reaches normal, invalid-host and emission-error paths. Independent pin controls and compiled controls reproduce exact assertions. |
| 056a9472 physical framing | ACCEPT_HOST_SCOPE | One bounded physical line is drained before interpretation; NUL/oversize protocol records refuse, noise never promotes a continuation into a record, CRLF/final EOF preserved, strict decimal count and ferror refuse. Own 32 plain/sanitizer boundary comparisons pass. |
| 3695dd59 command-string exit boundary | ACCEPT_HOST_SCOPE | Distinct abort-list return2 is consumed only as a command-string stop; stream/script continue. Actual Bash36 comparisons and compiled4/8-assertion controls pass. This bounded acceptance excludes the preexisting invalid-numeric-plus-extra-argument defect F2 below. |
| 86a40330 caller signal actions | ACCEPT_HOST_SCOPE | Five old actions saved and restored in reverse order; partial install rollback; setup/restore failures return70; noninteractive paths untouched. Own all-position oracle checks actual handlers, flags, per-action masks and process mask across all three APIs and both cores. Caller must serialize process-wide action ownership. |
| 7177ad74 parent fixture delivery | HOLD | Fixture implementation ACCEPT_HOST_SCOPE; unchanged assertion/main-body reconstruction, exact oldRED/newGREEN, helper bypass and lock lifetime verified. Public evidence closure fails F1. |

## F1: P2, missing published control results (blocking delivery)

Locations: `evidence/L562-HOST-ENTRY/SHA256SUMS:3` and `:6`; corresponding lossless pins at `lossless-raw-pins.json:58` and `:64`.

The manifest names `controls/no-cloexec/build/osh-host/host.out.gz` and `controls/no-quit-default/build/osh-host/host.out.gz`. Neither is tracked at7177ad74. From the repository root:

```sh
sha256sum -c evidence/L562-HOST-ENTRY/SHA256SUMS
# exit1; two missing files, remaining16 entries OK

git ls-tree -r --name-only 7177ad74 evidence/L562-HOST-ENTRY/controls
# compile.log.gz and run.log.gz only, no host.out.gz

git check-ignore -v evidence/L562-HOST-ENTRY/controls/no-cloexec/build/osh-host/host.out.gz
# .gitignore:1:build/
```

The original uncompressed files still exist locally in the parent's private output, but that does not publish them to another reviewer. The nine available gzip lossless records match their declared original hashes/lengths; the other two are unavailable in the committed packet. Root cause is consistent with recursive `build/` exclusion. Parent should publish the exact original files outside ignored paths, or explicitly include them, and update/verify the packet as necessary. This reviewer did neither. `raw/L562-root-checksums.log` and `fixture-source-and-lossless.json` preserve the failure.

## F2: P2, preexisting Bash mismatch (nonblocking for bounded deltas, still open)

Location: `src/osh/host/osh_builtin.c:657`. `bi_exit` tests argument count before numeric validity. Bash gives a nonnumeric first argument priority even with extra arguments:

```sh
/bin/bash --noprofile --norc -c 'exit bad 9
printf "tail\n"'
# exit2, no stdout, numeric argument required
build/L568/core/osh/osh -c 'exit bad 9
printf "tail\n"'
# exit1, no stdout, too many arguments
```

On stdin and script input, the candidate prints `tail` and returns0; Bash exits2 without printing it. Both Omega core modes reproduce this. An independently rebuilt pre3695 shell driver with the unchanged actual builtin reproduces the original defect in all three modes (including command strings printing `tail`/return0). `git diff 3695dd59^ 7177ad74 -- src/osh/host/osh_builtin.c` is empty. This is not introduced by the four deltas. M3 improves the ordinary too-many-arguments command-string boundary but does not establish general Bash exit equivalence. All15 before/after/Bash rows are in `raw/preexisting-results.json`; six final additional assertions remain actual FAIL, not waived PASS. Route separately to parent/module owner; no reviewer fix.

## Independent execution

Every build used quiet-build and unique owned output. Builds and both host gates use the existing Make lock helper. No timeout wrapper was interposed on the signal-inheritance Make tests. Deliberate initial signals are INT default, QUIT ignored through `env --default-signal=INT --ignore-signal=QUIT` before quiet-build/Make.

- Old test source from3f9eee71, same public Make target via existing source override: exit2,1559pass/5fail. Exact candidate target: exit0,1564plain+1564ASan/UBSan, no failures. Raw old results retained separately.
- Existing target batch `test-osh-prompt-signals test-osh-diff test-osh-e2e osh-trace`: exit0. Prompt254+254, differential66plain/64sanitizer plus2 original RLIMIT_AS NOT_RUN, e2e104cases/320checks,20trace scripts. Native AIENOS33 NOT_RUN in each differential leg.
- Original added pin controls22 comparator-only checks and framing17 sanitizer comparator-only checks: exit0. Actual host reference comes from independent public differential run, no synthetic native file is execution proof.
- All ten L553 compiled behavioral controls independently rerun: identities16/1, framing4/4/1/7, exit4/8, signal95/2 exact failing assertions. All compile0 and terminate normally; script exits0 mean exact expected red assertions observed. No crash, build failure, sanitizer or deadline credited.
- Two independently generated L562 entry variants compile both plain/sanitizer0. Public Make exits2 at exactly1 signal or4 FD assertions. Both plain and sanitizer ELF builds verified in binary pins; Make short-circuits after the failing plain leg, so sanitizer mutant execution is NOT_RUN, not claimed.
- Own signal oracle4746plain+4746ASan/UBSan checks,0failures. Injected every install position1..5 and restore position6..10, all three public APIs and both cores, repeating within the same shell object; tested SA_SIGINFO/SA_NODEFER/SA_RESTART, nonempty action masks, process mask preservation, reverse restoration/rollback states, and loud70 errors. Empty input prevents nested execution-adapter sigaction calls from confusing fault positions.
- Own entry probe confirms INT/QUIT defaults, inherited FD stays open with CLOEXEC, a separate open-file description cannot take the lock while the driver retains it, and can acquire after explicit driver close. Helper bypass on actual shipped test binary preserves QUIT ignored and explicitfd37. `F_SETFD` alters descriptor flags without closing/unlocking; fork retains the shared open description while exec drops marked descriptors. It does not globally close fd8 or change production descriptor creation.
- Own expanded final adversarial batch:68 assertions,62pass/6 actual F2fail, exit1. Reader32/32pass includes299/300-byte values,16383/16384 physical bounds including CR, overflow/trailing/tab counts, zero-padded count, post-end record, malformed prefix, NUL/huge noise, finalEOF and cross-token key. Additional exit30/36pass vs actual Bash; failures only F2. Existing36-case oracle still passes.

Initial supplemental execution used `/usr/bin/true` and a `/dev/null` open without grants. Those policy-denied inputs were an invalid Bash oracle for supported shell behavior, not source findings. They and the first fail-fast supplemental run remain in `raw/adversarial*`; final probe uses builtin printf and descriptor duplication and retains the six genuine F2 failures. No acceptance threshold was relaxed. Initial L562 checksum attempt used the packet cwd although that manifest has repository-relative paths; the final repository-root check independently confirms exactly the two real missing files. Four L553 manifests pass from their specified packet directories.

## Source and evidence closure

All four actual diffs were read relative to their own parents, including reader comparison/cleanup and shell loop/callers. Imported original observer/runner/signal commits ee138918/f362edd9/9d02dff5 have identical stable patch IDs to b5620936/d2c1034b/92d0a3f2. Shipped binary contains neither diff observer nor sigaction test wrapper (public targets check this). The production sources, Makefile and mk files are byte-unchanged by7177 vs3f9eee; actual `tools/with_build_lock.sh` and root Makefile are unchanged againstc100. The entire old test file is reconstructed byte-for-byte by removing only the new entry helper and single invocation. Existing helper dispatch precedes setup; every old assertion and subsequent main-body byte is preserved.

`source-pins.json`, `build-helper.sha256`, `binary-pins.json`, `tool-machine-pins.json`, exact patches, command records, raw logs/results and packet checksum list bind the reviewed work. Private ELF files are pinned, not embedded. Reproduction needs the pinned candidate checkout, build tools and a Linux host. This is independently executed correctness evidence, not author-table acceptance.

Read full assigned brief, L553 handoff/queue/four packets, L562 packet and relevant issue discussions/PR bodies. Cached inventory has historical freshness; live PR572 read during review remains OPEN41c83a71, and exact candidate remote ref matches7177. Ledger/worktree heads and read-only Codex SQLite metadata were reconciled; L563 is the separate live prompt-race owner. No agent was spawned or session changed.

L396 extraction06299ff8 to omega-host requires module-specific carry with PR572 dependencies. Do not re-add retired OSH to canonical Omega. L53163a92c55 carry remains pending in this old descendant. Parent L487/desk L488 retain correctness/integration. L563 prompt-read SIGINT race remains OPEN. No PR/issue remote writes/merge or issue closure occurred. Native AIENOS/QEMU, physical execution, current35B, shared-train, model/GPU and whole-base qualification remain NOT_RUN; energy UNMEASURED. Historical FAIL/INCONCLUSIVE and the two RLIMIT_AS NOT_RUN remain unchanged.

Raw evidence is stored losslessly in `raw.tar.gz`; `raw/` references above are archive member paths. Reviewer-only root generated crumb metadata is refreshed for this new packet; candidate source pins and verdicts remain bound to7177ad74. No source/test/Make changes are included in the review branch.
