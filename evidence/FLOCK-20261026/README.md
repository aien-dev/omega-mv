# flock host word (keyword id 197): evidence, 2026-10-26

Source: branch `feat/host-flock` off omega-mv main `19f014f2`. Host: Linux 7.0.0-1019-nvidia aarch64 (DGX Spark), every step run at `nice -n 19 ionice -c3`.

The change: `flock fd op` (Linux `flock`, ARM64 syscall 32 per `asm-generic/unistd.h` line 101) in `seed/omc/omc.om` (kw, hargc, hsysno, hostval, value dispatch), `seed/omc/genkw.awk`, fixture `seed/host/tests/flock.om`, 5 negative controls in `seed/host/negative.sh`, and an OMEGESE §10 row. OmegaNext reserved id 197. This PR deliberately leaves the pinned `seed/out/omc` and its downstream pins untouched; OmegaNext runs the reseed for its batch.

| Log | Command | Result |
|---|---|---|
| `seed-rebuild.log` | `sh seed/omega-from-seed.sh` | RC=0, omc sha256 `11e51adf183d882ad3f06d135f68e0b656ac7bfb7612a83d8cfb7299ed34eff7` (local build, not committed) |
| `host-check.log` | `sh seed/host/check.sh` | `PASS native flock …`, `PASS 46 total host negative controls`, `PASS 62 original fixtures retain identical machine code`, final PASS; gate-ir skipped inside check.sh only because the C oracles had not been built yet |
| `gate-ir.log` | `sh seed/omc/gate-ir.sh` (after `make -j2 build/omegese/omegese build/omega_emit/omega_emit build/checker2/checker2`) | RC=0 |
| `gate5.log` | `sh seed/omc/gate5.sh` | All referee/mutant checks PASS (fixpoint, om4 = omc on 147 inputs, gate4 with stage2, mutants A/B/C caught, every error 1..23). The single FAIL: `size-history.tsv has no row for this omc`. That row is hand-kept, belongs to the reseed, and is left for OmegaNext. gate5's rewrites of `evidence/SEED-0/omc/*` were not committed. |

Negative controls run outside the gates:
- A mutant omc that maps id 197 to syscall 210 (`shutdown`) instead of 32 builds the fixture, and the fixture exits 2, not 0.
- The pinned main compiler `befb8199…` refuses the fixture with `line 11: error 3` (flock is not a word there).
- The rebuilt omc compiles `omc.om` to its own bytes (fixpoint).
