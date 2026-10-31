# OM-2 hex literal fixtures (#745)

Coverage for `spec/omegese-om2.md` section 2 (hex literal, lines 53-58), implemented in
`seed/omc/omc.om` `tok_hex` by #750. Run by `seed/qualification/om2/gate.om`; `seed/host/check.sh`
requires both summaries below exactly.

- `hex.cases` (51 cases): `OM2GATE PASS cases=51 pass=51 fail=0 invalid=0 not_run=0`.
  - values: `pairs-*` (64 literals, eight per integer type: 0, 1, maximum, sign-bit or half value,
    four random), `floatlike-*` (`e`/`f` digits that the decimal path reads as float markers),
    `places-*` (const, global length, view/edit lengths, new, put, unroll, while bound, rotr/rotl,
    f64/f32 conversion, trap code), `fnmode-*` (function mode), the x86linux target (`target.txt+`);
    each runs to the Wolfram value and its decimal twin compiles to the same bytes (`same`).
  - refusals `ref/`: every out-of-range hex literal gets its decimal twin's code on the same line
    (8; 23 for a global past 16777216, as decimal); `-` before hex, 17 digits, `00x1`, `-0X1`, `0x1.8`,
    `0x-1` are error 6.
  - versioning `om1/`: an OM-1 unit, including IR mode, has no hex literal (error 6).
- `mutants.cases`: three `tok_hex` mutants of `omc.om` (`mutants/*.from` -> `*.to`), each killed;
  the decimal control passes under each: `OM2GATE FAIL cases=8 pass=3 fail=5 invalid=0 not_run=0`.
  - `hex-tbig`: no big flag (ulong >= 2^63 refused, signedness check lost);
  - `hex-nolimit`: the 16-digit limit dropped (17 digits silently truncate to 64 bits);
  - `hex-digit`: wrong value for a-f.
- `wolfram.txt`: the queries that produced every spelling and expected value.

Run from the repository root:

    seed/out/omc < seed/qualification/om2/gate.om > /tmp/gate && chmod +x /tmp/gate
    /tmp/gate seed/out/omc "$(sha256sum seed/out/omc | cut -d' ' -f1)" seed/omc/om2/hex/hex.cases /tmp/hexrun

Under the OM-1 compiler before #750 (`git show acb3f15c:seed/out/omc`, sha256 734f0a39...) the same
file gives `OM2GATE FAIL cases=51 pass=4 fail=47`: the 4 passes are the three OM-1 refusals and
`r8-const-dec` (that compiler refuses the const line before it reaches the `om2` line).
