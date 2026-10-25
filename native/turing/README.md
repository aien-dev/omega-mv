<!-- SPDX-License-Identifier: Apache-2.0 -->
# native/turing: seeded .om Turing evaluation for the ORIG selector (omega-mv #700)

Profile: `calibration/profiles/Turing-profile-ORIGSEL-v1.1.toml`. Its digest is in the `.sha256`
sidecar. `Turing-profile-ORIGSEL-v1.0.toml` is kept unchanged as history: it was superseded before
any data existed (the defects are listed in v1.1 `predecessor_profile`) and is never scored.

| file | role |
|---|---|
| `origsel.om`, `origselmain.om` | the evaluator (handwritten seeded .om), exactly as `REPORT-CONTRACT.md` |
| `origsel_kappa.om` | `tkappa`, the context map of candidates M2 and A, charged through the profile's TG1 code |
| `REPORT-CONTRACT.md` | arms, orders, rows, code lengths, D, bootstrap, verdicts and the report format, shared by both implementations |
| `origsel.pin` | compiler, pinned units (same as `native/orig4/orig4.pin` at 85641e1e), turing-coder commit, collection harnesses C2 and C3 |
| `../../tools/turing_origsel_indep/` | independent recomputer (C, disclosed external referee, written separately from the contract) |
| `../../tests/turing/origsel-test.om` | in-process tests |
| `../../tests/turing/origsel-run.sh` | POSIX glue (disclosed): sequencing, digests and byte comparisons only |

Commit E (profile `evaluator_freeze`) holds all of the above plus the qualification receipts in
`evidence/TURING-ORIGSEL/qualify/`. E is pushed before the collection that supplies D starts.

```sh
# Qualification (outcome-independent inputs; the v1 owner file only as an inspected dry run):
ORIGSEL_TRAIN_GIT=<aien-train clone> ORIGSEL_BRIDGE_GIT=<omega-bridge clone> \
ORIGSEL_OWNER_RECORDS=<v1 owner file> nice -n 19 sh tests/turing/origsel-run.sh
# Measurement, once, after the collection exists (IMAGES optional: else the pinned V2=1 trainer runs):
MODE=measure RECORDS=<collected file> ALLOW=<allow list> [IMAGES=<V2=1 build dir>] \
V1="<v1 owner file> <v1 sealed file>" OUT=<outside the tree> nice -n 19 sh tests/turing/origsel-run.sh
```

Status words: QUALIFIED_IMPROVEMENT, NO_IMPROVEMENT(COST_BOUND), NO_IMPROVEMENT,
INCONCLUSIVE(UNDERPOWERED), INCONCLUSIVE, INVALID, BLOCKED. Host-only: GB10 NOT_RUN, energy NOT_MEASURED.
