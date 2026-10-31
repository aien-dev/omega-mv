# om/tls security review bundle, 2026-10-26 (imported from /home/drakestapleton/handoffs/omega-tls)

Prior-lane review reports and raw logs for om/tls at omega-mv main 705c0f01 (omc pin 189ada82),
retained here so an outside auditor can read what the inside review found and reproduce the runs.

- `security-review.md`, `security-rereview.md`: the two inside review passes and their fix status
- `tls-contract.md`, `PORTING.md`: the port contract and porting notes
- `handoff-evidence/`: raw unit logs (sha2, x25519, chacha, aesgcm, ec, rsa, x509), the interop table,
  `tls-run-20261026T1706Z.log` (full suite), `main-pin-189ada82/` and `review-fix/` logs
- `SHA256SUMS`: `sha256sum` of every file above, taken when they were copied (verify with
  `sha256sum -c SHA256SUMS` from this directory)

Evidence level: the logs are MEASURED on the DGX Spark host (arm64, GB10); nothing here is a
hardware or third-party qualification. The outside-review fixes that followed are in the
`audit/tls-outside-review` branch (om/tls/THREAT_MODEL.md, om/tls/AUDIT_SCOPE.md).
