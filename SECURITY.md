# Security policy

## Reporting a vulnerability

Report privately. Do not open a public issue, pull request or discussion about it.

1. GitHub: open this repository's Security tab and choose "Report a vulnerability". This
   creates a private advisory that only the maintainers can see. If that option is not shown,
   private vulnerability reporting has not been enabled for this repository yet; use email.
2. Email: drake@aienos.com with the subject `[omega-mv] security report`.

Include the commit or release you tested, the component (path), steps to reproduce, and what an
attacker gains. If the report involves a secret, say where it was exposed and do not send the
secret itself. Encrypted mail is not required; do not post proof-of-concept payloads publicly.

## Response targets

This project has one maintainer; the targets below are best effort and stated honestly.

- Acknowledgement: within 3 working days.
- Triage and severity: within 7 days of acknowledgement.
- Fix or mitigation for confirmed issues: aim for 30 days for high and critical, 90 days
  otherwise, with a status update at least every 14 days until closed.
- Disclosure: coordinated. An advisory is published with credit (unless you ask otherwise)
  once the fix is on the default branch. Default embargo 90 days, negotiable.

## Scope

In scope:

- The seed chain and the seeded compiler: `seed/rung0` to `seed/rung4`, `seed/omc/omc.om`,
  the committed `seed/out/omc` and its sha256, miscompilation that breaks a stated check.
- Seeded libraries that parse untrusted input (`seed/json`, `seed/net`, `seed/wasm`).
- The C runtime and compiler still on `main` (`src/`), including memory safety in parsers and
  the reaction runtime's capability and receipt paths.
- `om/` units and the generated `src/omega_om_units.*` binding.
- Receipt integrity: content addressing, dirty-tree refusal, and the `evidence/` additions-only
  rule.
- `.github/workflows/`, `tests/gates/run.sh` and `.aien/` (trust anchor `.aien/verify.pub`).

Out of scope: the Spark test judge (`aien-test`, report to aien-sovereign-core-mv); the pinned
sibling repositories named in `physics.lock`, `argus.lock` and `aienos.lock`; findings that
require editing existing files under `evidence/`, which are historical records.

## Supported versions

Only the default branch `main`; there is no release, and an Alpha plan or open pull request is
not a qualified release (README). Fixes land on the default branch only.

## Status

Research grade; migration from C to seeded `.om` is unfinished. There is no certification and no
bug bounty. See AUDIT_SCOPE.md for what to review and how to build and test it.
