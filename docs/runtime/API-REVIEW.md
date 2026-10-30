# Omega Runtime API v0: independent review record (omega-mv#594, PR #955)

This file is the review record that `docs/runtime/API.md` cites. It summarises two read-only review lanes; their full
notes are not in this repository. Host only (aarch64 Linux, gcc 13.3.0): no GPU, no chip run.

## Review 1: ApiReview594, head 2908f13a against main 62d1d0d6

Scope: `docs/runtime/API.md`, `docs/runtime/omega_runtime_api.manifest`, `tools/runtime_api_check.sh`,
`tools/runtime_api_negative.sh`. Verdict: no blocking finding; 7 should-fix (F) and 5 nit (N). Fixed in commit e25aa1bb.

| ID | Finding | Fix |
|---|---|---|
| F1 | Recovery cited omega-mv#518, which is a lock leak in `omega_gpu_decode_graph_begin_token`, not session recovery. | Cite cut C5 and `evidence/C5-ISOLATION/`. |
| F2 | "The RM client close destroys this process's channels and frees every buffer" was stated as fact; the C5 chip run is NO RESULT, then NOT RUN, and the host evidence uses a fake platform (ordering only). | Stated as intended, not shown; the host-proven ordering kept. |
| F3 | "Frees every buffer" contradicts `src/omega_gpu_platform_linux.c:198-199` (buffers the caller did not free are not unmapped). | API.md and the `omega_gpu_session_recover` header comment say the per-recovery leak of CPU mappings is not known. |
| F4 | "Every section 9.5 call takes the lock" was too broad: the accessors do not, and `omega_gpu_matmul_cta_budget` reads unlocked what `omega_gpu_matmul_set_cta_budget` writes under the lock (`src/omega_gpu_matmul_api.c:146-153`). | The locking and lock-free calls are listed by name; the race is named. |
| F5 | "With no platform installed the lock does nothing" holds only for the freestanding build; the hosted build installs the Linux reference through `pthread_once` (`src/omega_gpu_session.c:109-115`). | Stated per build. |
| F6 | Section 9.6 said "except the three STABLE session entries"; every entry in that table is EXPERIMENTAL. | "Every entry here is EXPERIMENTAL." |
| F7 | The review-state line predated the changes under review. | Replaced by the reviewed revisions and this record. |
| N1 | The check went manifest to headers only, so a new public function was not seen. | Reverse scan with gcc `-aux-info`; control `new-header-function`. |
| N2 | `OmegaGpuCmdListOpts.first_barrier_none` was in the manifest only. | Prose in section 9.3 (`src/omega_gpu_cmdlist.h:85-87`); control `removed-padding-field`. |
| N3 | The launch argument checks were incomplete (`L` NULL, `spin_us` bound) and the `timeout_ms` 0 text was wrong. | Matches `src/omega_gpu_session.c:525-527` and `session_wait`. |
| N4 | INTERNAL helpers are declared in the public `omega_gpu_session.h` with no mark there. | INTERNAL comments on four declarations. |
| N5 | Recovery from a command-list-only latch and the return values of `omega_gpu_session_recover` were not described. | Section 3 and the 9.1 row. |

## Review 2: Rev955, head 7be2c6f7 against main 62d1d0d6

Verdict: F1-F7 and N1-N5 confirmed fixed; one major and five minor/nit findings, fixed in the commit that adds this file.

| # | Finding | Fix |
|---|---|---|
| 1 (major) | The section-10 parser took every backticked `omega_*` name in section 10 as INTERNAL, including the manifest functions the prose mentions (`omega_gpu_reserve_serving`, `omega_gpu_serving_seal`, `omega_gpu_serving_release`). Dropping `fn omega_gpu_serving_seal` from the manifest still gave PASS. | Only the leading names of a section-10 bullet count, and a name that is both a manifest `fn` and INTERNAL fails. Controls `manifest-fn-named-in-prose` and `internal-also-manifest`. |
| 2 | Section 9.3 said "a timeout latches the list and then the session"; `force_latch` (`src/omega_gpu_cmdlist.c:233-240`) latches the session only if its 1 ms check launch also times out. | Reworded, pointing to section 3. |
| 3 | `test-runtime-api` ran in no workflow and was not `.PHONY`. | `.PHONY`; gate `tests/gates/omega-runtime-api.gate` (requires `portable`), so the Spark judge and the AIEN Checks portable smoke set run it. |
| 4 | Citation `omega_gpu_platform_linux.c:197-198` was off by one. | `:198-199`. |
| 5 | API.md and the review-fixes README cited a home-directory path as the review record. | This file. |
| 6 | Section 3 grouped every `*_last_error` call with the section 9.5 accessors, but `omega_gpu_elementwise_last_error` is INTERNAL (section 10). A function returning a function pointer would have been read as declaring `int`. | The accessors are named one by one; the name split skips a `(*` declarator; control `new-fnptr-function`. |

Not checked by either review: that the prose is true beyond the cited lines, and any chip behaviour (recovery, extra
queues): NOT RUN.
