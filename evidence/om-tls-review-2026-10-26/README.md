# om/tls security review bundle, 2026-10-26 (imported from /home/drakestapleton/handoffs/omega-tls)

Prior-lane review reports and raw logs for om/tls at omega-mv main 705c0f01 (omc pin 189ada82),
retained here so an outside auditor can read what the inside review found and reproduce the runs.

- `security-review.md`, `security-rereview.md`: the two inside review passes and their fix status
- `tls-contract.md`, `PORTING.md`: the port contract and porting notes
- `handoff-evidence/`: raw unit logs (sha2, x25519, chacha, aesgcm, ec, rsa, x509), the interop table,
  `tls-run-20261026T1706Z.log` (full suite), `main-pin-189ada82/` and `review-fix/` logs
- `timing/`, `timing-v3/`: the constant-time measurement folders (each with its own `SHA256SUMS`)
- `SHA256SUMS`: `sha256sum` of every file in this directory and below except itself (verify with
  `sha256sum -c SHA256SUMS` from this directory). First taken when the files were copied; regenerated
  2026-10-27 to cover `timing/`, `timing-v3/` and this README, after dated correction notes (TLCSZ
  121696) were appended to `PORTING.md` and `security-review.md`. The earlier list is in git history.

Evidence level: the logs are MEASURED on the DGX Spark host (arm64, GB10); nothing here is a
hardware or third-party qualification. The outside-review fixes that followed are in the
`audit/tls-outside-review` branch (om/tls/THREAT_MODEL.md, om/tls/AUDIT_SCOPE.md).
