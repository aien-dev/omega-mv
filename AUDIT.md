# Audit before going public (L396, 2026-10-22)

Scope: every commit and file version kept from omega `src/osh`, `tests/osh`, `mk/osh.mk` and
`mk/osh-host.mk` (history kept with git filter-branch at omega a8ffd2b9), plus the commits made here.

| Check | Result |
|---|---|
| Authors | Drake Stapleton and his bot accounts only: `Drake Stapleton <drake@aienos.com>` 39 commits, `aien-dev <drake@aienos.com>` 10, `aien-dev <aien.atlas@proton.me>` 5. |
| Secrets | omega `tools/modules/secrets_scan.sh` over every blob reachable from any ref (413): 0 findings. |
| Third-party code | None. No file carries another project's copyright or licence header; the only licence text is `LICENSE` (AGPL-3.0). |
| Binaries | None. `tests/osh/vectors/v013.in` is a 9-byte lexer test vector with a NUL byte on purpose (`echo a\0b`). |
