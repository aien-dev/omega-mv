# omega_ref: a second, independent reading of the Omega systems words

## What this is, in plain words

The seed chain (`seed/`) builds the Omega compiler from a few hundred hand-written bytes. Its fourth
rung, om4, turns Omegese programs into machine code. To trust om4 we need something to compare it
with that was **not** built the same way. If the only judge is a copy of the same design, a mistake in
that design can pass every check, because both sides make the same mistake.

`omega_ref` is that second judge. It does not make machine code at all. It reads an Omegese program
and simply *does* what each line means, step by step, like a person following a recipe: it keeps the
numbers, the arrays and the pools, reads its input, writes its output, and stops with the same error
numbers ("traps") the rules call for. Then we run the program both ways and check that the answers,
every output byte and the exit status, are identical.

It was written from the rule book only: `docs/one_layer/OMEGESE.md` (sections 3 and 4 for the base
language, section 9 for the systems words) and `seed/README.md`. **om4's own source (`seed/rung4/om4.s`)
and the Omega compiler's source (`seed/omc/`) were not read while writing it.** Read as test material
only: the gate4 test list and test programs (`seed/rung4/gate4.sh`, `seed/rung4/tests/`), the referee
runner's output format (`seed/check/runner.c`) and om3's refusal numbers (`seed/rung3/gate3.sh`).

It is written in C. That is allowed here because it is an oracle: it sits outside the seed chain and
never makes a byte of it, the same way gcc is used only as a referee in gate4. Nothing in `seed/`
depends on it.

## How to use it

    sh tools/omega_ref/build.sh                      # builds build/omega_ref/omega_ref
    build/omega_ref/omega_ref check prog.om          # 0 if accepted, else the refusal number (as om4)
    build/omega_ref/omega_ref run prog.om < in > out # runs a whole program (one with fn main)
    build/omega_ref/omega_ref call prog.om f inputs  # function mode: calls f once per input line

`run` behaves like the program om4 would build: same input, same output, exit status = main's value
modulo 256, or 100 + the trap number when a trap stops it. A program the rules refuse exits 125 under
`run` (so a refusal can never be mistaken for an exit status); `check` gives the refusal number itself.

The gates read om4.bin, gate4.sh and the gate4 tests from `$SEED/rung4` and omc.om from `$SEED/omc`
(default `seed`), and write their logs to `$EVID` (default `evidence/OMEGA-REF`):

    SEED=/path/to/seed EVID=evidence/OMEGA-REF-SYNC sh tools/omega_ref/gate_ref.sh

Point SEED at the seed tree of the compiler under test. On this branch `seed/` is still the tree from
before the names pass (9.2: every signature read before any code, so a call may name a function written
later); the sync of 2026-10-24 ran every gate with SEED at a checkout of `seed/omc-unified` (omc.om
18d7e80b, om4.bin d0da40e4), logs in `evidence/OMEGA-REF-SYNC/`. The omc.om under this branch's own
`seed/omc` (1e5542bc) is no longer a program of the current language: the current om4 and the interpreter
both refuse it at line 908 with error 10 (a parameter named `cond` while a function `cond` exists, 9.2), so
a default run of `gate_ddc.sh` here fails for that reason, not for a mismatch. `gate_ddc.sh` also takes `OMC_REF`
(the branch to fetch omc.om and om4.bin from when `$SEED/omc/omc.om` is missing, default
`origin/seed/omc-unified`) and `DDC_FLIP=<offset>`, which inverts one byte of stage 2 on purpose so the
whole gate must end in FAIL (its negative control, run by hand).

## The check (`tools/omega_ref/gate_ref.sh`)

Every case gate4 has, run both ways (`evidence/OMEGA-REF/gate_ref.log`):

- the 10 practice tasks and the 9 function-mode programs (149 inputs): same value, same raw register,
  same trap;
- the 4 whole programs (Fibonacci, sorting 1,000 numbers, echo of 50,000 random bytes, byte copy):
  same output bytes and exit status;
- the 14 programs that must stop with a given status;
- the 41 refused programs: the same refusal number.

Result (against the om4 of seed/omc-unified, om4.bin d0da40e4, 2026-10-24, `evidence/OMEGA-REF-SYNC/gate_ref.log`):
213 cases written, 213 run, all equal (GATE_REF PASS, RC=0). The five newest are gate4's names-pass
cases: a call to a function written later (exit 7), mutual recursion (exit 0), a call cycle in function
mode (refused 9), a later function's name written as a new scalar (refused 7), and a forward call in
function mode that the referee runner and the interpreter both answer with 7. Negative control: a copy of
the interpreter with signed division made unsigned is caught on fm1. (The earlier sync, om4 at
seed/omega-in-omega@0a75aa3c: 208 of 208, `evidence/OMEGA-REF/gate_ref.log`.)

