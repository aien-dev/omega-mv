# Security policy

## Reporting a vulnerability

Report privately. Do not open a public issue, pull request or discussion about it.

Use GitHub private vulnerability reporting. Open this repository's Security tab and choose
"Report a vulnerability":

https://github.com/aien-dev/omega-mv/security/advisories/new

That creates a private advisory that only the maintainers can see.

Include the commit or release you tested, the component (path), steps to reproduce, and what an
attacker gains. A reproducer (a source file, a byte string, a recorded TLS flight) is the most
useful thing you can send. If the report involves a secret, say where it was exposed and do not
include the secret itself. Do not post proof-of-concept payloads publicly.

## Response targets

This project has one maintainer; the targets below are best effort and stated honestly.

- Acknowledgement: within 3 working days.
- Triage and severity: within 7 days of acknowledgement.
- Fix or mitigation for confirmed issues: aim for 30 days for high and critical, 90 days
  otherwise, with a status update at least every 14 days until closed.
- Disclosure: coordinated. An advisory is published with credit (unless you ask otherwise)
  once the fix is on the default branch. Default embargo 90 days, negotiable.

## Scope

This repository holds the Omega language, its seeded compiler and the Omega GPU engine the AIEN
engine pins in aien-dev/aien-sovereign-core-mv `omega.lock`.

In scope:

- The seed chain and the seeded compiler: `seed/rung0` to `seed/rung4`, `seed/omc/omc.om`,
  the committed `seed/out/omc` and its sha256, and miscompilation that breaks a guarantee the
  language documents (bounds checks, contracts, loop termination).
- Seeded libraries that parse untrusted input (`seed/json`, `seed/net`, `seed/wasm`), and the
  TLS 1.3 client in `om/tls/` with `seed/net/https.om`: memory safety, certificate validation
  bypass, handshake state confusion, key or plaintext disclosure, crashes or hangs on network
  input.
- The C runtime and compiler still on `main` (`src/`), including memory safety in parsers and
  the reaction runtime's capability and receipt paths.
- `om/` units and the generated `src/omega_om_units.*` binding.
- Receipt integrity: content addressing, dirty-tree refusal, and the `evidence/` additions-only
  rule.
- `.github/workflows/`, `tests/gates/run.sh` and `.aien/` (trust anchor `.aien/verify.pub`).

Out of scope: the NVIDIA driver; the Spark test judge (`aien-test`, report to
aien-dev/aien-sovereign-core-mv); the pinned sibling repositories named in `physics.lock`,
`argus.lock` and `aienos.lock`; findings that require editing existing files under `evidence/`,
which are historical records.

## Supported versions

Only the default branch `main`. There is no release, and an Alpha plan or an open pull request is
not a qualified release (README). Fixes land on the default branch only.

## Status

Research grade. The migration from C to seeded `.om` is unfinished. The TLS client has been
tested against public test vectors; it has not had a third-party audit and is not
NIST-validated. There is no certification and no bug bounty.
