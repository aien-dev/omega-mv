# R1 Runtime API: review findings fixed (#955, omega-mv#594)

Review record: `docs/runtime/API-REVIEW.md`, review 1 (ApiReview594, head 2908f13a against main 62d1d0d6; the same
tree as PR head 933dcdba). Host only (aarch64 Linux, gcc 13.3.0), no GPU, no physics checkout.

| File | Result |
|---|---|
| `test-runtime-api.log` | exit 0: PASS (96 functions, 28 structs, 237 fields, 159 constants) and all 13 negative controls caught. |
| `red-stale-internal-and-no-aux-info.log` | exit 1 twice: a name in API.md section 10 that no header declares, and a compiler without `-aux-info`. |

What changed:
- N1: `tools/runtime_api_check.sh` scans the manifest headers back with gcc `-aux-info`. Every external function they
  declare must be a manifest `fn` or be named in API.md section 10; every name there must still be declared. The scan
  found 41 declarations in no list; section 10 now names all of them. New controls: `removed-padding-field`
  (`OmegaGpuCmdListOpts.first_barrier_none`), `new-header-function`, `internal-unlisted`.
- F1-F3, N5: API.md section 3 and the `omega_gpu_session_recover` header comment no longer cite #518 or claim the RM
  client close frees every buffer; the chip effect of the close is stated as intended and not shown
  (`evidence/C5-ISOLATION/`), with the unmapped-buffer gap of the Linux reference. The command-list-only latch and
  recover's return values are documented.
- F4, F5: which calls take the session lock (the accessors do not; `omega_gpu_matmul_cta_budget` races with
  `omega_gpu_matmul_set_cta_budget`), and the hosted build's lock installs the Linux platform.
- F6, F7, N2, N3, N4: section 9.6 wording, the review-state line, `first_barrier_none`, the launch argument checks
  (`L` not NULL, `spin_us` bound, `timeout_ms` 0), and INTERNAL comments on four session declarations.

Not checked here: that the prose is true (review only); the fixes have not been reviewed again.
