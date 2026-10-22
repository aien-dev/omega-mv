# L592: bounded exit numeric whitespace and -- framing

The existing Linux host bi_exit accepts a single initial -- and trailing space/tab on a decimal number. Numeric validity still precedes excess argument count. Missing operands retain prior status; valid extra arguments set abort_list, invalid operands request whole exit2, and children cannot exit their parent. No parser, shell driver, prompt race code, Make/build helper, configuration or original test fixture changed. C remains existing replaceable host scaffolding under ADR0024. This is a follow-on to L577 source5583bea on baseac6fe2c7, preserving append-only F1 repairf2dbbb24; L563 prompt sourcef49e6a91 is separate for L570 carry after its accepted independent L585 review.

## Exact behavior and oracle

Actual /bin/bash --noprofile --norc runs for each comparison in explicit LC_ALL=C, PATH=/usr/bin:/bin, TZ=UTC and fresh0700 HOME/state/journals. Both compiled Omega cores run87programs in command/script/stdin:522 actual comparisons per plain/sanitizer build. Cases cover each ASCII whitespace leading/trailing/interior/alone/extra; empty/sign/signed64 bounds and overflows/junk/nondecimal; one -- with absent/valid/invalid/empty/extra/repeated-delimiter operands; prior13 status and pipeline first/last child isolation. Status, exact stdout and diagnostic class are checked; raw exact stderr is retained. ASan/refusal/crash/deadline errors are infrastructure failures, never mutant kills.

The actual Bash oracle accepts all six tested leading ASCII whitespace bytes, but trailing only space/tab. Trailing newline/CR/VT/FF and whitespace-only are numeric refusals. The -- delimiter is consumed once, so `exit -- --` remains invalid. In -c, valid extra aborts the full command string; script/stdin stop the current list and continue the next line. No grammar expansion is involved.

## Actual results

Tested source47aaea70323016f59f1306aafebc1aa4a9311624, explicit assigned-branch push and exact remote readback verified. No source changes after this milestone.

- Baseline actual RED:414PASS/108FAIL of522 in both plain and ASan/UBSan.
- Fixed source:522/522plain and522/522ASan/UBSan PASS. Original numeric-priority168plain+168ASan PASS.
- New compiled controls in each plain/sanitizer build:reject-blanks54exact failures; accept-all-whitespace84; ignore-delimiter60; repeat-delimiter12. Every remaining assertion passes, every control compiles0 and completes normally with test exit1.
- Original numeric-priority controls retain exact60/48failures in each plain/ASan build and all original168assertions. No source-hook change was needed: count-first restores the old function; action hook and original corpus unchanged.
- Original publicMake gates exit0:1564plain+1564ASan host,254plain+254ASan action restore,104e2e cases/320checks,20trace,66plain+64ASan differential with original2RLIMIT_AS NOT_RUN and21comparator controls. Original actualBash36 and compiled4/8exit,95/2prompt-action controls PASS.
- Unchanged inheritedL577 packet9payloads/4782rawmembers/260sourcepins and F1 original18manifest payloads verify from Git.

## Reproduction

Use a Linux LP64 host, GCC/GNU Make/Node/Bash pinned in tool-machine-pins.json, and fresh output directories:

```sh
quiet-build make -s -j2 OUT_DIR=/absolute/output osh /absolute/output/osh/osh_numeric_priority_asan
quiet-build node tests/osh/e2e/exit_argument_completion.mjs /absolute/output/osh/osh /absolute/cases/plain
quiet-build node tests/osh/e2e/exit_argument_completion.mjs /absolute/output/osh/osh_numeric_priority_asan /absolute/cases/sanitizer
quiet-build node tests/osh/e2e/exit_argument_completion_mutants.mjs /absolute/new-controls
quiet-build make -s -j2 OUT_DIR=/absolute/output test-osh-exit-numeric-priority
quiet-build node tests/osh/e2e/exit_numeric_priority_mutants.mjs /absolute/numeric-controls
env --default-signal=INT --ignore-signal=QUIT quiet-build make -s -j2 OUT_DIR=/absolute/regression test-osh-host test-osh-prompt-signals test-osh-e2e osh-trace test-osh-diff
quiet-build node tests/osh/e2e/exit_boundary_controls.mjs /absolute/output/osh/osh /absolute/exit-cases
quiet-build node tests/osh/e2e/exit_boundary_mutants.mjs
quiet-build node tests/osh/host/prompt_signal_mutants.mjs
```

The inherited original Make entry fixture supports ignored SIGQUIT and live inherited quiet/build-lock descriptors; no clean-environment substitute or assertion weakening was used. Original ASan RLIMIT_AS limitations remain unchanged. Source.patch.gz contains only this follow-on delta. Source/binary/tool/machine pins and commands.json bind actual runs. Raw.tar.gz is a lossless archive of per-case Bash/core commands, outputs/status/error class, control sources/build/test records and original gate logs; ELF binaries are hash-pinned, not embedded. Fixture journal/key files are excluded. verify_packet.mjs reads every payload and archive member from actual staged/committed Git objects.

## History and limits

The initial draft seeded prior status with failing cd, causing six additional error-class mismatches from its diagnostic; retained draft408PASS/114FAIL is not the true implementation RED. The corrected no-diagnostic pipeline fixture gives414PASS/108FAIL before production edits. Initial inherited-source verifier rejected generated crumb metadata; corrected verifier excludes metadata only and checks executable-source bytes plus baseline ELF hashes. Neither is a behavioral control kill. Old evidence packets, historical FAIL/INCONCLUSIVE and F1 missing-file repair remain intact.

Separate existing `exit --help` special behavior remains OPEN: actual Bash prints help and continues while baseline/candidate reject it as numeric.27 exact Bash/baseline/candidate option observations are in raw/option-observations. This bounded numeric/delimiter qualification is not universal Bash equivalence or whole issue344/568 closure. Subshell and other unsupported grammar remain named refusals; no new language/frontend or compiler was built.

READY HOST is pending independent review and L396/L570/parentdesk canonical omega-host integration. This descendant must not re-add retired shell code to canonical Omega or overwrite newer module source. Native AIENOS/QEMU, model/current35B/hardware/sharedtrain qualification NOT_RUN; performance/energy UNMEASURED. No extra agents, session restarts, PR/issue writes, merges, parent closure or live18091/service/model/driver changes.
