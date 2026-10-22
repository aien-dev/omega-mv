# Exploration diagnostic row schema

## Version 2

The live host adapter emits `stop_reason` beside the numeric `stop` code and
names the reachability count `unexplored_better`. `stop_reason` maps 1 to
`frontier_empty`, 2 to `budget_exhausted`, and 3 to `invalid_state`; other
values are `active_or_unknown`.

`unexplored_better` counts reachable nodes that were not visited and whose
declared fixture cost is below the final incumbent cost. It is a finite-fixture
accounting quantity. It does not establish that the policy rejected those
nodes. In particular, a budget stop can leave beneficial frontier nodes
unexplored. The driver does not retain a per-node rejection reason, so a
policy-pruned total cannot be reconstructed from these rows. Capacity
abandonment remains separately reported in `abandoned`; it is not included in
`unexplored_better` as a causal classification.

## Compatibility with sealed version 1

The original frozen 300 rows remain byte-for-byte unchanged and retain their
original `false_pruned` header. That column's actual formula is exactly the v2
`unexplored_better` formula. `tests/research/ex600/derive_diagnostics.py`
accepts only that exact header and 300 rows, then writes a separate v2 derived
table with an explicit stop reason and the corrected metric name. It does not
rerun policy, alter scientific values, or rewrite sealed evidence. Stop codes,
best values, spending, arithmetic, six aggregate totals, and the original
scientific verdict are preserved. Future producers write v2 directly.

The number of explicit policy/capacity-prune events remains unavailable from
the recorded campaign state. Do not infer it from the v2 reachability count.
