# SC1-G-v3 replay after merging main 62d1d0d66 (2026-10-30)

`README.md` and `clean-replay.json` require compiler
`734f0a39319fbfa930bcc62cb30dbf06d47d78e126372b74f5baf478e6126b61`, which is what
`sh seed/omega-from-seed.sh` produces at this branch's original base `00eb0e7106d2`.
After main was merged, the same bootstrap (and the committed `seed/out/omc`) produce
`62775d2a391cfef7cde64e387655caa9809a267093112910ec76bfa568f3712b`. Those files are
left unchanged; this note records what each compiler does with the README "Exact replay"
sequence (Linux aarch64 host, scratch directory outside the repository, no GPU).

| Step | 734f0a39 (pinned) | 62775d2a (current main) |
|---|---|---|
| 10 builds: source and executable SHA256 equal to `clean-replay.json` | 10/10 | 10/10 |
| `solve0` corpus | exit0, `0af558d8…e855`, 95140 bytes | identical |
| `check0` on positive corpus | exit0, both PASS lines | identical |
| control corpora SHA256 equal to `clean-replay.json` | 8/8 | 8/8 |
| `check0` on candidates 1–6 | exits 71/72/73/74/75/76 | identical |
| `check8` on positive corpus | exit78 | identical |
| `solve0 7`, `check0 7`, `roundtrip` | exit0 each | exit0 each |

Every executable the current compiler builds is byte-identical to the one recorded in
`clean-replay.json`, so the recorded results and native-v3 evidence carry over unchanged.
To replay with the current compiler, accept `62775d2a…712b` at the README's
"REQUIRED compiler hash" step. This is a replay, not a new qualification; every limit in
`README.md` still holds. Timing samples were not compared.
