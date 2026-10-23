# Mutant corpus dispatch correction

All six positive qualification phases passed. The first sanitizer fault corpus then exceeded its existing180s aggregate bound; both compilations succeeded, but no complete result existed. The outer gate failed2 and the old helper obscured the timeout with an ENOENT while trying to read absent output. This was NOT a killed fault. Original logs/output are retained.

The parent now dispatches the identical504-case focused corpus in four disjoint shards for each control, keeps the180s aggregate and20s per-command limits, and verifies every original identity/input/actual Bash observation exactly once. Ordinary failed assertions remain visible for the exact expected mutant set; abnormal/incomplete runs never produce a valid collected result. Case generators, comparisons and expected failure identities are unchanged after normalizing only dispatch/count/metadata lines. Runtime source is unchanged.

Normal and known-bad sanitizer pilots must actually finish, then all remaining qualification/fault gates and independent review remain required. No timeout is relabeled PASS; no gate or warning is waived.

Actual sanitizer pilots completed within the unchanged180s budget: normal504/504PASS; known-bad compiled no-restore504observations with112actual failures, normal exit1 and complete identity coverage. Remaining full controls/regressions still pending.
