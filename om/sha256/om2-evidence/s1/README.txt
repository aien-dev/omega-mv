S1 reduced-unit check (L838, 2026-10-25). Evidence class HOST (GB10 host CPU, AArch64 Linux), no GPU.
TEMPORARY: sha256-s1.om and harness.py exist only to exercise compiler slice S1 before S2-S4 land.
They are not the delivered unit and are removed when the delivered unit compiles.

Compiler: aien-dev/om2/compiler-s1 @ a5927895 (PR #750), omc sha256
  3134244727189883d317e6813b93d2f55b24f2ca5e236daf6d39ef48412f049f
  (copied from CompilerLane's worktree ~/workspace/om2-compiler/seed/out/omc; hash equals the
  coordinator's announced pin; not re-born here)
Unit: sha256-s1.om, sha256 2b7ecf75cc189488e7d278fbc8bedb763558f4b595ea2195d2f89a73af8ee843
  Kept from the delivered unit: om2 line, the 64 K and 8 IV hex lines (same lines), rotr-based
  Sigma0/Sigma1/sigma0/sigma1, the same schedule and round formulas.
  Reduced: rolled loops (no unroll, S2), state in a uint array (no layout, S3), zeroed w (no fresh,
  S3), program mode stdin -> hex digest (no unsized view/out/export, S4). Sigma functions are calls.
Output: sha-s1.bin, sha256 70e1a6e69ed75bdd9308ecc5e050227efa65cb5a2377ed1b734498a87040e442
  (ELF64 AArch64 program); a second compile is byte-identical.

Commands and results
  omc < sha256-s1.om > sha-s1.bin                         exit 0
  python3 harness.py ./sha-s1.bin                           vectors pass=158 fail=0 (vectors.out)
    harness: every row of fixtures/sha256-vectors.txt (sha256 b154d750…0c34; FIPS empty/abc/448/896/
    million-a, seq 0..130 and boundaries to 65536, rep, rnd up to 100003 bytes) on stdin; generators
    per fixtures/PROVENANCE.txt section 2. python3 3.12.3, oracle side only.
  mutant: rotr x 13 -> rotr x 12 in bsig0                  vectors pass=0 fail=158
  objdump -D -b binary -m aarch64 sha-s1.bin | grep ror     10 ROR (immediate) = the 10 rotr in the
    source, all uint with constant n (section 3 lowering; GNU objdump 2.42). ror.txt.

Findings that changed the delivered unit (commit after a46aa741)
  - error 12 (16 registers): a one-line big-endian load (4 gets, 3 ors), a one-line round body, a
    one-line Maj and a one-line hex step all exceeded the pool under S1. The delivered unit's load and
    round body are now built a term per line in the same shape that compiles here.
  - `write fd A off n` must be assigned (its long result); a bare write line is error 16.
  - The delivered unit under S1: "omc: line 114: error 16"; with `om2 diag`:
    omc-diag 1 code=16 sev=error line=114 col=25 span=3691-3696 expect=type found=6672657368
    obligation=- profile=OM-2   (found = "fresh"; the names pass reads signatures first, so the
    layout on line 23 is not reached).
