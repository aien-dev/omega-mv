# native/orig4: ORIG-4 shadow advice (omega-mv#731)

Default-off shadow advisor for the matvec arm chooser; specification in
`spec/shadow-advice-v0.md`. It consumes the `omexp-v1` records written by the
default-off tap of `tests/runtime/rx_empirical_optimizer.c` and scores the admissible
arms with the ORIG-2 candidate scorer. It changes no decision: output is `SHADOW`,
`ADVICE`, `FALLBACK` and `COST` lines on fd 1, nothing is read back.

| file | role |
|---|---|
| `shadow.om` | the advisor unit (admission, per-record advice `hvscore`, cost sums, modes S/G/H); handwritten .om |
| `shadowmain.om` | `fn main`: mode dispatch, the last file of the program unit |
| `orig4.pin` | the compiler hash and every pinned source (repo, commit, path, sha256) and candidate image hash |
| `candidates/` | the immutable ORIG-2 candidates (`SHA256SUMS`), byte-identical to aien-train `evidence/SI19-ORIG2/candidates` |

Unit order (concatenation, no import): add32 mul32 metadata affine fixture world omexp
omexpfix select (pinned) + `shadow.om` + caller (`shadowmain.om` or
`tests/orig4/shadow-test.om`). Compiler: this repository's `seed/out/omc`.

Run: `sh tests/orig4/run.sh` (gate `tests/gates/omega-orig4-shadow.gate`, step
`tests/gates/run.sh orig4-shadow`). Real records: `ORIG4_RECORDS=<file>
ORIG4_ALLOW=<allow-list> OUT=<outside the tree> sh tests/orig4/run.sh`; the owner
dataset stays local, only its hash and the receipts are public.
