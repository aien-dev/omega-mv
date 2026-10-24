# Public benchmark candidate boundary

Parent: Omega673/670 and653, spark-rsi67. Existing implementation owner: L775, with independent
public fixtures L776 and existing RSI judge/promotion/store owners unchanged. This proposal starts
at exact L775 `dd28ebc0a84abc4dab3a38bde49aeae98c846c78`; `src/learning/benchmark/core.om` is unchanged.
Only `.om` qualification consumers, adversarial fixtures and exact-source controls are added under
that owner's existing test directory. No second evaluator, signer, store or production runner is
introduced. No consumer is switched and no legacy implementation is deleted by this slice.

## Threat, assets and enforcing boundary

An untrusted static candidate can return wrong bytes, no bytes, too many bytes or a fabricated
approval, or attempt to open/truncate the judge, policy, key, receipt and budget paths. All five
files contain the literal public marker `SYNTHETIC-NONSECRET-QUALIFICATION`; there are no real keys
or protected tasks. The parent owns expected byte42 and collects actual output from the process.
The existing `lobserve` consumes that observation. Candidate-reported approval is ordinary output,
never authority. Files are checked byte-for-byte after every candidate invocation.

Kernel enforcement comes from the existing L408 compiler `hpack/hpure`, source at Omega265e19c0,
compiled SHA25687542cfdf91e9666851b6e21d16c3a281547a8f7520dcedcd1f2a8e8540285e1. The fixed
profile closes extra descriptors, grants only selected-inode execution and stdio, and installs
Landlock/seccomp/rlimits. The native test consumer forks, assigns three disposable regular streams,
invokes execpure and reaps the actual child with a ten-second deadline. Signals, launch failures,
wrong status and missing I/O invalidate this campaign; they cannot establish a semantic pass.

Supported run: Linux AArch64, exact platform in platform.txt. strace6.8 supplies an independent
observation of real kernel calls. This is native CPU/kernel testing, not emulation, physical device
qualification or formal proof. Core0 is insufficient with this host's piped Apport handler; only
public synthetic inputs are used. Private/holdout confinement remains unqualified.

## Results through the existing component

| Mode | Candidate behavior | Actual child exit | Existing lobserve result |
| --- | --- | --- | --- |
| 0 | Correct byte42 | 0 | 0 correct |
| 1 | Wrong byte43 | 0 | 7 wrong bytes |
| 2 | Attempt read/write of all five protected paths, then wrong byte43 | 0 after ten exact EPERM assertions | 7 wrong bytes |
| 3 | Empty output | 0 | 9 empty |
| 4 | Arbitrary failure | 77 | 8 exit mismatch |
| 5 | 257 output bytes | 0 | 10 malformed observation |
| 6 | Fabricated APPROVED string | 0 | 7 wrong bytes |
| 7 | Repeat known-correct byte42 | 0 | 0 correct |

The complete native consumer returned0. It requires all eight exact rows and exact protected file
contents. Mode7 repeats a known behavior; it is not an artifact rollback, revocation or persistent
agent-decontamination experiment. Modes1/2 are synthetic malicious behavior, not an empirical model
optimizer poisoning result or a claim of generalization.

Meaningful compiled negative controls:

1. `security-transform 0` changes only the existing byte-comparison refusal from7 to0 in a
   composed qualification copy. Mode1 is then incorrectly scored0 and the native consumer fails109.
   It passed mode0 first. This catches corrupted observation semantics without using a crash.
2. `security-transform 1` changes only the caller's execpure to unrestricted execprog. Mode2
   actually opens/truncates the disposable judge file, then the attack fixture fails61 because
   expected EPERM was absent. The parent records observer8 and fails109. The trace shows the actual
   write-capable open; the judge is0 bytes versus33 bytes in the fixed campaign. No other resource
   was targeted. This catches absence of the actual candidate isolation boundary.

