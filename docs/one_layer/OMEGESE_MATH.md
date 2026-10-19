# Omegese as mathematics: the automaton, the masked distribution, and what the words are for

Status: written 2026-10-19 for Drake's challenge ("take out the human words; convert the binary into a semantic
wave function in straight mathematics"). Sections 1 to 4 formalize what already exists in
`tools/omegese/omegese.c`; nothing here is new theory. Section 5 is the pre-registered experiment that asks
whether the words matter (evidence: `evidence/NO-WORDS/`).

## 1. Objects

- **Text.** A program is a byte string. `omegese emit` reads it (`tools/omegese/omegese.c`, the parser
  `run_line` and the end-of-input checks) and either accepts it, lowering it to an op list, or refuses it with
  one reason. Write **L** for the set of accepted texts.
- **Checker state.** After reading a prefix p, the parser holds a state s(p): the functions written so far
  (names, parameter and return types), the open blocks (fn, if, else, while, for) and their nesting, the names
  in scope with their types and kinds, which names have been read, the ownership state of every own array on
  every path, the loop checks still waiting for their `end`, the registers, blocks and constants used, and the
  words of the unfinished line. The state is the parser's own global variables; `next` copies it
  (`S0f`, `restore()`, omegese.c around line 1714).
- **Live prefix.** p is *live* when some continuation finishes it into L: there is a string w with pw in L.
  The empty text is live; a refused prefix is dead; a text in L is live and *accepting*.
- **The mask.** For a vocabulary V of pieces (characters, Omegese words, or tokenizer tokens), the mask after
  p is the 0/1 vector m_p over V with m_p(v) = 1 exactly when pv is live. `omegese next` prints this set
  for words, name classes, number intervals, brackets, space, newline and end of input (OMEGESE.md 6.1 and
  6.2); a token mask follows from it by the rule in 6.2 (append a token's text when the current word followed
  by that text is still the start of a printed item).

## 2. Which class of language

L is a set of strings; the question is what machine recognizes it.

**With the code's limits, L is finite, so it is regular.** omegese.c bounds everything: the input
(`MAX_IN`, 1 MiB), functions (`MAX_FUNCS` 32), parameters (6), names (512, each at most 63 characters),
nesting (`MAX_FRAMES` 64), registers (480), blocks (512), distinct constants (256), words on a line (96),
own arrays tracked (64) and waiting loop checks (256) (omegese.c lines 34 to 43, 146, 184). Every number is
bounded by its type. A finite set of strings is a regular language, so a deterministic finite automaton
recognizes L: its states are the checker states s(p), its transitions are "read one more piece", its accepting
states are those where the end-of-input checks pass. This automaton is never built. The number of states is
astronomically large (every combination of names in scope, types, read flags and ownership states), so the
regular-language fact is true and useless for engineering.

