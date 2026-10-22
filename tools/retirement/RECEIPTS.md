# Retirement receipt consistency checks (schema 1)

`policy.py` verifies consistency of recorded evidence. It does not attest that an
operator performed a real production cutover. No signatures or signing keys are
required. Hashes bind file contents; scope labels alone cannot promote isolated
work to production. Production retirement still requires every existing count,
coverage, legacy-presence, closure, cutover, and seven-stage gate check.

Every PASS or NOT_APPLICABLE gate references a readable JSON object using
`gate_evidence[stage] = {"path": "evidence/seed-retirement/...json",
"sha256": "<64 lowercase hex digits>", "scope": "isolated|production"}`.
Paths must be repository-relative, without parent traversal, and resolve inside
`evidence/seed-retirement/`. Escaping symlinks, missing files, hash changes,
malformed JSON, duplicate keys, and non-JSON numeric constants are rejected.

## Production receipt

A distinct receipt is required for each stage. A receipt for one stage cannot be
reused for another stage, even with a correct hash. The mandatory shape is:

```json
{
  "schema": 1,
  "receipt_type": "omega.retirement.stage",
  "stage": "executable_equivalence",
  "scope": "production",
  "status": "PASS",
  "production_cutover": true,
  "identity": {
    "artifact_id": "<exact manifest artifact id>",
    "source_commit": "<40 lowercase hex digits: original source revision>",
    "source_sha256": "<original source hash>",
    "replacement_path": "<repository-relative Omega source path>",
    "replacement_source_sha256": "<replacement Omega source hash>",
    "native_sha256": "<qualified candidate executable hash>",
    "seed_sha256": "<seed compiler hash>"
  },
  "details": {"cases_run": 77, "cases_passed": 77}
}
```

All identity fields must exactly match the manifest row, with `artifact_id` from
`id` and `native_sha256` from `candidate.sha256`. All hashes are 64 lowercase hex
digits. `replacement_source_sha256` must also match the actual replacement file
in this checkout; the source must resolve inside the repository. The original
Rust `source_sha256` is separate from the replacement Omega source hash. Receipt
validation binds the binary to the qualified candidate hash recorded in the
manifest. It does not read or authenticate an installed production executable.

Only receipt type `omega.retirement.stage` with integer schema 1 is supported for
production. Missing/unknown types, mismatched status/stage/scope/identity, and a
cutover value other than boolean true fail closed. These rules also apply to a
production-scoped PASS gate on a row that is not yet retired.

Mandatory PASS `details` by stage:

| Stage | Required content |
|---|---|
| executable_equivalence | Positive integer `cases_run`, integer `cases_passed` equal to it |
| api_abi_data | `api_compatible`, `abi_compatible`, `data_compatible`: boolean true |
| isolated_candidate | `candidate_smoke`: boolean true |
| coordinator_model_train | `coordinator`, `model`, `train`: boolean true |
| dependency_closure | `status`: `PRODUCTION_VERIFIED`; `unresolved`: empty array |
| transactional_swap_rollback | `swap`, `rollback`: boolean true |
| main_archive_smoke | `archived`, `main_smoke`: boolean true |

A NOT_APPLICABLE gate requires the same production envelope and identity, status
`NOT_APPLICABLE`, and a nonempty string `reason`. A PASS receipt cannot stand in
for it. Production retirement flags must be explicit booleans.

## Existing isolated evidence

The two existing untyped isolated formats remain supported only with an
`isolated` reference on a non-retired row. Both must have status PASS and exact
native/replacement-source identity. The equivalence summary must report positive
executed/written case counts that agree, a positive original corpus, and no
production/scope fields. It supports only `executable_equivalence`.

The deployment format must explicitly say `scope="isolated only; production
untouched"`, boolean `production_cutover=false`, and match candidate/seed hashes,
with PASS closure and nonempty deployment cases. It supports only
`isolated_candidate`, `dependency_closure`, and `transactional_swap_rollback`.
Neither isolated format can satisfy a production reference, regardless of the
reference's hash or relabeled scope.

`test_receipts.py` creates TEST-ONLY production-consistent receipts in a temporary
directory and removes them when done. They are never published as production
proof. Actual production retirements remain zero. Run both `test_policy.py` and
`test_receipts.py`, or the complete `verify.sh`, for positive and negative checks.
