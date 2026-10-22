# Exact-head failure disposition

| Original PR / check | Observed failure | Disposition |
|---|---|---|
| 614 CRUMB-VERIFY | Stale crumbs: 6. Hosted job also exits1 here; later hosted stages skipped. | HOLD. Generated metadata repair belongs to authorized desk/crumb owner. No compile or gate waiver performed. |
| 614 OMEGA-M19 | Clean-clone reproduction fails; cumulative regression158/159, receipt prerequisite fails.  | HOLD hardware/clean-clone. Root of clean-clone failure not exposed by archived console blobs; UNKNOWN beyond observed failure. No hardware rerun or unsupported root-cause claim. |
| 614 OMEGA-NUMERIC-TRANSC-FULL | BLOCKED_OPERATOR; no receipt or test execution. | NOT_RUN. Operator/desk admission required, never reinterpret as numerical failure. |
| 614 OMEGA-R11-OWN-HOLD | Own hold q1-abc refused; script expected living run. Same shape on all four pinned heads. | HOLD L345. L336 skip-accepting23a34113 explicitly held. No timing/load override or speed run. |
| 615 CRUMB-VERIFY | Stale crumbs: 3. Hosted job also exits1 here; later hosted stages skipped. | HOLD. Generated metadata repair belongs to authorized desk/crumb owner. No compile or gate waiver performed. |
| 615 OMEGA-M17 | Native channel creation refused RM_ALLOC status0x51, subsequent submission/parity/completion gatesFAIL. | HOLD hardware/admission observation. Does not demonstrate wrong GEMM/scan values. No channel/driver/recovery test run here. |
| 615 OMEGA-M19 | World init refused rc-8; clean-clone and cumulative regression fail. | HOLD hardware/clean-clone. Root of clean-clone failure not exposed by archived console blobs; UNKNOWN beyond observed failure. No hardware rerun or unsupported root-cause claim. |
| 615 OMEGA-NUMERIC-TRANSC-FULL | BLOCKED_OPERATOR; no receipt or test execution. | NOT_RUN. Operator/desk admission required, never reinterpret as numerical failure. |
| 615 OMEGA-R11-OWN-HOLD | Own hold q1-abc refused; script expected living run. Same shape on all four pinned heads. | HOLD L345. L336 skip-accepting23a34113 explicitly held. No timing/load override or speed run. |
| 615 OMEGA-R13-HOST | Mode2 no-promotion-authority: AIEN never confirmed incumbent cost; gateFAIL. | HOLD. Real host failure in archived receipt. Existing R13 branches are candidate prerequisites; their applicability to this exact failure is UNVERIFIED. No new timing run authorized. |
| 616 CRUMB-VERIFY | Stale crumbs: 3. Hosted job also exits1 here; later hosted stages skipped. | HOLD. Generated metadata repair belongs to authorized desk/crumb owner. No compile or gate waiver performed. |
| 616 OMEGA-M17 | Native channel creation refused RM_ALLOC status0x51, subsequent submission/parity/completion gatesFAIL. | HOLD hardware/admission observation. Does not demonstrate wrong GEMM/scan values. No channel/driver/recovery test run here. |
| 616 OMEGA-M19 | Clean-clone reproduction fails; cumulative regression158/159, receipt prerequisite fails. Some archived channel creation failures are also present. | HOLD hardware/clean-clone. Root of clean-clone failure not exposed by archived console blobs; UNKNOWN beyond observed failure. No hardware rerun or unsupported root-cause claim. |
| 616 OMEGA-NUMERIC-TRANSC-FULL | BLOCKED_OPERATOR; no receipt or test execution. | NOT_RUN. Operator/desk admission required, never reinterpret as numerical failure. |
| 616 OMEGA-R11-OWN-HOLD | Own hold q1-abc refused; script expected living run. Same shape on all four pinned heads. | HOLD L345. L336 skip-accepting23a34113 explicitly held. No timing/load override or speed run. |
| 616 OMEGA-R13-TESTBUILD-SILICON | Mode3 revoked-experiment: AIEN never confirmed incumbent cost. Fabric silicon subcase105/105 passed, overall test-buildFAIL. | HOLD. Preserve overallFAIL; no new silicon/timing run. |
| 617 CRUMB-VERIFY | Stale crumbs: 7. Hosted job also exits1 here; later hosted stages skipped. | HOLD. Generated metadata repair belongs to authorized desk/crumb owner. No compile or gate waiver performed. |
| 617 OMEGA-COMPOSITION-GATE-GPU | Build preflight refuses supplied physics6d7cf0d while head lock pins7476de7160. | HOLD exact dependency binding. Canonical carry already provides driver; L479 consumer pin integration prerequisite. No hardware result produced by this failure. |
| 617 OMEGA-M19 | Compilation fails: NVRM_MAX_EXTRA_CHANNELS and multi-channel API declarations absent from supplied old driver headers. | Existing canonical L457 carry already supplies API; canonical library builds in our targeted run. No duplicate driver fix. Original pinned official check stillFAIL. |
| 617 OMEGA-M5 | Compilation fails: NVRM_MAX_EXTRA_CHANNELS and multi-channel API declarations absent from supplied old driver headers. | Existing canonical L457 carry already supplies API; canonical library builds in our targeted run. No duplicate driver fix. Original pinned official check stillFAIL. |
| 617 OMEGA-NUMERIC-TRANSC-FULL | BLOCKED_OPERATOR; no receipt or test execution. | NOT_RUN. Operator/desk admission required, never reinterpret as numerical failure. |
| 617 OMEGA-R11-OWN-HOLD | Own hold q1-abc refused; script expected living run. Same shape on all four pinned heads. | HOLD L345. L336 skip-accepting23a34113 explicitly held. No timing/load override or speed run. |

All original bundle, receipt and stdout/stderr bytes are retained under spark/. Receipts with zero exit claims are never synthesized. Four BLOCKED_OPERATOR rows have no receipt. No archived FAIL was changed to PASS.

CodeQL614 annotations8: capture path/permissions/snprintf, four canonical listing files and one Omega sieve listing. CodeQL617 annotation1: isolation dump permissions. Dedicated regressions reproduce unsafe I/O on exact original bodies and pass after repair. Official CodeQL rescan NOT_RUN.