**Without the limits, L is not context-free.** The nesting of fn/if/else/while/for ... end alone is a Dyck
(balanced brackets) language, which is context-free. But three rules compare names written far apart: a name
must be declared before it is read, a new name may not equal a visible one, and every name written must be read
before its scope ends. Matching an unbounded name against an earlier unbounded name is the copy language
{w#w}, which no pushdown automaton recognizes (pumping lemma). With unbounded names the language needs memory
that grows with the program: it is context-sensitive (a linear-bounded machine, the parser itself, decides it).

**What is used in practice: a state machine computed on demand.** The engine never needs the whole automaton,
only the out-edges of the current state. `omegese next` computes them: for each candidate item it builds a
*witness* (the line is finished with the words it still needs, every open block is closed) and runs the real
parser on it from the current state (omegese.c around line 1700, the comment above the next oracle, and
`witness()` at line 1842). An item is printed only when its witness is accepted, so the mask is **sound by
construction** (nothing printed leads to a dead end). It is **complete** up to the witness finder's limits
(OMEGESE.md 6.4); the suite's mutant and fuzz checks found no miss (OMEGESE.md 6.5).

## 3. The "semantic wave function"

Let x be the task (signature and prompt), y = (y_1, ..., y_T) a program as a sequence of vocabulary pieces,
y_<t its first t-1 pieces, and z_t in R^|V| the model's logits at step t (a function of x and y_<t).
Let m_t = m_{y_<t} be the mask from section 1, tau the temperature. One step of the masked model is

    pi(v | x, y_<t) = m_t(v) exp(z_t(v) / tau) / sum over u in V of m_t(u) exp(z_t(u) / tau)

and the probability of a whole program is

    Psi(y | x) = product over t = 1..T of pi(y_t | x, y_<t),   with y_T = end of input.

Every symbol: V the vocabulary; v, u pieces of it; m_t(v) in {0, 1} the mask; z_t(v) the model's score for v;
tau > 0 the temperature; pi the next-piece distribution; Psi the distribution over whole programs.

Properties, each a consequence of soundness:

1. **Psi puts all its probability on L.** Every step with positive probability keeps the prefix live, and the
   end-of-input piece is legal only when the prefix is already accepted, so every complete path is a program
   `emit` accepts. The denominator is never zero, because a live prefix always has a legal next piece.
2. **Psi is a probability distribution, not a quantum wave function.** The values are non-negative reals that
   sum to one; there are no complex amplitudes, no phases and no interference. The analogy that holds:
   before sampling, the model holds a weighted set of every legal program at once (a "superposition" in the
   loose sense); sampling picks one path (the "collapse"). Greedy decoding picks the most likely piece at each
   step. Top-p sampling (p = 0.8 in exp445) first keeps the most likely pieces up to mass p and then
   renormalizes; applying the mask before the cut is what keeps property 1.
3. **What collapses it:** drawing y_t from pi at each step, with the run's seed. Nothing else.

**What the receipt certifies.** The realized path y lowers through the fixed chain: Omegese -> op list
(`omega_emit`) -> canonical binary IR -> checker2 -> signed receipt (OMEGESE.md section 2, rule 7). checker2
ACCEPT certifies the IR is well formed: types, definite assignment, ownership and borrows, and every loop proven
to end (the bounded shape). With the ownership rules folded into `emit`, a path the mask allows also passes
checker2, except for the residual cases in OMEGESE.md 6.4 (a borrow outliving its owner across a merged view,
more than 64 own arrays).

**So the mask gives "cannot be malformed". It does not give "cannot be wrong."** A legal program can compute
the wrong answer: in the r3 Stage 1 run, 16 of 60 held-out Omegese answers passed every check and still
behaved wrongly (7 unexpected traps, 8 wrong results, 1 wrong trap; OMEGESE.md section 7 item 4).

**What would give "cannot be wrong" inside the same mask: contracts.** The IR already has the parts:
`requires` checks (trap 9, the caller's fault) and `ensures` checks (trap 10), and checker2's bit-blaster
proves, for every argument value, that a function passing its `requires` ends without any trap, `ensures`
included, with an LRAT proof (`src/checker2/c2_blast.h`, property NO-TRAP, for a supported subset: scalar
code, no calls, no memory, no division). Omegese has no `requires` or `ensures` word today. Adding them, and
writing the task's specification as the program's first lines (forced by the engine, as the signature line
would be), makes the specification and the program one path through one mask; the receipt would then certify
the stated property, not only well-formedness. It certifies only what the specification says: a specification
that is itself wrong, or a property outside the blaster's subset, is not covered. Not built.

## 4. What the words are for

Let sigma be a one-to-one renaming of the fixed words (and of the chosen names) into another spelling, B.
Then L_B = sigma(L), and for every prefix the masks agree: m^B_{sigma(p)}(sigma(v)) = m_p(v). The set of legal
programs, the set of legal next pieces at each step and everything the receipt certifies are identical. The
only thing that changes is z_t: the model's scores for the renamed pieces, learned from what it saw in
training. Under a mask, therefore, **the words matter for exactly one thing, the model's prior**: whether
`add` (a word it has seen next to arithmetic billions of times) gets a better score in the right place than a
meaningless symbol. That is an empirical question; section 5 measures it.

Two honest qualifications. First, sigma is one-to-one on words, not on tokenizer tokens: a spelling whose
words are several tokens (the numbers spelling: two-digit numbers are several tokens) makes paths longer, so
the mask must also handle the middle of a word (OMEGESE.md 6.2 already does at the character level). Second,
the runs in section 5 apply **no mask while the model writes**: the 27B server has no option for it (no
grammar or logit-bias field), and the mask inside our own engine is phase 2 work (OMEGESE.md section 7 item 2).
Without the mask the spellings differ in two ways: the prior on legal continuations, and how often the model
leaves L. The reader's refusals measure the second; the answers that pass the reader measure the first.

**The repo's algebra work is about numbers, not programs.** `src/algebra/oma_trit` is balanced-ternary
encoding (a value as two bit planes), `oma_z3` the integers mod 3 mapped onto balanced trits, `phase_twin` a
software model of carrying Z3 values as the phase of a tone (addition as phasor multiplication), and
`oma_select` a retired selector that picked the cheapest exact realization of those number systems from
benchmark receipts. Each represents numbers and arithmetic on them; none represents programs, and none is a
distribution over programs. The program representations in the repo are the OSC text, the op list, the binary
IR and Omegese; the only "wave" object over programs is Psi above, and it lives in the inference engine, not
in `src/algebra`.

## 5. The experiment: words, symbols, numbers

(Filled in from `evidence/NO-WORDS/` after the run; the prediction was committed first.)
