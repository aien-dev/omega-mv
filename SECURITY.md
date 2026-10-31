# Security policy (omega-mv)

This policy takes precedence over the aien-dev organisation policy
(https://github.com/aien-dev/.github/blob/main/SECURITY.md) for this repository.

## Reporting a vulnerability

Report privately. Do not open a public issue, pull request or discussion about it.

- **GitHub (preferred):** the repository's **Security** tab, **Report a vulnerability**
  (private vulnerability reporting is enabled). Only maintainers can see the advisory.
- **Email:** aien@aienos.com

Include the commit, the file and line if known, steps or an input that reproduces it, and the impact
you expect. A reproducer (a byte string, a certificate, a recorded TLS flight) is the most useful thing
you can send. Do not send secrets.

## What we commit to

| step | target |
|---|---|
| acknowledge the report | 3 working days |
| first assessment (confirmed / not a vulnerability / need more) | 10 working days |
| fix or mitigation on `main` for a confirmed issue | 90 days from the report |
| public advisory, with a CVE requested through GitHub when the issue qualifies | when the fix lands, or at 90 days |

We follow coordinated disclosure: please give us the 90 days, and we will credit you in the advisory
unless you ask us not to. If an issue is being exploited, we publish sooner.

## Scope

In scope: everything on `main`. Highest priority:

- `om/tls/` (TLS 1.3 client, X.509 path validation, OCSP/CRL revocation, SHA-2, SHA-3, HKDF,
  HMAC_DRBG, X25519, ML-KEM-768 (X25519MLKEM768), ChaCha20-Poly1305, AES-GCM, AES-GCM-SIV,
  ECDSA/RSA verification) and `seed/net/https.om`: memory safety, certificate validation
  bypass, handshake state-machine confusion, key or plaintext disclosure, timing leaks on secret data,
  and parser crashes or hangs on network input.
- The compiler (`seed/out/omc`, `seed/omc/omc.om`) when it emits code that breaks a guarantee the
  language documents (bounds checks, ownership, loop termination).

`om/tls/THREAT_MODEL.md` and `om/tls/AUDIT_SCOPE.md` describe the attacker model and what the client
deliberately does not implement (server-side TLS, client certificates, session resumption, 0-RTT).
Revocation is checked only when the caller opts in (`tls_init` flag 8, `rev.om`: stapled OCSP,
must-staple, caller-supplied CRL); without it a revoked chain verifies (`om/tls/README.md`, "Policy").

## Status of the code

Pre-alpha research software. Only `main` is supported. The TLS client has been tested against
public test vectors (Wycheproof, RFC test vectors, and the NIST ACVP-Server sample vectors run locally
by `om/tls/test/acvp/run.sh`, which is not a CAVP certificate); it has **not** had a paid third-party
audit and is **not** NIST-validated. The commands that re-run every suite, and their expected results,
are listed under "Tests" in `om/tls/README.md`.

## Not covered

Reports about the live services of other repositories go to those repositories. Denial of service
that needs more traffic than the client itself generates is out of scope.