## Decision 22: one way to read every partial program (`tools/omega_ref/gate_next.sh`)

Decision 22 says the language must read left to right with one legal next word, so a writer (a person or
a model whose choices are masked) can never be halfway into something that has two meanings.
`tools/omega_ref/s9.grammar` writes the om4 language down as a grammar of about 40 short lines, and
`omega_next` follows a program word by word with every rule that could apply, never choosing early. After
each word it counts the ways the words so far can be read. With the names pass, `omega_next` reads the
`fn` lines of the whole text before it parses, as 9.2 does, so a call may name a function written later
(SPEC_GAPS.md G32 says what that means for a writer under a mask). The check
(`evidence/OMEGA-REF-SYNC/gate_next.log`, GATE_NEXT PASS, RC=0, 2026-10-24):

- every word prefix (36,490) and every cut inside a word (79,732) of the 40 gate4 programs om4 accepts
  and of omc.om, the Omega compiler written in Omega (34,630 word prefixes and 77,006 cuts on its own):
  exactly one reading, the real next word legal, a half-written word still finishable;
- the 43 refused gate4 programs (41 one-liners and the two names-pass files om4 refuses): one reading up
  to the word where the grammar stops;
- 800 one-word mutants of those programs: whatever om4 or the interpreter accepts, the grammar accepts;
  om4 and the interpreter give the same verdict and the same refusal number on all 800;
- the 144 probe programs: every one the interpreter accepts reads one way;
- negative controls: three grammar copies with one ambiguous rule each are caught with two readings, and
  a copy missing the `exit` rule is caught on the program that uses it.

`omega_next next s9.grammar prefix-file` prints the legal next words after any prefix, one per line, in
the style of `omegese next --machine`. It knows words and names, not types, so it may allow a word that a
type rule refuses later (18 of the 58 mutants it accepts).

## Diverse double-compiling: a third parent for omc (`tools/omega_ref/gate_ddc.sh`)

L408 wrote the Omega compiler in Omega (`seed/omc/omc.om`). om4 builds it once (stage 1); stage 1 then
compiles omc.om again (stage 2). Both steps share om4's way of making machine code, so a mistake in that
design could copy itself into both and still look consistent. The interpreter shares none of it: it runs
omc.om on its own source text by meaning alone.

Result (`evidence/OMEGA-REF-SYNC/gate_ddc.log`, `ddc.sha256`, omc.om 18d7e80b of seed/omc-unified, PR
#696, 2026-10-24): the interpreter's output is byte for byte the same 224,520-byte program as stage 2,
sha256 d0603ff8fc276774...183e83 (the fingerprint of the omc that `seed/omega-from-seed.sh` builds and
reports as its stage 2 = stage 3 fixpoint), same exit 0, in under a second. GATE_DDC PASS, RC=0. Controls:
an interpreter with one bit of unsigned shift changed gives a different program; one changed byte of
stage 2 is caught; and a whole run with `DDC_FLIP=100` (byte 100 of stage 2 inverted on purpose) ends in
GATE_DDC FAIL, RC=1 (`evidence/OMEGA-REF-SYNC/ddc-flip-control/gate_ddc.log`). The earlier result, omc.om
at 0a75aa3c, 74,740 bytes, sha256 10d75652...b01398e, is in `evidence/OMEGA-REF/`.

In plain words: two completely different ways of reading the compiler (one turns it into machine code, one
just follows the rules) agree on every one of the 224,520 bytes it produces when it compiles itself.

## Beyond gate4: probes of the rule book

`tools/omega_ref/probe_ref.sh` runs 144 more small programs through both, each with the answer the rule
book gives (`evidence/OMEGA-REF-SYNC/probe_ref.log`, PROBE_REF PASS, RC=0). They found two om4 slips (F1: the
largest unsigned numbers, from 2^63 up, were refused; F2: one refusal had the wrong error number), both
fixed by L408 within the hour (dedfd93c, with gate4 cases added) and 32
places where the rule book is silent (G26 to G32 came with the names-pass sync: where a call cycle is
reported, the names pass and blocks, which numbers are bool numbers, the order of refusals within a line,
and what a next-word mask means when a name's class depends on later text). Each is in
`tools/omega_ref/SPEC_GAPS.md` with the sentence we propose adding (decision 19: no meaning left open).
The sync also fixed two places where the interpreter strayed from the rule book: a number in a bool place
(section 7 lets a bare number take its place's type; 0 and 1 now run, others are 8) and the limit of 6
parameters (9.2; now 9, as om4).
