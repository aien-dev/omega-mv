# OM-2 errata (coordinator L835)

Clarifications to the frozen `spec/omegese-om2.md` (sha256 `245bb74e…0881`) that resolve conflicts
inside it. No erratum changes the meaning of a construct that a lane has already built on. Each
erratum names who raised it.

## E1 (AbiLane L837, 2026-10-25): which units are v2, and where identities live

§6 "Symbol versioning" and "Module boundary" conflicted with C2-style byte identity for v1 bindings.
Resolution:
- Every OM-2 unit (first non-note line `om2`) is bound ABI v2 (`omega2_<export>`), whatever its
  parameter kinds. This replaces "a unit with any unsized view or layout parameter".
- A v2 binding embeds the unit source sha256 (computed by the generator from the exact bytes), the omc
  pin (supplied by the harness, per C3) and profile `OM-2`, readable via `omega2_ident_<unit>()`.
- OM-1 units stay ABI v1 with byte-identical binding output and no embedded identity. Their identity
  (source sha256, omc pin, profile `OM-1`) is recorded by the harness, and `om/units.sh --check` refuses
  a stale OM-1 unit.
- An `out` parameter is passed as one pointer with no count, like a layout.

## E2 (QualPhaseB L836, 2026-10-25): sized arrays to unsized parameters

A sized array (length N) may be passed to an unsized `view T *` / `edit T *` parameter of a function
in either mode; `len` is N inside the callee. Writability rules are unchanged (a view never reaches an
edit). Unsized parameters are allowed on internal functions, not only on exported entries.

## E3 (ShaLane L838, 2026-10-25): context copies and the C API names

- A byte copy of a context between `init` and `final` is an equivalent context: all hashing state lives
  in the 104 layout bytes (no hidden state, no self-pointers). Receipt required: copy after k updates,
  continue both copies with the same data, equal digests. Callers relying on this:
  `src/fabric/fab_loopback.c:107`, `src/fabric/fabric.c:496`, `src/osh/host/osh_journal.c:81/104`.
- Digest overlapping data is used by `omega_program.c:777` and `tests/runtime/rx_sem_incremental.c:183`;
  §6 `out` semantics cover it (receipt required).
- C API names: the SHA unit exports `omega2_sha256_init` etc. The bridge-generated header maps the
  `sha256_*` names to them (generated, not handwritten). The freestanding nm allow-list
  (`Makefile:657`, `RX_CX_FS_ALLOWED`) changes from `sha256_*` to the `omega2_sha256_*` symbols. That
  one Makefile rule is changed by the SHA lane (L838) in the cutover commit.

## E4 (QualPhaseB L836, 2026-10-25): diagnostic record fields and the fmt input

- `col` is the 1-based byte column of A on its line.
- The span `A-B` is the offending lexer token; at end of input it is the empty span `len-len`.
- `found` is the hex of the span bytes, or `-` when the span is empty.
- `expect` is the token class the parser wanted next.
- `om2 fmt`: "the rest" is the bytes after the first LF; the output does not contain the `om2 fmt` line.

## E5 (ShaLane L838, 2026-10-25): scalar layout fields; cutover build rules

- In `field F T [COUNT] at OFF little|big`, an omitted COUNT is a scalar field (`get c F`,
  `put c F v`). A COUNT (1..4096) is an array field (`get c F i`), even when COUNT is 1. In the §5
  SHA example, `field count ulong 1 at 32 little` reads `field count ulong at 32 little` (scalar).
