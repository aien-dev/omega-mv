<!-- SPDX-License-Identifier: Apache-2.0 -->
# native/turing: seeded .om Turing evaluation for the ORIG selector (omega-mv #700)

Profile: `calibration/profiles/Turing-profile-ORIGSEL-v1.1.toml` (digest in the `.sha256`
sidecar). `Turing-profile-ORIGSEL-v1.0.toml` is kept unchanged as history: it was superseded
before any data existed (defects listed in v1.1 `predecessor_profile`) and is never scored.

- `origsel_kappa.om`: `tkappa`, the context map of candidates M2 and A (charged through the
  profile's TG1 code, not by source bytes). License: Apache-2.0, as this directory.
- The evaluator (`origsel.om`, `origselmain.om`), the independent recomputer, the glue and the
  qualification receipts form commit E, which must be pushed before the collection that supplies D
  starts (profile `evaluator_freeze`, `d_collection_rule`).

Status words: QUALIFIED_IMPROVEMENT, NO_IMPROVEMENT(COST_BOUND), INCONCLUSIVE(UNDERPOWERED),
INVALID, BLOCKED. Host-only; GB10 NOT_RUN; energy NOT_MEASURED.
