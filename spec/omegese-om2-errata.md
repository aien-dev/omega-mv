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