- Cutover: `src/omega_om_units.inc` is today included only by `src/sha256.c`, and BLAKE3 links through
  it. The generated bindings move to a generated `src/omega_om_units.c`, and every build rule that
  names `src/sha256.c` (Makefile, mk/*.mk, scripts) is switched to it in the cutover commit. The SHA
  lane (L838) is the single writer of exactly those build-rule lines in that commit.

## E6 (QualPhaseB L836, 2026-10-25): reserved names, diag fields, formatter edge cases

- **Q1:** declaring an OM-2 reserved word as a name inside an OM-2 unit (for example `fn rotr`) is
  error 16, as OM-1 does for `fn break`.
- **Q2, `expect` and spans:**
  - A lexically bad literal (error 6) or an out-of-range literal (error 8) reports `expect=number`.
  - A missing `end` at end of input reports `expect=end`, with `sev=incomplete`.
  - An unknown name reports `expect=name`.
  - A line that ends before its operand when more input follows reports the LF byte as the offending
    token (span covers the LF), with `sev=error`.
  - An incomplete call to a function whose signature is not yet read reports the callee name token
    as the span, with `sev=unknown`.
- **Q3:** line numbers always count every line of the whole input, the `om2 fmt` line included.
- **Q4, formatter:**
  - (f) Formatter mode applies iff the input's first line, at byte 0, is exactly `om2 fmt`. A line
    `om2 fmt` anywhere else is error 16.
  - (c, d) A note line is any line whose first token is `note`, indented or not. It is kept byte for
    byte, including its indentation and any trailing spaces. `text` literal bytes are kept byte for
    byte. These two rules take precedence over every other formatting rule.
  - (b) A whitespace-only line becomes an empty line.
  - (a) Blank lines at the end of the input are error 6, so the formatter never removes lines and the
    output always ends with exactly one LF after the last non-blank line.
  - (e) The spacing next to `(` and `)` is kept as written. Grouping is never respaced.
  - (g) CR is error 6 anywhere, including inside a `text` literal.
  - (h) The rest may be an OM-1 unit. The formatter is lexical, and compile(fmt(x)) == compile(x)
    applies whatever x's profile is.

## E7 (QualPhaseB L836, 2026-10-25): unroll and inline limits, site records

- `for i T K unroll`: K = 0 or K > 64 is error 8. A K that is not a number is error 16.
- The limit of 64 copies applies per unrolled loop. Nested unrolls multiply, and their total is bounded
  only by the 65536-word limit (error 14, `obligation=growth`).
- Inline depth: a call site in a function that is not being inlined is depth 1. A chain of 4 inlined
  calls is accepted; a 5th level is error 14 (`obligation=growth`).
- `break`/`continue` inside a rolled loop nested within an unrolled body is allowed; it leaves the
  rolled loop. Only a `break`/`continue` whose innermost loop is the unrolled one is error 9.
- `omc-site 1 line=N iter=P` records appear in emission order. P is the 0-based value of each enclosing
  unrolled loop's counter, from outer to inner and joined by `.` (for example `iter=3.15`), or `-`.
- OM-1 shifts accept a shift amount of a different type. OM-2 `rotr`/`rotl` require the same type;
  this stricter rule applies to OM-2 words only.

## E8 (QualPhaseB L836, 2026-10-25): codes and lines for §5/§6 refusals

- A `fresh` with N > 64 is error 14 (`obligation=init`).
- An abstract type used as an entry parameter or return, or as an array element type, is error 7.
- An operation on an abstract value that its `abstract` line does not list is error 7.
- A field access with the wrong arity: a missing index on an array field (any COUNT) is error 4; an
  extra index on a scalar field is error 5.
- Layout SIZE 0 or > 4096, and a `layout` after the first `fn`, are error 29 (`obligation=layout`).
- `part` anywhere other than as a call argument is error 9.
- Which line each error names:
  - error 27: the offending `get` or call line;
  - error 28: the `seal` line, or the `holds` line for a bad predicate;
  - error 29: the offending `field` line, or the `layout` line for a SIZE error.
- Confirmed as written:
  - `rotr` of a dup32 value with n = 64 traps 7;
  - `omc-check` records name the constructor's `ret seal` line;
  - for layout fields, a constant out-of-range index is error 21, a `put` through a view is error 20,
    and an array of the wrong size is error 7.

## E9 (QualPhaseB L836, 2026-10-25): rotate count of an abstract value; late layout line

- For `rotr`/`rotl` on an abstract value, `n` has the abstract type's representation type T (ulong
  for dup32), so a bare number takes T.
- Error 29 for a `layout` after the first `fn` names the `layout` line.

## E10 (QualPhaseB L836 D3, 2026-10-25): blank lines before `om2`

A unit is OM-2 iff its first line that is neither a `note` line nor blank (empty, or only spaces) is
exactly `om2` (or `om2 diag`). OM-1 refuses such a unit at the `om2` line whether or not blank lines
precede it, so C1 and C3 hold. The fmt mode rule (E6 Q4 f, the first line at byte 0) is unchanged.
