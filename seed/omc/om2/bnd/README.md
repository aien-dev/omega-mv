# OM-2 expression boundary gate (#745 checkbox 2)

`spec/omegese-om2.md` section 2 (lines 59-62) keeps the OM-1 expression boundaries in both profiles. The cases come from the frozen L836 suite at `seed/qualification/om2/sec/s0s2/candidate.cases`: lines 173-257 and its C2 rows for `s2bnd`. They are copied verbatim, and the fixtures and expected outputs stay there. `seed/host/check.sh` requires both summaries exactly:

- `bnd.cases`: `OM2GATE PASS cases=47 pass=47 fail=0 invalid=0 not_run=0`. It covers:
  - missing operand (`short-*`, error 4);
  - tokens after the value (`left-*`: errors 5 and 11);
  - grouping (`group-*`, `bracket`, `brace`: errors 16 and 3);
  - `g0..g4`: grouped spellings that run to 7 and compile to identical bytes, and to the pinned OM-1 bytes (C2).
- `mutants.cases`: `OM2GATE FAIL cases=10 pass=2 fail=8 invalid=0 not_run=0`. Two mutants of `omc.om`:
  - `bnd-extra`: `eol` reports 11 instead of 5.
  - `bnd-missing`: `rdval` reports 16 instead of 4 for a missing operand.
  - Each mutant kills 4 rows, and the `g0` control passes.

Under the OM-1 compiler from before #750 (`acb3f15c`, sha256 734f0a39...), `bnd.cases` gives `OM2GATE FAIL cases=47 pass=26 fail=21`. All 21 failures are `s2-om2-*` rows: that compiler has no OM-2 profile. The OM-1 rows pass because OM-1 already had these checks, so no historical compiler lacks them. The mutants are the red run for the checks themselves.
