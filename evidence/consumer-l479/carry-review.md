# Carry review at the canonical baseline

Frozen original Omega heads: snapshots in the L479 handoff. CUDApp canonical
baseline is 349913f17c083996680c72b32f767b225005155d; consumer starts from L457
Omega 1ad2dc1af70f01680fd93cb41a7a94fd0951723f. This reuses the completed carry,
not the obsolete PR618 extraction. The exact CUDApp baseline stays an ancestor
of the security repair when the L478 commit is consumed.

The audit covers all 242 changed paths across PR614-618, including the 152
original extraction deletions. The GitHub `files` list is capped at100 for
PR618; local exact-head Git diffs supply its complete158 paths. All195 mapped
canonical files are separately hashed against their pinned working bytes.
The existing source-carry matrix and selected current-head audit from
CUDApp roadmap/program-20261022 are retained as provenance. A differing blob is
recorded as a deliberate divergence for review, never silently overwritten.

Reviewed baseline divergences in `carry-divergences.patch`:

- PR614: codegen keeps opt-in register limits, scheduled control words, LG2,
  and the tensor-core width bound. Attention retains the hd128 register bound,
  control-word digest pins and complete listing instruction count. FP8 retains
  quantizer, coalescing, deep-kernel, schedule and target-shape coverage. Other
  differences are MIT license lines. The original disassembler and its CLI
  remain byte-identical at the canonical baseline.
- PR615/616: scan reference differs only by a trailing blank line; tensor-core
  simulator differs by its MIT license line. Scan close-hook changes are the
  same as original d7e15f18 via earlier0bd79b15, despite different ancestry.
- PR617: shared engine rules retain CUDApp driver paths, configurable production
  target and standalone source closure. Command-list capacity remains4096,
  with versioned runtime0.2/platform1.3 contracts and full maximum-capacity
  fixtures. Linux memory reserve and full refusal diagnostics remain. Session
  headers retain the process spin-setting documentation. Other differences are
  license lines. Recovery, stale-list rejection, paired-wait barrier and
  multi-channel driver carry remain as qualified by L457. The L458 recovery
  contract contradiction is still an owner prerequisite, not resolved here.
- Omega Makefile differences combine all feature rules and the strict canonical
  consumer. PR618's lock/map/linker are superseded by L457's broader195-path
  extraction plus the L479 hardening. README changes preserve the current
  extraction description, and `.gitignore` covers the extracted links.

The original PRs and canonical owner remain unchanged. Source-level carry does
not establish hardware behavior, model parity, timing, recovery qualification,
or official desk admission.
