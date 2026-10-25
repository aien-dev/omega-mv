# Combined compiler with the branch correction

Functional source: `078455c9c1862b9145ef892dfba68676204a38da`.
Compiler SHA256: `9f48f66a9ee9f6a166df615a7ab9c48bf5b06ac2ba544ac51e52e9a3d290a6c4`.

The existing security owner's correction and native qualification sources were
imported exactly from immutable commit7a1692908d86c69be91191ac227ce465d4ee8b1d.
The owner worktree was not changed. This source adds the checked signed branch
displacement helper and routes both conditional branch paths through it. It
does not add another security implementation or claim the larger security task
complete. At intake the source was locally committed; it was not a merged-main
feature. The exact combined source is now reviewable through this PR.

The original seed chain rebuilt the combined compiler with identical three-stage
output. The complete host qualification passes, including all70 original
fixtures and the device/mapping tests. The imported native gate669 passes on
this actual combined compiler: exact signed extrema, adjacent overflow and
alignment refusals, real backward array fill, real forward programs with an
independent decoder, oversized source refusals, a compiled range-bypass control
and malformed decoder-input controls. The malformed output is never executed.

All ten mapping/device compiler controls were rebuilt from this combined source
and detected by the tests. The generation-ceiling control also passes. The first
development attempt to transfer the mutation failed its exact-once source-match
check before building any variant; adding unchanged context made each edit
unambiguous. That refusal is not counted as a behavioral test pass.

Seven CUDApp executables (service, its test, four pure suites and the real-file
probe) rebuilt byte-identically with this compiler. `experiment-reproduction.json`
binds those bytes to their previously tested executables. The66-shard result is
the earlier actual read-only run, not a new inference or GPU trial.

Reproduction: `quiet-build sh seed/host/check.sh`, then compile and run
`seed/qualification/security673/gate669.om` as described in its pinned README.
The native gate requires a fresh scratch directory and refuses to overwrite one.
Raw gate outputs and host log are retained here; source/binary identities and
compiled-control results are in receipt.json. Final independent source review,
native GPU/model attachment and full experiment remain open. Hardware NOT_RUN.
