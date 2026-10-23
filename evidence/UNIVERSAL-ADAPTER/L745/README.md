# L745 integration handoff

## Candidate
- Worktree: `/home/drakestapleton/workspace/hermes-desk/ua-integration-L745`
- Branch: `ua/integration-L745`, based on `origin/main` `a8ffd2b912c84197d08f963b05f9c7d8b30a3f8a`.
- Carried module/evidence trees byte-identical to profile source commit `110a5e5aba2bf903a80c78c4e6f4b1bfd994adbe` and buffers source commit `d588b8905e9c5453fceede78703aaf814cfd9fd1`; compared each source tree to its source commit with `git diff --quiet`.
- Compiler: `/home/drakestapleton/handoffs/universal-adapter-L707/shared/omc`, SHA-256 `191b776ef98821597c3e30655da6d680a9b66edbc9ceaca3c39c61ea6df098e9`.

## Implemented and exercised
Added `tests/gates/omega-native-universal.gate` and `native-universal` runner step in `tests/gates/run.sh`. It hash-pins the OMC, reconstructs every executable from `.om` sources, requires nonempty static AArch64 EXEC ELF without INTERP, and executes actual profile/buffer assertions and compilable behavioral controls. Existing reviewed module test bodies and evidence were not edited.

Direct gate command: `bash tests/gates/run.sh native-universal`
Observed: `NATIVE_UNIVERSAL_PASS profile assertions=1204 profile_mutants=3 buffer suite+rights+read=3 buffer_mutants=6` (exit 0). This includes the profile unit, buffer suite, separate rights/read regressions, the exit-wrap negative control, three compiled profile mutants, three original buffer mutants and two corrective buffer mutants. Mutants were classified only after successful compile to nonempty AArch64 executables.

## Blocking planner findings / remaining
- `aien-local-ci plan --base origin/main --allow-dirty` still reports every `native/universal/profile` and `native/universal/buffers` path as `UNMAPPED`, even though the new manifest lists both trees as inputs. It also reports `BLOCKED_OPERATOR PIPELINE_CONFIG_CHANGED`, including the existing broad gate set. Do not call the planner result PASS or impersonate the operator. Resolve the actual manifest-input mapping behavior before publication.
- Ordinary plan without `--allow-dirty` refused the uncommitted candidate (dirty tree), as expected.
- No source push/PR/judge/deployment was performed. Reviewed-source preservation and functional gate are verified, but pipeline mapping, exact-source committed replay, crumb verification on the final state, independent integration review, L741 operator qualification and remote publication remain open.
- Workflow trigger inspection: candidate main has only PR/push-to-main `aien-verification.yml` and PR-only evidence immutability; no feature-branch push workflow was found.

Keep the L745 ledger open. L741 remains the sole desk writer; parent owns integration acceptance.
