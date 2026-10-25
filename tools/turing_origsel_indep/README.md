<!-- SPDX-License-Identifier: Apache-2.0 -->
# origsel_indep: independent recomputer of the ORIGSEL v1.1 report

The external referee for Turing-profile-ORIGSEL-v1.1 (omega-mv #700). It is a second
implementation, written in C, of the report defined in `native/turing/REPORT-CONTRACT.md`. It
works from the written contracts and was not ported from the seeded .om evaluator (`origsel.om`).
The two reports must be byte-identical, except for the `h1` lines, which only the .om H mode
produces.

Sources it implements:

- `native/turing/REPORT-CONTRACT.md`: arms, orders, MLP, F1 rows, code lengths, D, bootstrap, verdicts, report format.
- `calibration/profiles/Turing-profile-ORIGSEL-v1.1.toml`: binding wherever the contract is silent.
- aien-train 692d8510 `docs/si19/ORIG1-CONTRACT.md` s.2-3 (record layout, features) and s.5 (allow-list).
- aien-train 692d8510 `docs/si19/ORIG1-CONTRACT-v2.md` s.2-3 (admission `oxadm2`, splits `oxhspl`).
- `calibration/docs/CODER_SPEC.md` s.8 (`ub`).

It runs on the host CPU only and uses no GPU, network or model.

## Build

```
make origsel_indep      # cc -O2 -std=c11 -ffp-contract=off -fno-fast-math -Wall -Wextra
make selftest           # ./origsel_indep --selftest
```

binary32 arithmetic uses C `float`. Each product and each sum is its own statement, and
`-ffp-contract=off` prevents fused multiply-add. That gives round-to-nearest-even with gradual
underflow, the default floating-point environment on AArch64 Linux. `ub` uses
`unsigned __int128` for its 128-bit products.

## Usage

```
origsel_indep REC ALLOW V1 IMG_M1 IMG_SH IMG_UN SELECT_OM OMEXP_OM KAPPA_OM > report
origsel_indep --selftest
origsel_indep --fnv FILE
```

- `REC`: the omexp-v1 record file. `ALLOW`: the allow-list text.
- `V1`: the v1 files concatenated, raw records. It may be empty (no burned shapes).
- `IMG_*`: the V2=1 mlp, shuffled and untrained images.
- `SELECT_OM`, `OMEXP_OM`: the unit sources at aien-train 692d8510. `KAPPA_OM`: `native/turing/origsel_kappa.om`.

Exit status:

- 0: report written.
- 2: contract refusal. stdout holds the single line `refused allow` or `refused admission <code>`.
- 1: usage error, I/O error, or an input outside the contract. That covers a malformed image, a
  V1 file that is not a whole number of records, and a missing unit function. The message goes
  to stderr and stdout stays empty.

`--fnv FILE` prints FNV-1a 64 over every byte of FILE as an unsigned decimal followed by a
newline, then exits 0. This is the hash the allow-list lines use (ORIG1-CONTRACT s.5), and the
glue uses it to write allow-list lines for derived audit files. A file it cannot read exits 1.

`--selftest` prints the following and exits 0 only when every check passes:

- `ub` for the golden values in REPORT-CONTRACT and CODER_SPEC s.8: 1, 32768, 43481, 46819, 13107, 13108 and 65536.
- The first five splitmix64 outputs from state 20261025.
- A product-then-sum check that fails if the compiler fused the multiply-add.

## Interpretation decisions

These are the places where a contract left room for more than one reading. The .om consultations
are listed in the next section.

1. **Allow-list.**
   - The file's FNV-1a 64 covers every byte.
   - Lines are split on `\n`. The first line whose leading decimal token equals the hash decides.
     If the token overflows u64, the line does not match.
   - That line is accepted only when a single space follows the number and the next token, ending
     at a space or the end of the line, is exactly `AUTHORIZED` or `SYNTHETIC`.
   - Anything else is `refused allow`: no matching line, `REVOKED`, or a malformed status.
   - The label is not checked. The .om parser classifies the status by its first letter and
     length, so the two parsers can only disagree on a malformed allow-list.
2. **Check order.**
   - The allow-list is checked first, then the whole-file size check `-1` (empty, not a multiple of
     256, or above 524288 bytes), then the per-record codes, then the `-11` disjointness check.
   - All input files are read, and must exist, before any check runs.
3. **-3 family.** The record is refused when word 2 > 5, as the v2 text says ("above 5"). That
   admits family word 5 even though v1 defines families only up to 4. `oxfmt2` confirms this reading.
4. **-14 pairing.**
   - Mask word 12 must be exactly 31.
   - For arms 0..4, ps must be non-zero and the verified word 24+a must equal **1**.
   - For arm 5, ps (word 21) and the verified flag (word 29) must both be 0.
   - "Arms 6, 7 absent" means ps words 22 and 23 are 0. The arm count is already forced to 6 by -3.
5. **-10 range.** The 2^40 check applies to ps of arms 0..4 only.
6. **-8 sequence.** "The previous record" means the record immediately before this one in the
   file, whatever its own code. It is not the previous admitted record.
7. **-15 duplicate.** A record is a duplicate when an earlier record with final code 0 has the same
   (core, pressure, M, N).
8. **-11 split conflict.**
   - The file is refused when two admitted records in different splits share a cell or share (M, N).
   - The split is a function of the cell, so this check cannot fire on any file. It is implemented anyway.
9. **Image acceptance.** The contract says only "SI19SLCK images (kind 2, 11560 bytes)". The checks
   are taken from `slverify` in select.om:
   - size 11560;
   - magic `SI19SLCK`;
   - u64 at offset 8 equal to 1, offset 16 (kind) equal to 2, offset 24 (H) equal to 32, offset 136 (nparams) equal to 705;
   - the last 8 bytes equal FNV-1a 64 of the bytes before them.

   The range and finiteness checks of `slverify` (`slrange`, `slfinite`) are **not**
   reimplemented. Any bit pattern is used as given. A failed check exits 1 (decision 2 above under
   exit status), not a report line, because the contract defines no image refusal line.
10. **MLP inputs.** z_j = (float)f_j / 16.0f. Every feature is a small integer, so this is exact.
    All 20 inputs take part in every hidden sum, including the zero one-hot inputs, so ±0 and NaN
    propagate exactly as the written loop does. a_h = acc when acc > 0, else +0.
11. **Fit, D and P1R filters.**
    - D is admitted HELD-OUT with pressure 0 and core 0 or 1. P1R uses the same filter with pressure 1.
    - Each record's (M, N) is compared with words 5 and 6 of every V1 record. `burned` counts
      removed **pressure-0** records only. P1R also excludes V1 shapes ("the same as D"), but that
      count is not reported.
    - D and P1R stay in file order.
12. **V1 cell.** A V1 record's cell is 16 fl(M) + fl(N), using fl(0) = 0. V1 records are not
    admitted or checked; only words 5 and 6 are read. A V1 file that is not a whole number of
    256-byte records exits 1.
13. **R neighbour.** Candidates are admitted TRAIN records with the same core class and the same
    pressure, in both pressures (the row records are pressure 0, P1R records are pressure 1).
    "Lowest record index" is the record's position in the file.
14. **rows= field.** It prints the F1 counts n_r, not Q_r. Rows are listed in rank order, and for
    M2 and A the four kappa contexts are listed in order 0..3, separated by `;`.
15. **Canonical bytes.**
    - A function starts at the line that begins with exactly `fn NAME ` (column 0, trailing space).
    - A line is dropped when it is empty (length 0) or its first four bytes are `note`.
    - A final line with no newline is counted without one; none of the pinned units has such a line.
    - A missing function exits 1.
16. **F = 0.** The bootstrap draws no RNG outputs. Every replicate sum is 0, so lb = ub = the
    P1-only term. The same holds for the (cell, core) bootstrap when F2 = 0.
17. **Verdict n.** n = |D|. n_min uses integer ceil(P1 x 10^6 / 2321950).
18. **Bootstrap statistics.** They are signed 64-bit, built from u64 block sums. `total` for `tcf`
    is the same over-D total as for `t`. Sort positions are 0-based 166 and 9833, which are ranks
    167 and 9834 in the profile.

## .om consultations

These sources were read only to settle the ambiguities above. No .om evaluator code (`origsel.om`)
was read.

- `omexp.om` (aien-train 692d8510), the list of `fn`/`global` header lines.
- `omexp.om` lines 482-679: `oxfmt2`, `oxpair2`, `oxval2`, `oxsame`, `oxdup2`, `oxrec2`,
  `oxconf`, `oxdisj`, `oxdec`, `oxtally`, `oxadm2`. They settled decisions 3-8.
- `omexp.om` lines 22-35 and 170-293:
  - `oxlg16` agrees with the contract's lg16 for v >= 1.
  - `oxfnv`.
  - `oxload`, the v1 loader; it was in the displayed range and is not used.
  - `oxdigit`, `oxline`, `oxstatus`, `oxallow`, for decision 1.
- `select.om`: the file-head `note` lines 1-20, the head of `sladmit`, and `slverify` (for decision 9).

## Qualification run (2026-10-25, host only)

- Fixture `/tmp/turx/run/fixb` (SYNTHETIC, 400 records) wrote `/tmp/turx/run/indep-fix.report`.
- v1 owner file (inspected development data, never D) with the `/tmp/turx/run/ownb` images wrote
  `/tmp/turx/run/indep-own.report`.
- Admission counts match the .om trainer's logs:
  - owner file: 583 / 330 / 108 / 145, refused 155 (`own.log`);
  - fixture: 318 / 206 / 58 / 54, refused 82 (`fix.log`).
- A throwaway diagnostic build, not part of this tool, compared top-1 choices with the trainer's
  evaluation output (`v2_eval_*.stdout`). Per split and core it summed the top-1 cost of M1, CSH,
  CUN, FR, S and R, the oracle cost, the reference cost and the M1 agreement count. Every value
  equalled the trainer's `eval part=1|2 core=0|1` lines for all three images, on both inputs.
- That check covers the trainer's evaluation path only. Agreement with `origsel.om` is still
  outstanding.