Both transformed sources compiled successfully and were ELF-inspected before execution. Exact
exit109, preceding result rows and actual trace/file effect identify the intended failures;
an unrelated failure, signal, build error or missing run would be INVALID.

## Reproducible commands and raw evidence

Working directory: `/home/drakestapleton/workspace/omega-security673-benchmark`.
The copied compiler was measured against the independently rebuilt strict-profile compiler above.
The consumer composition uses the unchanged existing module, as its documented test contract does.

```
cat src/learning/benchmark/core.om tests/learning/benchmark/security-boundary.om > build/bench-security-tools/consumer.om
build/bench-security-tools/omc < build/bench-security-tools/consumer.om > build/bench-security-tools/consumer
build/bench-security-tools/omc < tests/learning/benchmark/security-candidate.om > build/bench-security-tools/candidate
build/bench-security-tools/omc < tests/learning/benchmark/security-transform.om > build/bench-security-tools/transform
```

Every build returned0, stderr empty. readelf -h of each executable returned0 before chmod. Raw
build results and headers are retained. The fixed execution command was:

```
timeout 30s strace -f -o evidence/LEARNING-652/security-boundary/native.strace -e trace=close_range,openat,landlock_create_ruleset,landlock_add_rule,landlock_restrict_self,prlimit64,seccomp,execveat,read,write,clone,wait4,fsync build/bench-security-tools/consumer build/bench-security-tools/candidate
```

stdout native.log, stderr native.err and exit native.exit are retained. Each mutant was produced by
`build/bench-security-tools/transform MODE < build/bench-security-tools/consumer.om`, then built with
the same compiler and inspected before execution. Mutant runs used:

```
timeout 30s strace -f -o evidence/LEARNING-652/security-boundary/mutant-MODE.strace -e trace=close_range,openat,landlock_restrict_self,seccomp,execveat,execve,read,write,clone,wait4 build/bench-security-tools/mutant-MODE build/bench-security-tools/candidate
```

MODE0 and1 each returned109 with empty stderr. The fresh exclusive scratch directory was renamed
between runs to `build/bench-security-fixed`, `build/bench-security-mutant-0` and `...-1`; an existing
scratch directory causes refusal115. Exact source/compiler/binary/configuration/fixture identities
are in identities.txt; all raw generated artifacts are archived. External cat, readelf, sha256sum,
timeout and strace are disclosed development/review tools, not a foreign product implementation.
No new first-party shell/Python/Rust/C implementation or automation was introduced.

## Remaining acceptance requirements

This supplies a real process-output observation boundary for the existing public benchmark
component and blocks tested candidate file modifications. It does not implement statistical
improvement evidence, protected adaptive/final batches, calibrated feedback, restart-safe attempt
accounting, authenticated receipts, parent-bound promotion or release authorization. The existing
RSI v2 signer/policy/ledger and GEN custody must supply those in their owned `.om` successors.
There is no actual model, tokenizer, training run or adaptive optimizer in this campaign; those
identities and gates are NOT_RUN, not invented. No speedup, statistical error guarantee or power
claim follows from these eight deterministic cases.

The trusted parent and executable bytes remain assumptions; the test's command-line child path
is not an authenticated artifact admission API. Candidate failure cannot modify the parent's
compiled expected bytes in these process tests, but unrelated same-user host processes and crash
collection are outside this bounded guarantee. The host crash-policy blocker must be resolved
before private inputs. The release path remains with existing Renda/desk owners; no automatic
promotion, deployment, GPU work or issue closure occurred.

## Final-source independent replay

The final consumer reads until EOF or the257-byte cap, including protected-file comparisons;
a short read is not a complete-payload assertion. Original author artifacts and identities remain
in the before-eof archive. The final author's native8case replay returned0. The independent final
snapshot rebuilds this exact source and both mutants: fixed0, comparison bypass109 and isolation
bypass109, with the named result/real file effect reproduced. See `independent/REPORT.md` and its
complete snapshot archive. This is the final-source evidence for the corrected reader.
