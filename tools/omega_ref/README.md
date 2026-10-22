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

## Where it has to guess (the rule book is silent)

Listed in `tools/omega_ref/SPEC_GAPS.md`, each with the sentence we propose adding (decision 19: no
meaning left open).
