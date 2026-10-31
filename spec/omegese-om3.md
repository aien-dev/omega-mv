# Omegese profile OM-3: additions after the OM-2 freeze

Status: PROPOSED (draft omega-mv#1008, 2026-10-31). Not frozen, not reviewed by the #599
language-evolution coordinator. `spec/omegese-om2.md` is frozen and says "A change after freeze needs
`omegese-om3.md`"; this file is where such changes are written. It is additive to
`spec/omegese-om2.md`, its errata and `docs/one_layer/OMEGESE.md`, which stay normative for everything
not stated here.

## 0. Opt-in and compatibility

- Each addition below names the units it applies to. Section 1 applies to units whose first line that
  is not a `note` is `om2`; this draft adds no `om3` line. Whether later additions need their own
  opt-in line is open.
- **C1 and C2 of OM-2 still hold.** Every OM-1 unit keeps its meaning, error code and line, and the
  frozen C2 manifest compiles byte-identically. An addition may only turn an OM-2 refusal into an
  accepted unit; it never changes the output of a unit that compiles today.
- **C3 still holds.** The compiler source stays inside om4's language and om4's function limit.

## 1. Host words in function mode (#1008)

- An OM-2 function unit (first line `om2`, no `fn main`) may use the host words `fopen`, `fclose`,
  `fstat`, `stat`, `lstat`, `getuid`, `geteuid`, `getgid` and `getegid` (OMEGESE.md section 10) on
  AArch64, with their program-mode meaning and machine code.
- Every other host word in function mode, any host word in an OM-1 function unit, and any host word for
  another target is error 9.
- These words do not form a sandbox: a caller that embeds such a unit grants it the file and identity
  view of its own process, so the omega-bridge refuses a unit that names one unless the caller opts in.
- Gate: `seed/omc/om2/fnhost/fnhost.cases` with the mutants in `seed/omc/om2/fnhost/mutants.cases`,
  run by `seed/host/check.sh`.
