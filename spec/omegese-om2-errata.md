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
