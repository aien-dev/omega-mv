# L552 executed commands and scope

All source exports are `git archive EXACT_HEAD` materializations below `/home/drakestapleton/handoffs/omega-issue-sweep-20261022/L552/{decoder,decoder-old,protocol,trace,trace-old}`. The reviewer branch starts at corrected decoder676a0b80. Each target/output was fresh, under `/home/drakestapleton/workspace/targets/L552-review`. No foreign asset was written. Author refs were read using `git ls-remote https://github.com/aien-dev/omega-mv.git ...`; see author-remote-refs.txt. Ledger/session/worktree reconciliation is retained privately in sweep/L552; active/ended state does not transfer ownership. No session was resumed or signalled.

From reviewer repository root:

```sh
quiet-build python3 evidence/L552/run_checks.py > evidence/L552/check-run.log 2>&1
quiet-build python3 evidence/L552/decoder_extra.py "$PWD" /home/drakestapleton/workspace/targets/L552-review > evidence/L552/decoder-extra.log 2>&1
```

Both exit0. `commands.json` gives every core child argv/cwd/exit including expected red exits1. Decoder original67 raw cases are retained. Native expected output and all342 extra request hashes are in protocol-extra.log. Full decoder gate is retained/inspected, not rebuilt. No original600 regression executable was rerun; prior receipts are explicitly inspected only. Five native mutant binaries reused after SHA checks; compile-success provenance inspected, test exits1 independently reproduced.

Using S=/home/drakestapleton/handoffs/omega-issue-sweep-20261022/L552 and O=/home/drakestapleton/workspace/targets/L552-review, the exact actual startup invocations from the reviewer repository were:

```sh
quiet-build node evidence/L552/startup_probe.mjs "$S/trace-old" "$O/startup-old" old > evidence/L552/startup-red.log 2>&1
quiet-build node evidence/L552/startup_probe.mjs "$S/trace" "$O/startup-fixed" fixed > evidence/L552/startup-green.log 2>&1
quiet-build node evidence/L552/startup_probe.mjs "$S/trace-old" "$O/startup-old-fixed-expectation" fixed > evidence/L552/startup-red-assertion.log 2>&1
```

First two exit0 because each asserts its expected old/new behavior; raw JSON records show the defect and correction. Third exits1 with ERR_ASSERTION using identical fixed expectations on old source. The clean corrected probe remains exit0. No archive probe touches real user data. It changes only owned synthetic fixture names/bytes, and verifies the actual archive files remain unchanged across injected reservation failure.

From S/trace:

```sh
quiet-build env OMEGA_TRACE_BUILD_DIR="$O/trace-build" ARGUS_REPO=/home/drakestapleton/workspace/aienos-argus bash tests/trace/run.sh
quiet-build env OMEGA_TRACE_BUILD_DIR="$O/trace-build" OMEGA_TRACE_MUTANT_EVIDENCE_DIR="$O/trace-mutants" node tests/trace/mutants.mjs
quiet-build env OMEGA_TRACE_REVIEW_MUTANTS="$O/trace-review-mutants" node tests/trace/review-mutants.mjs
quiet-build strace -f -qq -e trace=network -o "$REVIEW/evidence/L552/trace-network.log" "$O/trace-build/network-deny" env OMEGA_TRACE_NETWORK_DENIED=1 node tests/trace/review-offline.mjs
```

REVIEW is the absolute reviewer repository; shell commands used these full absolute paths. Each command redirected stdout/stderr to its named public trace log and wrote its actual exit to the matching .exit file: all0. Full51-test suite zero skips. Mutation runners intentionally select one behavioral test per mutant; their selection counts are not full-suite runs. Detailed selected assertion failures and exit1 records are retained. Offline14-check workflow has empty network log.

Packet checks:

```sh
(cd "$S/decoder" && sha256sum -c evidence/L547-DECODER-LINKS/SHA256SUMS)
(cd "$S/protocol" && sha256sum -c evidence/L547-PROTOCOL/SHA256SUMS)
(cd "$S/protocol" && sha256sum -c evidence/L547-PROTOCOL/sources.sha256)
sha256sum -c evidence/L552/binaries.sha256
quiet-build python3 evidence/L552/verify_packets.py
```

All final exits0. First `verify_packets.py` attempt exited1 because its own retained-log string expectation was incorrect; original failure log retained separately. Corrected exact gate-text check exits0. All other outputs remain as originally produced. Original parent's failed combined-expression compile, initial strict-whitespace packaging failure, and trace development mutant failures stay in their respective immutable packets; they are not passing evidence.

Re-execution requires fresh output names because fixtures refuse overwrite. Scripts record stable seeds and request digests. No throughput, latency or energy measurement is claimed by these correctness commands; elapsed test-run diagnostics are incidental.

Final publication check initially returned2 because raw Node assertion diagnostics contain whitespace-only lines and a raw Git patch includes context lines. That check output is retained losslessly as `format-check-initial-failure.log.gz`. Every offending raw file and all exact source patches were compressed with gzip, preserving their original bytes; `lossless-publication.json` records original SHA256/length and published filenames. No diagnostic text or gate was edited. `gzip -dc FILE.gz` recovers exact raw evidence. Final staged whitespace check is rerun normally. Raw originals remain in the owned sweep/L552/raw-publication-originals directory.
