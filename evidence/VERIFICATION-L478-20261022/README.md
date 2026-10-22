# L478 bounded supplemental verification repair

HOLD for overall readiness. Source fixes are published and targeted host regressions pass. No original PR was rewritten, merged, labeled, judged or triggered. Ledger L478 remains open because official exact-head readiness still has prerequisites. The integration candidate and full shared suite belong to L479; the desk is L480/Renda.

## Source mapping and supersession

| Original item | Pinned source | Supplemental disposition |
|---|---|---|
| Omega614 c6/disasm | 19eebe0e04811002f641c0f7b58c1b87ea9b5c42 | This branch descends directly from it; fixes only the Omega sieve listing and adds a real-call-site regression. Canonical GPU fixes live in CUDApp. |
| Omega615 c3/gemm | a7d6463b9df4485484e207f11fb7802d09aed627 | No GEMM implementation changes. Preserved by existing canonical carry evidence, reused here. Original checks remain blocked. |
| Omega616 c3/reduce-scan | d7e15f18f5fb81555fe932fe3023c9f5f8ab8ba1 | No scan implementation changes. Preserved by canonical carry. Original checks remain blocked. |
| Omega617 c5/isolation, contains C4 streams | f792cd1d35b0e9c191647518c3be651a8d86dc21 | Canonical isolation dump fix and host regression in CUDApp. No recovery implementation changes. |
| Omega618 opencuda/consume | 67c70741c8cec5c33e28a099d7c83bbd4f3ab839 | Read-only inspection. Obsolete extraction must be superseded by L479 consumer integration; do not replay over canonical fixes. |
| CUDApp canonical L457 | 349913f17c083996680c72b32f767b225005155d | Supplemental branch repair/omega-verification-614-618-20261022 descends from this exact source. |
| CUDApp repair | 3414600ba598630b01c240cdd630b33545f8b0d9 then 135c0103712fb3766875c33f210e21d2472ab6d0 | Final tip135c010 includes diagnostic security fixes, regressions, all targeted raw test logs and source/tool/binary hashes. Pin this revision for integration. |

Reuse: CUDApp roadmap/program-20261022 at11d485ade01ac304e9570b9306744f99f3445662, evidence/roadmap-program/latest-carry-identity.json. That existing selected12-path matrix is retained as reused-carry-identity.json; it is not represented as a newly executed full carry audit. Source rechecks show every original head and canonical pin unchanged from fetch-time inspection.

## Demonstrated fixes and validation

CodeQL614 reported8 alerts and CodeQL617 reported1. Exact check-run annotations and complete hosted verification job logs are retained under github/. Canonical fixes restrict file creation to0600, refuse unsafe capture directories and existing files/symlinks, replace snprintf offset accumulation with fixed-size hex encoding, and use private temporary listing streams. The capture-root restriction is documented: user-owned0700 directory beneath the current checkout or /tmp. All original instruction-presence, undecodable-word, branch-target, oracle and mutant assertions are preserved.

The Omega sieve regression includes and invokes the actual existing sm121dis_check, builds a real sieve kernel and plants a symlink at the old predictable listing filename. Original pinned source compiles successfully and fails2/14 checks because it overwrites the target and removes the symlink; repaired source passes14/14, exit0. Separate full sieve host correctness run passes172/172 against the repaired canonical CUDApp archive, exit0. No GPU is opened. Exact commands, source/binary hashes and exits are in sieve-listing-results.json and validation.json.

CUDApp original capture function is byte-identical to Omega614; original isolation test is byte-identical to Omega617 (original-source-identity.json). Capture regression: original4/13 failures, repair13/13. Isolation regression: original5/6 failures, repair6/6. Six compiled behavioral mutants fail at runtime, not compilation. Existing offline decoder mutant rejects10/10 captured kernels over408 words; unmutated decoder agrees with nvdisasm on all10. Targeted host+simulator targets and freestanding codegen build exit0. Detailed CUDApp evidence: https://github.com/aien-dev/CUDApp/tree/135c0103712fb3766875c33f210e21d2472ab6d0/evidence/verification-l478

## Remaining exact gates

See failure-disposition.md for all22 original non-PASS rows and original blob hashes. No archived verdict was rewritten.

- CRUMB-VERIFY: hosted verification stops with6/3/3/7 stale crumbs on614/615/616/617. Later hosted tests are skipped. Authorized metadata/desk owner must repair final integrated metadata; no crumb compile or gate waiver done here.
- OMEGA-R11-OWN-HOLD: all four owner cases are refused under load. L345 owns the stronger actual-living-run repair. Prior L33623a34113 is explicitly HELD for passing a skip. No load-limit bypass or timing run here.
- OMEGA-R13-HOST615: real mode2 failure, incumbent cost never confirmed. Existing R13 repairs are candidate prerequisites, not verified fixes for this exact record. OMEGA-R13-TESTBUILD-SILICON616 has mode3 same symptom; a105-check Fabric subcase passing does not make the overall gate pass.
- OMEGA-M17615/616: channel allocation refusal, no demonstrated wrong arithmetic. OMEGA-M19614/615/616: clean-clone/cumulative regression failures; archived stdout does not expose a complete clean-clone root cause, so that remains UNKNOWN and cannot be called fixed.
- OMEGA-M5/M19617: old pinned driver headers lack multi-channel declarations. L457 already carried those canonical dependencies; current canonical library builds in our targeted tests. OMEGA-COMPOSITION-GATE-GPU617 fails a physics pin mismatch before test execution. L479 must bind the final consumer dependencies; no duplicate driver patch here.
- OMEGA-NUMERIC-TRANSC-FULL: BLOCKED_OPERATOR on all four, no executed test receipt. Operator admission required.
- Official CodeQL rescan, AIEN judge, hardware, model load, hang/recovery, benchmarks and shared full suite: NOT_RUN by L478. Coordinator-owned hardware qualification and Renda admission remain required. Host correctness does not certify C1-C6 or CUDA replacement.

No live18091, driver/modules, coordinator/desk ownership or cron changed. No worker spawned or original worker resumed. No PR writes, merges, labels, bypass keys or judge triggers.
