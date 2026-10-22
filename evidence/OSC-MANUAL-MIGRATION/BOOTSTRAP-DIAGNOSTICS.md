# Bootstrap diagnostic checks moved to Omega

The two `grep -q` calls in `seed/omega-from-seed.sh` are replaced by the existing seed-native byte comparator. They now require complete diagnostic bytes, rather than accepting any message containing an error-number substring. No new compiler, language word or duplicate comparator was introduced.

The pinned compiler's observed diagnostics are retained in `seed/native/diagnostics/{cycle,assign}.txt`: cycle reports line 7/error 9, and assignment to a later function reports line 2/error 7. The associated input programs and compiler source are unchanged. This is a frozen diagnostic contract for this compiler pin, not a general assertion about all future compiler versions.

## Verification

`bootstrap-diagnostics.py` runs six isolated source-export scenarios. The old script passes the intact baseline but fails five required controls: it depends on grep, accepts error 90 as error 9, accepts the wrong line, accepts trailing text, and ignores a missing golden fixture. The new script passes all six, including a full birth with grep disabled. Refusals must carry the named diagnostic, not merely any failure.

Wolfram independently computed the fixture bytes and SHA256 fingerprints and confirmed that the three corrupt messages match the old substring rule but fail exact equality. The exact evaluator input and normalized returned JSON are retained as `diagnostics-wolfram.wl` and `diagnostics-wolfram.json`. Its abc hash matches the standard test vector. `wolfram-diagnostics.py` checks actual fixture files and the native Omega hasher against those results, then exercises the native comparator against all three Wolfram refusal controls. Wolfram remains an external verification oracle, not an Omega build/runtime dependency.

The bootstrap now uses no grep, sha256sum or cut calls. Early cmp and shell/file orchestration remain. This is a caller-level retirement on the migration branch, not a system-wide utility removal, full legacy retirement or GPU-driver replacement.

## Prior independent SHA review

Reviewer `sa-0-b3c89136` (delegation `deleg_dea30aab`, ledger L559) reported no blocking correctness or trust-boundary findings for the SHA command/integration at source `8f8be9a6`, confirmed exact source reuse and reviewed retained host compiler authentication. The review was read-only; it did not run builds or mutation tests and does not cover subsequent bootstrap edits.

Three nonblocking coverage gaps remain: fragmented writes do not guarantee short reads, partial output writes/non-SIGPIPE write failures are not forced, and post-progress read errors/EINTR are not directly exercised. These are not silently marked tested. Independent review of the diagnostic caller delta and the full repository suite remain unclaimed.
