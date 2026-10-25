<!-- SPDX-License-Identifier: Apache-2.0 -->
# native/turing: seeded .om Turing evaluation for the ORIG selector (omega-mv #700)

Profile: `calibration/profiles/Turing-profile-ORIGSEL-v1.0.toml` (digest in the `.sha256`
sidecar). Frozen before the ORIG v2 collection C2 exists.

- `origsel_kappa.om`: the bounded reusable abstraction `kappa(s)` of candidates M2 and A. It is
  candidate code, so its exact byte length is charged in L(M|G); it carries no comment lines on
  purpose (every byte is paid for). License: Apache-2.0, as this directory.
- The evaluator units (`origsel.om`, `origselmain.om`) and tests land after this freeze and are
  qualified on fixtures before any HELD-OUT outcome of C2 is read (profile
  `verification_requirements`).

Status words: QUALIFIED_IMPROVEMENT, NO_IMPROVEMENT, INCONCLUSIVE, INVALID, BLOCKED. Host-only;
GB10 NOT_RUN; energy NOT_MEASURED.
