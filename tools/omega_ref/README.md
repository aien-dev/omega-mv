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

## The check (`tools/omega_ref/gate_ref.sh`)

Every case gate4 has, run both ways (`evidence/OMEGA-REF/gate_ref.log`):

- the 10 practice tasks and the 9 function-mode programs (149 inputs): same value, same raw register,
  same trap;
- the 4 whole programs (Fibonacci, sorting 1,000 numbers, echo of 50,000 random bytes, byte copy):
  same output bytes and exit status;
- the 9 programs that must stop with a given status;
- the 33 refused programs: the same refusal number.

Result: 195 cases written, 195 run, all equal (GATE_REF PASS, RC=0). Negative control: a copy of the
interpreter with signed division made unsigned is caught on fm1.

## Decision 22: one way to read every partial program (`tools/omega_ref/gate_next.sh`)

Decision 22 says the language must read left to right with one legal next word, so a writer (a person or
a model whose choices are masked) can never be halfway into something that has two meanings.
`tools/omega_ref/s9.grammar` writes the om4 language down as a grammar of about 40 short lines, and
`omega_next` follows a program word by word with every rule that could apply, never choosing early. After
each word it counts the ways the words so far can be read. The check (`evidence/OMEGA-REF/gate_next.log`,
GATE_NEXT PASS, RC=0):

- every word prefix (1,606) and every cut inside a word (2,044) of the 32 accepted gate4 programs: exactly
  one reading, the real next word legal, a half-written word still finishable;
- the 33 refused gate4 programs: one reading up to the word where the grammar stops;
- 640 one-word mutants of those programs: whatever om4 or the interpreter accepts, the grammar accepts;
  om4 and the interpreter give the same verdict and the same refusal number on all 640;
- the 125 probe programs: every one the interpreter accepts reads one way;
- negative controls: three grammar copies with one ambiguous rule each are caught with two readings, and
  a copy missing the `exit` rule is caught on the program that uses it.

`omega_next next s9.grammar prefix-file` prints the legal next words after any prefix, one per line, in
the style of `omegese next --machine`. It knows words and names, not types, so it may allow a word that a
type rule refuses later (12 of the 44 mutants it accepts). When L408 pushes `seed/omc/omc.om`, the same
gate runs over every prefix of it.

## Beyond gate4: probes of the rule book

`tools/omega_ref/probe_ref.sh` runs 125 more small programs through both, each with the answer the rule
book gives (`evidence/OMEGA-REF/probe_ref.log`, PROBE_REF PASS, RC=0). They found two om4 slips (F1: the
largest unsigned numbers, from 2^63 up, are refused; F2: one refusal has the wrong error number) and 25
places where the rule book is silent. Each is in `tools/omega_ref/SPEC_GAPS.md` with the sentence we
propose adding (decision 19: no meaning left open).
