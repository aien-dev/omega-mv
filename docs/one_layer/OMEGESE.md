# Omegese: a program language built for the AI writer

Status: v1 (nested values, exact next oracle, 2026-10-15). Phase 1 done (language, translator, checks, token measurement);
phase 2 started (section 7).
Tool: `tools/omegese/omegese.c`; tests: `make test-omegese`. Named Glyph until 2026-10-15 (the name was
taken); evidence recorded before then says glyph. Home: omegese.com (owned by Drake since 2026-10-15).

## 1. Why

exp445 (`docs/one_layer/EXP445_PROTOCOL.md`) asks AIs to write Omega programs as OSC text, as a
JSON op list (`tools/omega_emit/SCHEMA.md`) and as hex bytes of the binary IR. The cost and the
errors come from the same places: punctuation and keys (JSON), two characters per byte (hex), and
bookkeeping the writer must do in its head: numbered registers with declared types, numbered blocks,
byte offsets. Text avoids the bookkeeping but needs a compiler for a human-readable language.

Omegese keeps the machine-first path (no human source language in production) and removes the
bookkeeping and the punctuation. Its words are chosen from the writer model's own vocabulary.

## 2. Design rules

1. **One word, one token.** Every fixed word (ops, types, keywords) is exactly one token of the
   target tokenizer when written with its leading space (Qwen3; the 8B, 30B and 4B tokenizers share
   one vocabulary). `make test-omegese` checks this when given a tokenizer (section 6). There is no
   punctuation besides `=` (also one token, and optional) and the optional brackets `( )`; no
   commas, no indentation. Values nest in prefix order (every op takes a fixed number of values), so
   brackets are never needed. Numbers cost one token for the space plus one per digit (Qwen3 splits
   digits).
2. **Types are inferred.** Only parameters, the return type, conversions, typed numbers
   (`uint 5`) and new arrays name a type. A value's type comes from the op that makes it, bottom-up
   through nested values; a number takes the type of the other value in its op. This removes the
   "register declared one type, written with another" error class.
3. **Names, not numbers.** Values are named; registers, blocks and branch targets are numbered by
   the translator. Control is structured (`if`/`else`/`end`, `while`, `for`).
4. **Scope gives definite assignment.** A name first written inside an `if`, `else`, `while` or
   `for` dies at its `end`; to carry a value out, write it before the block. So every read is
   written on every path by construction.
   Every name written must be read before its scope ends (parameters and loop counters excepted), so a
   misspelled word (the short form turns `rte a` into a new name) or a forgotten value is refused.
   No name hides a visible one (loop names included). Parameter names are distinct. Calls go to earlier functions only, with each argument checked
   against the parameter (arrays: own from own, edit from own or edit, view from any).
5. **Loops are proven to end.** Every loop lowers to the shape checker2 proves terminating
   (`src/checker2/c2_check.c`, "loop bounds"; the shape oscc emits for `while ... bound N`).
6. **Prefix-decidable.** After any prefix the checker knows which words may come next
   (`omegese next`): the mask an inference engine can apply while the model writes, so an invalid
   program cannot be written.
7. **Same trust chain.** Omegese -> omega_emit op list -> canonical OSC binary IR -> checker2 ->
   signed receipt. Omegese changes nothing after the op list.

## 3. The language

A program is one or more functions. A line holds one statement; words are separated by spaces.

```
fn <name> (<param> <type>)* to <type> | void      a function; `end` closes it
<name> = <value>          write a value (a new name, or an existing one of the same type);
<name> <value>            the same, with `=` left out
if <cond> / else / end
while <cond> [bound N] / end      N 1..999999, default 1000000; past N iterations: trap 4
for <name> <type> <count> / end   name = 0 .. count-1 (count a number, or a value of that type)
                          (any other count: trap 4 after 1000000 passes, as a while with no bound)
break                     leave the innermost loop; outside a loop: error 9. Like ret, it ends the
                          path (a later line in the same block is error 17)
continue                  jump to the next iteration: a while's head, a for's increment. Outside a
                          loop: error 9. Like ret, it ends the path (a later line is error 17)
ret [<value>]
put <array> <index> <value>      store
trap <code 1..14>         stop with that code (out of range: error 8). Program mode exits 100 + code;
                          function mode calls OscRt.trap. Like ret, it ends the path (a later line in
                          the same block is error 17)
<function> <args>         call a function that returns nothing
```

Values are prefix expressions. Every op takes a fixed number of values, and each of those values may
itself be an op, so no brackets are needed: `add mul r r 4` is add(mul(r, r), 4), and
`eq rem n 2 0` is eq(rem(n, 2), 0).

| value | takes | meaning | IR |
|---|---|---|---|
| `<name>` / number | 0 | copy | mov / const |
| `add sub mul div rem and or xor shl shr` a b | 2 | arithmetic, bitwise, shifts (shift amount any whole-number type) | bin |
| `eq ne lt le gt ge` a b | 2 | comparison, gives bool | cmp |
| `neg` a / `not` a | 1 | negate (signed) / logical not (bool) or bitwise not (number) | un |
| `<type>` a | 1 | a typed number (`uint 5`) or a conversion (`ulong x`, `ulong add a b`) | const / cast |
| `get` arr i | 2 | load (arr is an array name) | load |
| `new` type len init | 3 | a new own array of len (1..64) elements; only as the whole value of `<name> =` | alloc |
| `own` / `edit` / `view` arr | 1 | move / mutable borrow / shared borrow (arr is an array name) | mov |
| `<function>` args | its parameter count | call an earlier function | call |

Brackets are optional grouping: `( <value> )` holds exactly one complete value and changes nothing
else (`sub diff (i64 b)` = `sub diff i64 b`). A `(` without its `)`, a `)` without its `(`, an empty
`( )`, or a `( )` holding more than one value is refused. `(` and `)` are separate words even when
written against a name.

Typing is bottom-up. A value's type comes from its op; a bare number takes the type of the other
value of its op (`add x 1`, `sub 15 i`, `shl 1 sh`), or the type the place needs when it stands
alone (`ret 0`, `x = 5` for an existing x, a call argument, an index is uint). Two bare numbers in one
op are refused (`add 1 2`, also when nested: `add a mul 2 3`). Comparisons give bool. In a shift a
bare number on the left takes the type of the shift amount (`shl 1 sh` with sh uchar shifts a uchar);
write its type when you mean another (`shl ulong 1 sh`). A typed number inside a conversion is
converted (`long int 5` is the int 5 converted to long; `uchar int 300` traps 5).

A condition is any value that gives bool (`while ne b 0`, `if or lt p -32768 gt p 32767`). Values may
be nested wherever a value goes: after `=`, `ret`, `if`, `while`, `put` (index and value), call
arguments, a `for` count, inside conversions. `and`, `or` evaluate both sides (no short cut).

A wrong number of values is refused with its place: `line 3: le has 3 values after it; it takes 2`,
`line 2: sub has 1 value after it; it takes 2`, `extra words at the end of the line: 'b'`.

Types: `bool uchar ushort uint ulong byte short int long` = bool u8 u16 u32 u64 i8 i16 i32 i64
(the IR names `u8` .. `i64` are accepted too, as the task signature writes them; they cost 2 or 3 tokens)
(`u32` is three Qwen3 tokens; `uint` is one). Arrays: `own|edit|view <type> <len>`.

Example (dev04):

```
fn gcd a uint b uint to uint
while ne b 0 bound 100
t = rem a b
a = b
b = t
end
ret a
end
```

## 4. Lowering

Numbers are hoisted into the entry block, one CONST per distinct value and type (so the loop bound
is defined exactly once, as checker2 requires). Blocks are numbered in the order they start.

- `while c bound N`: hidden ulong counter = 0; head: c -> guard | exit; guard: `ge counter N` ->
  trap block (trap 4, br exit) | body; the body starts with `counter = add counter 1`.
- `for i T K` (K a number): i = 0; head: `lt i K` -> body | exit; latch: `i = add i 1`.
- `for i T n` (n a name or any value that is not a number): as `while lt i n`, with the hidden guard,
  plus `i = add i 1` at the latch.
- `if c` -> cbr then | else-or-join; a join block exists only if some branch reaches it.
- Nested values: each op inside a value writes a fresh hidden register (a local with the op's type,
  never a name, so the unread-name rule does not apply), in evaluation order: innermost first, left
  to right (`x = add mul a b c` -> `t = mul a b; x = add t c`). Only the outermost op writes the
  target name, after every operand has been read. Call arguments are evaluated left to right before
  the call; a `put` evaluates its index, then its value, then stores.
- A typed number used as a value (`uint 5`) is the hoisted CONST itself; a bare name or number used
  as an operand is read in place (no copy).
- `while`: the condition's ops are emitted in the head block, so the whole nested condition is
  computed again before every pass.
- `for i T <value>` (a count that is not a number): a name is read at every pass (as v0); any other
  value is computed once, before the loop.
- `trap N`: `trap N`, then a `ret` that is never reached (with a zero of the return type), because
  the IR block still needs a terminator.

## 5. Measured (phase 1)

The 10 public dev tasks (`tests/one_layer/exp445/dev`), reference solutions in each spelling,
tokens counted offline with the Qwen3 tokenizer (the measure.sh method: text = reference.osc;
bytes = hex of the compiled IR; structured = `omega_emit lift` of that IR; Omegese =
`tests/one_layer/omegese/dev/*.omegese`; Omegese short = the same with `=` left out).

| task | text | bytes | structured | Omegese | Omegese short |
|---|---|---|---|---|---|
| dev01 | 64 | 281 | 309 | 48 | 45 |
| dev02 | 60 | 256 | 286 | 43 | 40 |
| dev03 | 81 | 567 | 624 | 57 | 53 |
| dev04 | 82 | 441 | 507 | 41 | 38 |
| dev05 | 27 | 118 | 91 | 17 | 17 |
| dev06 | 27 | 112 | 91 | 17 | 17 |
| dev07 | 120 | 594 | 750 | 72 | 65 |
| dev08 | 103 | 807 | 926 | 84 | 77 |
| dev09 | 75 | 488 | 547 | 55 | 50 |
| dev10 | 22 | 98 | 79 | 15 | 15 |
| **total** | **661** | **3762** | **4210** | **449** | **417** |

Omegese is 9.4x fewer tokens than the structured op list and 32% fewer than OSC text, for the same
IR shape (all 10: harness `ola.sh experiment` set omegese-1 accepted 10/10, RUN VALID, run
`20261015T094000Z-e27a33ee052d-562014`; checker2 ACCEPT on all 10).

Limits: the Omegese programs were written by the designer, not by a model, and in places use the
shortest form (writing to parameters instead of copies), so the text column is not an equal-effort
rewrite. Whether models write Omegese reliably is not measured yet.

## 6. The next oracle and the checks (`make test-omegese`)

### 6.1 What `omegese next` promises

`omegese next <prefix>` reads a prefix of a program (any text, cut anywhere, even inside a word) and
prints what may come next. It is **exact**: an item is printed if and only if some text that starts
with it turns the prefix into a program `omegese emit` accepts. Nothing that can still lead to an
accepted program is missing (within the limits in 6.4), and nothing printed leads to a dead end. When no text can, it prints one
`REFUSED omegese: ...` line and exits 1: a defect in its complete words gets the reason `emit` would
give; otherwise (also when the word being written cannot be finished) the line says `no text after this prefix makes a program emit accepts` with the reason the
nearest attempt failed.

How it decides. The prefix is split into its complete lines, the complete words of the last line, and
the word still being written (empty after a space, a bracket or at a line start). For each candidate
item a witness is built: the line is finished with the words it still needs (a `ret` line also reads
the names the ending path would otherwise leave unread), every open block is closed (`x = x` for each
unread name, a `ret` of a zero where a value is owed, `end`), and the real parser that `emit` runs
re-reads that line and the closing lines from the state after the complete lines, with every
end-of-input check. An item is printed only when its witness is accepted, so every printed item is
legal by construction. Completeness rests on the witness finder; the test fuzz below checks it.

Names stay open at the character level. When a line binds a new name, any identifier is legal except
a reserved word (`omegese words` and the IR type names `u8` .. `i64`) or one equal to a visible
name; that is printed as one class item. When a line reads a name, the names printed are the
visible names (and earlier functions) of the right type and kind.
Numbers come as intervals: the parser only compares a number with fixed bounds (type ranges, array
lengths 1..64, trap codes 1..14, loop bounds and counts up to 1000000) and with the lengths of the
arrays in scope, so one test per breakpoint and per gap between breakpoints decides every number, and
neighbouring legal pieces merge. Numbers are written as decimal digits with an optional minus sign and
no leading zeros; `-0` is refused by `emit` as well (it is not a number).

### 6.2 `omegese next --machine`: the fixed grammar

One item per line, in this order, nothing else on stdout. Exit 0 with at least one item, or exit 1
with exactly one line starting `REFUSED omegese: `. Let U be the word still being written.

| line | meaning |
|---|---|
| `word X` | finish the current word as X (X starts with U): a fixed word, a visible name, a function, or a name written earlier on the same line (a new parameter) |
| `newname E1 E2 ...` | finish the current word as any new name: a letter or `_`, then letters, digits or `_`, at most 63 characters, starting with U, not a reserved word, and not one of E1 E2 ... (the visible names, functions and names earlier on the line that start with U; the list may be empty) |
| `number LO HI` | finish the current word as a number v with LO <= v <= HI whose decimal text starts with U |
| `(` | an opening bracket (it also ends the current word) |
| `)` | a closing bracket (it also ends the current word) |
| `space` | a space or tab: ends the current word, or adds blank space between words |
| `newline` | ends the current word and the line |
| `eof` | the input may end here: the prefix is already an accepted program |

A writer (an engine's mask) may append text s to the current word exactly when U followed by s is
still the start of some `word`, `newname` or `number` item; the soundness fuzz checks that a word
stays legal after each of its first characters.

Example, after `fn f a uint to uint` and a newline: `word if`, `word while`, `word for`, `word ret`,
`word trap`, `word a`, `newname a f`, `space`, `newline`.

### 6.3 Human output and `omegese complete`

`omegese next` without `--machine` prints the same set for a person, one labelled line per kind,
empty lines left out: `assign:` (at a line start, the names that may be assigned) or `names:`,
`number: LO..HI, ...`, `new-name: any name not in use`, and `words:` (fixed words, functions, `(`,
`)`, `(space)` inside a word, `(newline)`, `(end of input)`).

`omegese complete <prefix>` prints one text that, appended to the prefix, gives a program `emit`
accepts (finish the word and the line, then close everything); nothing when the prefix is already a
program; exit 1 (`REFUSED`) when there is none.

### 6.4 Limits (where exact can miss)

There is no over-approximation: every printed item was accepted by the real parser. An item can be
missed only when the witness finder fails although a completion exists, which needs one of these
limits: a line longer than 96 words (a witness can be longer than the shortest completion), more than
480 registers, 256 distinct constants or the block and frame limits in one program, more than 64
tries when several names of the right kind could be read, or a new name within a few characters of
the 63-character limit (the class is decided by one representative name). None of these is reached
by any program in the suite or the fuzz.

**Ownership (checker2's C2-OWN rules), folded into `emit` on 2026-10-15.** checker2 follows every own
array along every path (it holds an array, holds nothing yet, or was freed or moved) and rejects a use,
a move or a free where it holds nothing on every path (C2-OWN-USE-DEAD, C2-OWN-DOUBLE-RELEASE), and a
new array or a return where it still holds one on every path (C2-OWN-LEAK). `emit` keeps the same
states along the lines it reads and refuses such a line where it is written, so `next` leaves out the
word that leads there. Inside an open loop a state that includes the loop head is not known until the
loop's `end` line (a later line of the body can change it); such a check waits and is decided at
`end`. `complete` (and every witness) settles what it can: before a loop's `end` it gives a name the
loop head needs an array (`x = new ...`) or frees one that would still hold its array there, and at the
function's end it frees each array that still holds one on every path (or ends the path with `trap 1`
when that array's name is out of scope).

Measured with the residual walk in 6.5 (1000 walks from each of two starts, seed 4453): completed
walks that `emit` accepts and omega_emit or checker2 then rejects.

| build | rejected later | by rule |
|---|---|---|
| before (2026-10-15, `f6b44356`) | 154 of 2000 (7.7%) | 136 C2-OWN-LEAK, 15 C2-OWN-USE-DEAD, 3 C2-OWN-DOUBLE-RELEASE |
| ownership states only, no loop checks | 2 of 2000 (0.1%) | 2 C2-OWN-USE-DEAD (a use in a loop head of an array freed before it) |
| after | 0 of 2000 | none |

Not folded, so a program `emit` accepts can still be rejected later:
- C2-BORROW-OUTLIVES-OWNER: checker2 ties each view to the array it was taken from over the whole
  function (a view filled from two arrays loses its origin); `emit` does not follow borrows (0 of
  2000 walks met it);
- more than 64 own arrays in one function: the ones past 64 are not tracked;
- C2-OWN-ARENA-MOVE and C2-OWN-RELEASE-ARENA: Omegese has no arenas, so no program reaches them.

The other way, `emit` refuses a function with more than 256 distinct ownership checks waiting for loops to end,
which checker2 might accept. And where exact can miss: a waiting check whose name is out of scope (or
hidden by a newer name) at the loop's `end` gets no settling line, so the word that led there is left
out; a `ret` whose value moves the array into a call is not tried by the witness.

### 6.5 The checks

- the 10 dev programs: omegese -> omega_emit -> checker2 ACCEPT;
- nested values: 6 dev programs rewritten with nested values (`tests/one_layer/omegese/nested`) and 5
  trial 2 answers kept verbatim (`trial2`), all ACCEPT; the trial 2 isqrt answer (dev07) is still refused
  with `line 3: le has 3 values after it; it takes 2` (it has one value too many);
- 8 coverage programs (IR type names, break, nested loops, else-if, a for with a name or a value as count,
  arrays and calls, calls without values, a bool function as a condition, nested values);
- 46 refusals, each with its expected reason, each with a fixed control that is accepted (arity, brackets,
  two numbers in one op, extra words, `new` inside a value, lines after trap, and 12 ownership cases: a
  leak at a return or at a second `new`, a second free, a use after a move, an out-of-scope array at a
  return, and five decided at a loop's `end`, one of them through a nested loop);
- review fixes: a conversion of a typed number converts it (`ret long int 5`, `uchar int 300`); a for
  count in brackets lowers exactly as the bare count and has the same limit;
- `omegese next` on 69 prefixes and cases, red before green (the open-type list after a binary op, a typed second
  value, a bracket waiting for `)`, a `ret` that must read the names its path leaves unread, a cut
  inside a word, the exact number range of every scalar type and of trap codes, array lengths, loop
  bounds and counts, a minus sign where none fits, `-0`, the `--machine` lines including the 6.2
  example and the new-name exclusion list, `complete`, refusals as soon as a prefix is wrong, and
  ownership: no `ret` or second free while it would be refused, `end` left out of a loop whose head
  needs an array, `complete` settling a loop and freeing at the function's end, a loop opened at the
  nesting limit refused, 300 uses of one array in a loop accepted);
- next oracle: at all 2636 word boundaries of the 75 accepted programs in the suite, the real next
  word, and the end of input at the end, are legal;
- completeness: 150 single-word mutants of those programs (a word dropped, doubled, or replaced by a
  number, a name, a fresh name or a fixed word): `emit` accepts a mutant if and only if every word of
  it was legal when it was written (2906 word boundaries);
- soundness fuzz: 40 random walks of up to 25 legal items from random cut points (a number item is
  tried at its ends and at a random value inside); the legal set is
  never empty, a word picked stays legal after each of its first characters, and `omegese complete`
  then finishes the walk into a program `emit` accepts within a bounded number of lines;
- checker2 residual: 40 walks from each of two starts (an empty file and a cut point in a suite
  program), each closed with `complete` and sent through omega_emit and checker2; at most
  `OMEGESE_RESIDUAL_MAX` (default 0) may be rejected there (`OMEGESE_RESIDUAL_WALKS` sets the count,
  `OMEGESE_RESIDUAL_KEEP` a folder that keeps the rejected programs; the table in 6.4 used 1000);
- truncation fuzz: all 2799 byte prefixes of the nested, trial and coverage programs through `emit`,
  `next` and `next --machine`, which must answer (accept or refuse with a reason) and never crash; at
  the 2698 prefixes of accepted programs, `next` must also allow the byte that follows (the whole
  word it belongs to) and `complete` must finish the prefix into a program `emit` accepts;
- loop guard negative control: the dev04 op list without the hidden counter increment passes
  omega_emit and is REJECTED by checker2 (`C2-LOOP-UNBOUNDED`);
- tokenizer gate, when `OMEGESE_TOKCOUNT` (a counter: `<tool> <tokenizer.json> words <word>...`
  printing the token count of " word" first) and `OMEGESE_TOKENIZER` are set.

The same suite then runs again on a build with AddressSanitizer and UndefinedBehaviorSanitizer
(memory and undefined-behaviour checks), with 10 walks and one mutant per program; every sanitizer
finding fails the run. Its negative control: two canary builds (`-DOMEGESE_CANARY=1`, a read after
free; `=2`, a signed overflow) must each be caught first. Clean. A stress run with 25 mutants per
program (1575 mutants) and 500 walks of up to 40 items (17386 items) found no failure.

## 7. Phase 2

1. Prompt-only trial on the dev tasks, no mask (done once, 2026-10-15): the 30B on our own engine, one
   attempt, harness run `20261015T105049Z-e27a33ee052d-1500714` (RUN VALID): Omegese 3/10, text 3/10,
   structured 0/10; completion tokens 1015 / 1346 / 12027. 8 of 10 first answers were refused only for
   writing the task signature's type names (`u8`, `u32`); those are now accepted.
   Trial 2 (same model, one attempt plus one repair round): Omegese 2/10, text 3/10. 6 of the 8 Omegese
   failures wrote nested values (`n = add (mul (uint 3) n) 1`), which v0 refused. Values now nest (section 3): 5 of
   those 6 answers, kept verbatim in `tests/one_layer/omegese/trial2`, now pass checker2; the sixth
   (dev07) has one value too many and is still refused, with its place. Rerun owed.
2. The mask inside our own engine: `omegese next` as a table on the GPU, one lookup per token;
   positions with one legal word need no model step.
3. A pre-registered held-out comparison: exp445 revision r3 (`docs/one_layer/EXP445_PROTOCOL.md`
   section 13), Omegese against OSC text. The `omegese` arm's system prompt is
   `tools/exp445/arms/common.md`, then `tools/exp445/arms/omegese.md` (the language and a word card:
   every fixed word on one line with what it does), then the worked examples
   (`tools/exp445/arms/examples/`, built by `reader.sh system_prompt`): 14 dev-style tasks in the
   many-shot mode, the r2 single example (`clamp_sub`) in the one-shot mode (Stage 0 only). The `text`
   arm gets the same tasks with the program in OSC. `make test-exp445` checks every example in both
   forms (reader, back end, the sealed harness on its own inputs), that the word card is exactly
   `omegese words`, the one-token gate on every word of the examples, and that no example shares an
   entry name with a held-out task.
4. Where the r3 Stage 1 answers failed, by reader stage (config A, attempt 1, 60 held-out tasks per
   arm; aggregate counts and refusal categories from the score output only, no task text read):

   | arm | round | passed the reader | refused | where refused, by category |
   |---|---|---|---|---|
   | Omegese | first answer | 35 | 25 | all at `omegese emit`: type 13, structure 6, arity 4, scope 1, range 1 |
   | Omegese | repair round | 4 | 21 | all at `omegese emit`: type 11, structure 6, arity 3, scope 1 |
   | text | first answer | 44 | 16 | the OSC compiler |
   | text | repair round | 5 | 11 | the OSC compiler |

   No Omegese answer was refused at omega_emit, at the signature check or by the back end. Final
   verdicts: Omegese 23 ACCEPT, 21 no candidate, 16 wrong behaviour (7 unexpected trap, 8 wrong result,
   1 wrong trap); text 34 ACCEPT, 11 compile refused, 15 wrong behaviour (6 missing trap, 8 unexpected
   trap, 1 wrong result).
   What a legal-word mask (`omegese next` in the engine) prevents: every one of those Omegese refusals,
   since each was an `emit` refusal and the mask only lets through words that some accepted program
   continues. With the ownership rules folded in, what the mask lets through also passes checker2's
   ownership rules, within the limits in 6.4.
   What it does not prevent: an answer whose function has the wrong name, parameters or return type is
   still an accepted program, so only a forced signature header (the task's `fn` line written for the
   model) prevents a signature mismatch; none was seen, but every Omegese refusal stopped at `emit`
   before the signature check, so mismatches could be hidden behind them. Neither prevents the 16
   wrong-behaviour answers. The 11 text refusals would need a mask for OSC text.

## 8. Prior art (quick search, not exhaustive)

Grammar-constrained decoding (Outlines, XGrammar, llguidance, SynCode), grammar-aligned decoding
(NeurIPS 2024), type-constrained decoding for TypeScript (Mündler et al., PLDI 2025), and a grammar
redesigned for fewer tokens (SimPy, 2024). Not found: a lexicon chosen so one terminal is one vocab
token, with type-aware prefix checking feeding an independently checked, signed IR. "New" is
UNVERIFIED.

## 9. Systems words (seed chain om4 and omc, ledger L408)

Added 2026-10-22 for the Omega compiler written in Omegese (`seed/omc/omc.om`) and the translator that
first builds it (`seed/rung4/om4.s`). Every word below is used by omc or by the seed test corpus
(`seed/rung4/tests`); the C path (`tools/omegese`) does not read these words yet. Each rule names the
gate that tests it (`seed/rung4/gate4.sh`, section numbers of that script). Decisions 16-24 are those of
`docs/PROGRAM.md` (branch doc/program).

### 9.1 Two kinds of output

- A program with a line `fn main` is a whole Linux program (program mode); otherwise the output is the
  functions' machine words under the OSC-1 native rules (function mode), entry = word 0. (gate 4 s.4-6)
- `fn main` has no parameters and returns a whole-number type or void. The program's exit status is
  main's value modulo 256 (Linux keeps the low 8 bits), 0 for a void main. (s.6 t_exit, s.7)
- A trap in program mode ends the program with exit status 100 + the trap code; in function mode it
  calls OscRt.trap as the C path does. (s.6 t_*, s.4-5)

### 9.2 Functions and calls

- `fn` may appear many times. A names pass reads every signature before any code is written, so a call
  names any function in the unit, earlier or later. A name that is a function never becomes a new
  scalar: a value-returning function used as a statement is error 7, including `name = value`.
  (s.6 r5b_fwd, r5b_assign, r5b_fwd_fm)
- In program mode a function may call itself, and two functions may call each other. In function mode
  a call cycle, including a function calling itself, is error 9, so the stack a call chain uses is
  bounded. (s.6 fib, r5b_mut, r5b_cycle, s.7)
- `<function> <args>` as a value calls a function that returns a value; as a line it calls a void one.
  Using the one where the other is needed is error 7. (s.7)
- `to void` makes a void function; `ret` with no value returns from it, and so does reaching its `end`.
  A function with a return type whose `end` can be reached is error 18. (s.6 t_void, s.7)
- Arguments are read left to right, each fully before the next. (s.5 fm5)
- Function names may have up to 63 characters; other names up to 8 (error 13). (s.4 dev01, s.7)
- A function has at most 6 parameters; a seventh is error 9. `main` has none (error 9). A new name
  may not reuse a name already in scope, a function or a struct: error 10.
- In program mode the stack is a fixed 8 MiB pool; a call or a local array that would go below it traps
  6 before it writes anything. (s.6 t_stack)

### 9.3 Numbers (decision 19: one meaning per word and width)

- Unsigned add, sub, mul, shl, neg, not wrap modulo 2^width (uchar 8, ushort 16, uint 32, ulong 64 bits).
  (gate 3, s.4)
- Signed add, sub, mul, neg, div and shl trap 1 when the exact result is outside the type (byte 8,
  short 16, int 32, long 64 bits), including the minimum divided by -1. (s.5 fm1-fm4, fm9)
- div and rem trap 2 when the divisor is 0. Signed div rounds toward zero; signed rem has the sign of
  the dividend, and the minimum rem -1 is 0. (s.5 fm1, fm2, fm9)
- A shift amount outside 0 .. width - 1 traps 7, for every type. shr of a signed value keeps the sign.
  (s.5 fm3, fm4)
- A conversion `<type> v` traps 5 when the value is outside the new type. (s.6 t_conv)
- There are no implicit conversions (decision 20): the two values of an op, a value and the name it is
  written to, an argument and its parameter, all have the same type, or the line is refused (error 7).
  A bare number takes the type the place needs. `uchar` is an 8-bit unsigned number, never a character. (s.7)
- A number is written in decimal, with a `-` in front when negative, in at most 20 digits; it must be a
  value of the type it takes: -2^63 .. 2^63 - 1 for long, 0 .. 2^64 - 1 for ulong, and so on (error 8
  outside; more than 20 digits, or a character that is not a digit, is error 6). (s.6 t_u63, t_u64max,
  t_cmp_unsigned, t_conv_sign, t_write_wrap, s.7)
- `true` and `false` are the bool numbers 1 and 0. (s.6 t_loop)

### 9.4 Arrays (decisions 16, 17, 18, 23, 24)

- Every array has a fixed element type (uchar .. long, never bool) and a fixed length written as a
  number; the length travels with the array's name, never as a terminator byte. An array parameter or
  a pool written without its length is refused (error 16). (s.7)
- `global NAME T N`: a pool of N elements (1 .. 16777216), all 0 when the program starts, in the
  program's own memory image (no heap, no allocation call); at most 1 GiB of pools, else error 23.
  Program mode only (error 9). (s.6 echo, bytecopy, s.7)
- `text NAME "bytes"`: a read-only array of uchar holding the bytes between the quotes, with its
  length; `\n` is 10, `\t` is 9, `\\` is 92, `\"` is 34; any other escape, or a quote missing its end,
  is error 22. Program mode only. (s.7)
- `NAME = new T N v`: a local array of N elements, each v, on the stack, given back when the function
  returns. Only in a function's outermost block (error 9); at most 4 MiB of local arrays (error 23).
  There is no `free`: nothing is given back by hand (decision 24), and the word is refused (error 9).
  (s.5 fm7, fm8, s.6 sort, s.7)
- A parameter `NAME view T N` or `NAME edit T N` takes an array of exactly that type and length (error 7
  otherwise). A view is read-only; `put`, `read` into it, or passing it as edit is error 20. A text is
  read-only in the same way. (s.7)
- An argument is a local array or an array parameter, optionally after `view` or `edit`. A global is
  never passed (error 19), and the arrays of one call are distinct names (error 19); so two live
  references in one function are always distinct storage, and no write is ever seen through another
  name. (s.7)
- `get A i` reads element i; `put A i v` reads i, then v, then writes. `len A` is A's length as a ulong.
  A number index must be inside A when om4 reads it (error 21); any other index is checked when it runs
  and traps 3 when it is not below the length (a negative signed index traps too). There is no word that
  turns an array into an address or adds to one. (s.5 fm8, s.6 t_bounds, s.7)

### 9.5 System calls and lines

- `read fd A off n` and `write fd A off n` (fd int, off and n ulong) move n bytes between the open file
  fd and A's elements off .. off + n - 1 with the Linux read and write calls, and give a long: the bytes
  moved, or minus the Linux error number. They trap 3 unless n <= len A and off <= len A - n, before the
  call. `read` needs a writable array (error 20). A must be an array of uchar for the counts to be bytes.
  `pread fd A off n pos` / `pwrite fd A off n pos` (pos long) do the same at an explicit file position
  without changing the descriptor's offset, with the same slice checks and result. (s.6 echo, bytecopy, t_rw)
- `exit v` (v int) ends the program with status v modulo 256. Like `ret`, it ends the path: a line after
  it in the same block is error 17. (s.6 t_exit, s.7)
- `note ...` is a line the translator does not read beyond its first word. (s.6 sort, fib)

### 9.6 Capacities (decision 23)

Input over a capacity is refused with error 14, never overrun: om4 reads at most 8 MiB - 1 bytes and
writes at most 8 MiB, 1024 names in scope, values nested 1024 deep, 512 functions, 8192 trap sites per function, 512 nested
blocks. A function that needs more than the 16 registers it hands out is error 12.

### 9.7 Structs (omc only, rung 6)

om4 does not grow for this. A struct program is refused by om4. omc.om does not use these words, so
om4(omc.om) stays equal to omc(omc.om). Checked by `seed/omc/gate6.sh`.

- `struct Name field type [len] ... end`, one line, before the first `fn`. 1 to 16 fields, at most 16
  structs, at most 64 cells (error 14 over that, error 16 if empty, error 4 if the line ends before `end`). A field is a name
  and a scalar type; a number after the type is an array field of that length (1 .. 64). Every field
  is one or more 8-byte cells, in order, no padding. (gate6 t7, t8, t9: a wrong scale overlaps a
  neighbour and changes the result; the expected results are 1, from the layout, not from this compiler)
- The struct's name is a word from that line on. No function, variable, or field may be spelled the
  same (error 10). Field names are unique in their struct (error 10). A variable may share a field's name.
- `p = new S v1 .. vn`, only as the whole of an assignment, and only in the function's outermost block
  (error 9 inside `if` / `while` / `for`). One value per field; an array field takes one value, stored
  in every element. The cells are a stack slot, 16-byte aligned, given back when the function returns.
  There is no `free` (decision 24); the word stays error 9. (gate6 t1-t6, still_free, new_in_if)
- `get p f` reads a scalar field. `get p f i` reads an element of an array field. `put p f v` and
  `put p f i v` write. A view cannot be written (error 20). An unknown field is error 3. A number index
  outside the field is error 21; any other index is checked when it runs and traps 3, as for arrays.
- A parameter is `p view S` or `p edit S`. A call passes an own or edit struct to edit, and any struct
  to view, always of the same struct (error 7 otherwise, error 20 for a view passed to edit). The
  structs of one call are distinct names (error 19). A function does not return a struct.
- `len` of a struct is refused (error 7). `read` and `write` of a struct are refused (error 7).

### 9.8 Break, continue and trap (omc only, gate7)

`break`, `continue` and `trap` are the path-ending statements §3 lists. omc implements all three;
om4 refuses them, so they are omc-only like structs. Checked by `seed/omc/gate7.sh`; it is run by
`seed/host/check.sh`.

- `break` scans the open blocks for the innermost loop (kind 3 while, 4 for), emits an
  always-taken `B.AL` to that loop's exit and records the word. When the loop closes, `s_end`
  patches only the entries recorded for that block and marks them spent, so a later loop that
  reuses the block slot cannot move an earlier break. Outside a loop it is error 9. Like `ret`,
  the line ends the path: a later line in the same block is error 17. At most 1024 break sites
  per function (error 14 over that), reset when the function ends. (gate7 w1, f1, n1-n3)
- `continue` finds the innermost loop the same way, but its recorded branch is patched to the loop's
  latch, not its exit: the head of a while, so the condition and the bound count run again, or the
  increment of a for, so the counter still advances. Outside a loop it is error 9; a later line in the
  same block is error 17; at most 1024 sites per function (error 14). (gate7 c1-c5, bad_continue,
  bad_after_continue)
- `trap N` takes a decimal number 1..14 (error 4 when the line ends, 16 when the token is not a
  number, 8 out of range). It emits the same always-taken branch the runtime trap checks use;
  `finish` makes one stub per code, so program mode exits 100 + N and function mode calls
  `OscRt.trap`. The line ends the path (error 17 for a later line in the same block).
  (gate7 t1, t2, bad_trap0, bad_trap15)
- None of the three may be a name; all are reserved from the keyword table. (gate7 om4 refusal)


### 9.9 Floating point (omc only, gate8)

The words are `bitcast`, `fsqrt`, `fabs`, `fmin` and `fmax`, not `bits`, `sqrt`, `abs`, `min` and
`max`: the benchmark corpus uses those shorter spellings as ordinary identifiers, and a keyword may
not take a name that existing `.om` code already uses. gate11 pins both halves of that rule.

`f32` and `f64` are IEEE-754 binary32 and binary64. A float value is its bit pattern held in an
ordinary integer register; arithmetic moves it to v0/v1, runs one scalar FP instruction, and moves it
back. FPCR is never written: round to nearest even, subnormals kept, no fused operations, one rounding
per operator. There is no implicit conversion: mixing f32 and f64, or float and integer, without a
conversion is error 7.

- Literals: `digits.digits[(e|E)[+|-]digits][f32|f64]`; a digit must be present on both sides of the
  dot, and a leading `-` negates. A literal with no suffix takes its type from context exactly as an
  integer literal does; where no context gives a float type it is error 11. The decimal text is
  converted exactly to the target type, keeping a 64-bit significand, a binary exponent and one
  sticky bit, then rounding once to nearest even: 0.1f64 is 0x3FB999999999999A. The full binary32 and
  binary64 range is covered, including subnormals (5e-324, 1e-45) and signed zero. A literal that
  rounds to infinity is refused (error 8); one below the smallest subnormal underflows to signed
  zero. The significant digits must still fit in 2^63-1 (error 8 beyond that). (gate8 f64_bits,
  f32_bits, neg_ambiguous, neg_dot, neg_overflow, neg_overflow2, neg_overflow3)
- Operators: `+ - * /` and unary `neg`; comparisons `eq ne lt le gt ge`. Bit operators, shifts and
  `rem` on a float are error 7. IEEE results: division by zero and overflow give infinity, 0/0 gives
  NaN, and no float operation traps. NaN compares false everywhere except `ne`, which is true. -0.0
  compares equal to 0.0; `bitcast` exposes the sign. (gate8 arith, compare)
- Intrinsics `fsqrt x` and `fabs x` take one float; `fmin a b` and `fmax a b` take two of one float
  type and follow FMINNM/FMAXNM (IEEE minNum/maxNum: one quiet NaN returns the other operand; -0.0
  orders below +0.0). A bare integer operand is error 7 and an unresolved literal is error 11.
  (gate8 intrinsics, neg_intr_*)
- Conversions `<type> value` handle int<->float, f32<->f64 and float->int. Int to float is SCVTF/UCVTF
  (round to nearest even); float to int is FCVTZS/FCVTZU (truncate, saturate, NaN -> 0); f32<->f64 is
  FCVT. `bitcast T value` reinterprets a value of the same width (f32/i32/u32, f64/i64/u64) without
  changing its bits; any other width or a bool is error 7. (gate8 convert)
- Floats are allowed in `global` pools, `new` arrays, parameters, returns and struct fields; the same
  8-byte cell rule as for structs applies (an f32 field or array element still occupies an 8-byte
  cell, with the 32-bit pattern in its low half). A float cannot index an array or be a `for` count
  (error 7), `read`/`write` still need a uchar array (error 7), and IR mode refuses float words
  (error 3). (gate8 arrays, globals, params, structs, neg_*)


### 9.10 Grouping parentheses (omc only, gate9)

`(` and `)` are separate words even when written against a value (`(int 2)`, `((7))`); the lexer
splits them out of the surrounding word. A group returns exactly the value inside it and adds no
code. A group whose only value has no type is still ambiguous (error 11), and a group missing its
`)` is error 16. Checked by `seed/omc/gate9.sh`, which `seed/host/check.sh` runs.


### 9.11 Layout queries (omc only, gate10)

`sizeof T` is the size in bytes of a scalar type (bool 1; uchar/byte 1; ushort/short 2;
uint/int/f32 4; ulong/long/f64 8). `sizeof S` is a struct's size: 8 bytes for each of its cells.
`offsetof S f` is the byte offset of field `f`; an array field gives its first element. Both are
compile-time constants of type ulong and add no code, so they compose in expressions.

An unknown type name is error 16, an unknown struct or field is error 3, and `sizeof void` is
error 9. Checked by `seed/omc/gate10.sh`, which `seed/host/check.sh` runs.


### 9.12 Constants (omc only, gate16)

`const NAME TYPE LITERAL` at top level defines a named value before use. `NAME` is at most
8 characters and may not collide with a visible name, a function or a struct (error 10); the value
must fit the type (error 8). The constant is a typed number: an integer (`uint 5`, `long -3`) or a
float literal (`f64 0.1`, `f32 0.1`); an integer literal under a float type is refused (error 16).
It composes wherever a value goes, including inside functions and as a call argument, and it cannot
be assigned (error 7). In IR mode an integer constant is folded to its literal, including as a
`trap` code, a `while` bound, or a `for` count; a float constant there is refused (error 9), as are
struct/global/text declarations. Definitions are sequential, so a
constant defined after a function is not visible to that function.

An integer constant is also accepted as an array length in `new`, `global`, a parameter declaration
(`view T N` / `edit T N`) and a struct field length (1..64, error 14); a float constant used as a
length is error 16. A length token that is a name other than an integer constant is error 16, and
`arrlen` still enforces 1..16777216 for arrays (errors 8 and 23). Signatures are pre-scanned, so a
constant used in a parameter length must be defined before that function.

An integer constant is also accepted as a `trap` code (1..14) and as a `while ... bound` count
(1..999999); a `for` count is an ordinary value, so a constant works there too. Out-of-range values
keep error 8, and a float constant in any of these positions is error 16.

### 9.13 Profile OM-2 (omc only, `spec/omegese-om2.md`, ledger L834)

OM-2 is opt-in per unit; the contract `spec/omegese-om2.md` and its errata are normative, and this
section lists what omc implements of it. Fixtures, expected results and compiled mutants:
`seed/omc/om2/` (run by the #748 gate `seed/qualification/om2/gate.om`). Every OM-1 unit keeps its
meaning and, for the C2 manifest, its exact output bytes or error.

- **Opt-in.** Leading blank lines and `note` lines are skipped; when the next line is exactly `om2`,
  `om2 diag` or `om2 fmt` (no other bytes on it) the unit is OM-2. Any other line leaves the unit OM-1,
  where the `om2` line itself is error 16 as before. The words of this section are reserved only in an
  OM-2 unit: `rotr rotl unroll inline fresh abstract seal open holds layout field at little big part out
  export ops`. Using one as a name there is error 16; an OM-1 unit keeps them as ordinary names.
- **`om2 diag`.** After the human line `omc: line N: error E` (unchanged), stderr gets one record
  `omc-diag 1 code=E sev=S line=N col=C span=A-B expect=K found=H obligation=O profile=OM-2`. `line` is
  the human line. `A-B` is the half-open byte span of the token being read (the LF for a line that ends
  too early; `len-len` at the end of input) and `col` the 1-based byte column of A on its line. `found` is
  the span's bytes in hex (at most 32 bytes), `-` when empty. `expect` is the class being read: `number`
  for errors 6 and 8, `end` for error 15, `name` for an unknown name, otherwise `value`, `type`, `name`,
  `newline` or `word` from the reader that failed. `sev` is `unknown` for an undeclared function or struct
  (errors 3 and 13 at a call or `offsetof`; error 16 for an undeclared word where a type or struct is
  read, as in `new Pt` or `p view Pt`), `incomplete` when the failure is on the end-of-input token or
  in a text cut by the end (except the call-cycle error 9), else `error`. `obligation` is `bounds` (21),
  `growth` (14), `regs` (12), `init` (27), `ctor` (28), `layout` (29) or `-`.
- **Hex literals.** `0x` and 1..16 digits `0-9a-f` is the unsigned value and takes the type the place
  needs under the decimal range rule (error 8 outside; `int 0xffffffff` is error 8). A `-` before it, `0X`,
  no digits, more than 16 digits, an uppercase digit, `_` or any other character is error 6.
- **`om2 fmt`.** Only when the input's first line, at byte 0, is `om2 fmt` (error 16 elsewhere). The rest
  (the bytes after the first LF) is compiled as a unit of its own, OM-1 or OM-2 by its own first line;
  if it is refused, the error is reported against the whole input and nothing is written. If it is
  accepted, stdout gets its canonical text: note lines and text literal bytes exactly as written; the
  indentation as written; each other run of blanks between words one space, except next to `(` or `)`
  where it is kept; no blanks at a line end; a whitespace-only line empty; one LF after the last line. A
  CR anywhere and blank lines at the end are error 6. fmt never adds or removes a line, is idempotent,
  and compile(fmt(x)) is byte-identical to compile(x).
- **Rotate.** `rotr x n` and `rotl x n`: x and n of one unsigned type (uchar, ushort, uint, ulong; else
  error 7), the result x's type. n outside 0..w-1 traps 7 at run time, a number n included. uint/ulong
  with a number n give one `ROR` (immediate, EXTR); otherwise the amount check, then `RORV` (rotl through
  `NEG`). uchar/ushort replicate the value into a 32-bit pattern, rotate it, then truncate.
- **Unroll.** `for i T K unroll`: K a number 1..64 (error 8 outside; a name or other value is error 16).
  The body is compiled K times with `i` the constant 0..K-1 (a write to it is error 7). `break` or
  `continue` whose innermost loop is the unrolled one is error 9; inside a rolled loop in the body they
  work as usual. No loop branch or counter is emitted. Removing `unroll` leaves behaviour unchanged.
- **Inline.** `fn NAME ... to T inline`: every call is compiled in place. Arguments are read left to
  right as for a call; a scalar argument in a named register is copied, one in a temporary is used as it
  is. The caller's locals are hidden from the body, and `ret` becomes a branch to the body's end (a last
  one is dropped). The standalone function is still emitted. `new` in it, or an inline function reached
  again while it is being inlined (itself included), is error 9; so is an inline function that reaches
  itself anywhere in the call graph, through non-inline calls too (checked at the end of the input). A
  fifth nested inline level is error 14. Calls to other functions stay calls. Removing `inline` leaves
  behaviour unchanged. An argument that is a number is copied to a register, so folding does not reach
  through parameters.
- **Folding and checks.** Only inside an unrolled or inlined body, an operation (`add` .. `shr`,
  comparisons, `rotr`/`rotl`, `neg`, `not`) on two typed numbers is computed with OM-1 arithmetic. One
  that would trap (signed overflow, a divisor 0, an amount at or above the width) is not folded and keeps
  its runtime trap. Conversions are never folded. A number index into a sized array is checked when it
  is compiled (error 21), as in OM-1, and in an OM-2 unit up to 4095 it is an immediate offset (LDR/STR
  unsigned offset) with no runtime check. Every other index keeps its runtime check.
- **Bounds.** At most 65536 emitted words per function after unrolling and inlining, 64 copies per
  unrolled loop and 4 inline levels (error 14, `obligation=growth`). 16 registers stay the limit
  (error 12, `obligation=regs`). Expansion fuel (E14, E16): each function has 65536; an unrolled loop is
  charged K when it starts, at its line, before any copy expands, and an inline expansion 1 at its call
  line, in emission order at every nesting depth (nested unrolls multiply). The charge that takes the
  total above 65536 is error 14 (`growth`) on its line; exactly 65536 is allowed. So two nested 64-copy
  unrolls (4160) compile and three do not. An inline function's standalone body is compiled with its own
  fuel where it stands in the source. The inline call graph keeps each edge once.
- **Trap sites.** With `om2 diag`, an accepted unit's stderr gets one `omc-site 1 line=N iter=k` record
  per runtime trap site, in emission order: N the source line, k the counters of the open unrolled loops
  outer to inner joined by `.`, or `-`. stdout is unchanged.
- **External layout.** `layout NAME SIZE`, then one line `field F T [COUNT] at OFF little|big` per field
  (note lines allowed), then `end`, at top level before the first `fn`. NAME and F at most 8 bytes and not
  OM-2 words; NAME is a new name (error 10), F unique in its layout (error 10); T an integer type
  (`uchar` .. `long`; else error 7). SIZE outside 1..4096 or a `layout` after the first `fn` is error 29
  on the layout line; a COUNT outside 1..4096, an OFF or field extent past SIZE, or a byte shared with an
  earlier field is error 29 on the field line (`obligation=layout`). No COUNT is a scalar field (E5).
  A parameter `c view NAME` / `c edit NAME` is a `uchar` array of exactly SIZE bytes (another array is
  error 7; ABI: one pointer). `get c F` / `get c F i` and `put c F v` / `put c F i v` load / store the field
  at OFF + i * width with its width and byte order (unscaled register-offset LDR/STR, REV16/REV for
  big; signed fields sign-extended). The index is checked like an array's: a number outside COUNT is
  error 21, a run-time one traps 3. A missing index on an array field is error 4, an extra one on a
  scalar field error 5, `put` through a view error 20. Any other word after `c` reads it as the plain
  `uchar` array (`get c i`, `len c`, passing it on). Structs (9.7) are unchanged.
- **Fresh scratch.** A parameter `s edit T N fresh` (N at most 64, else error 14 `obligation=init`; on
  a view error 16) has unspecified incoming elements. omc tracks, per such array, which elements are
  definitely written on the path being compiled: a `put` with a number index (after unrolling,
  inlining and folding) marks its element; a `get` with a number index needs that element, any other
  `get` all N (else error 27, `obligation=init`, on the get line). An if without else, a while and a
  rolled for leave the set as it was when they opened (no induction); an if/else keeps the elements
  written in both arms, or in the arm that does not end in `ret`; an unrolled or inlined body keeps
  what it wrote. Passing the array to a non-inline call, `read`, `write` or a host operation is error
  27 on that line; an inline callee works on the same set, and its own `fresh` parameter starts empty.
  At most 16 fresh arrays per function, inline ones included (error 14). The generated code is unchanged.
- **Abstract types.** `abstract NAME T [ops W ...]` (T `uint` or `ulong`, else error 7; W from `rotr rotl
  and or xor`, else error 16) and `holds NAME dup32`, at top level before the first `fn` (error 9 after
  it). NAME is a type distinct from T. A value of it is made only by `seal NAME v` (v of type T) in the
  function named NAME followed by `mk` that returns NAME, inlined or not; a `seal` anywhere else is
  error 28 (`obligation=ctor`) on its line. `open v` gives the T value. A T value or a number where NAME
  is needed, a conversion, comparison or any operation on it not listed by `ops`, an array or struct
  field or `const` of it, `sizeof`/`bitcast` of it, and NAME as a parameter or result of the entry of a
  function-mode unit are error 7. Listed operations run on T and give NAME: `rotr`/`rotl` of NAME by a T
  count (E9; 64 traps 7), `and`/`or`/`xor` of two NAME values. `holds NAME dup32` (ulong only; any
  other predicate, or a uint NAME, is error 28 on the holds line) makes every seal check high32 ==
  low32 at run time (LSR, CMP UXTW, B.NE trap 10), except when the constructor's body is exactly the
  line `ret seal NAME or shl ulong x 32 ulong x`, x a uint parameter, then `end` (the check is erased,
  rule `dup32`: uint values are held zero-extended). With `om2 diag` each seal compiled writes
  `omc-check 1 kept|erased line=N rule=dup32|-` (N the seal line) with the omc-site records.
- **Unsized views.** A parameter `NAME view T *` / `NAME edit T *` takes an array of element type T of any
  length (error 7 for another type): a sized array (E2, also on internal functions), an unsized one or a
  text (view only). `len NAME` is its run-time count, a ulong value. Every index into it, a number too,
  is checked at run time against the count (CMP, B.HS trap 3; no error 21, no elimination); `read`,
  `write`, `pread`, `pwrite` and the host words keep their slice checks against the count. An unsized
  array never goes to a sized parameter (error 7); view / edit rules, distinct names (19) and fresh (27)
  are as for sized arrays. `new` stays sized; `fresh` needs a length (error 16).
- **Parameter slots.** Parameters take x0..x5 in order, in both modes: an unsized view or edit two slots
  (pointer, then count), every other parameter one (a scalar, a sized array, a layout or a struct by its
  pointer). A parameter that needs a seventh slot is error 9 on the fn line.
- **Part.** `part A o k` (o, k ulong; another type is error 7), only as an argument for an unsized
  parameter of a call (a sized parameter is error 7; anywhere else, `read`/`write` included, error 9), is
  the k elements of A from o. A is a name: a local, a view, an edit or a text (a global is error 19, a fresh
  array error 27). o is read, then k, then the checks run in this order: o <= len A, then k <= len A - o
  (CMP, B.HI trap 3; the wrapped o + k is never formed). The callee gets A + o * size and count k. A part
  of a local or an edit is an edit, or a view with `view` in front; a part of a view or a text is a view
  (to an edit parameter error 20). For distinct names a part is its base A: A and a part of it, or two
  parts of A, in one call are error 19 whatever the ranges.
- **Out.** A parameter `NAME out T N` (N a length; `*` or `fresh` is error 16) is written by the unit;
  the bridge owns the buffer and copies it out after status 0 (spec 6, E1). Inside the unit it is an edit
  of T N in one slot (its pointer): `get`, `put`, passing it on as an edit; a view or an array of another
  length passed to it is error 20 / 7.
- **Export.** `export fn NAME ...` at top level (error 16 elsewhere, and when `fn` does not follow) marks a
  function-mode entry (with `fn main` error 9; a 33rd export error 14). With any export, words 0 .. E-1
  are `B` to the exports in source order and the code starts at word E; the other functions are private.
  Without export the OM-1 rule holds (one function: its bare words; more: word 0 is `B` to the last). The
  entry rules of the abstract types apply to each export (to the last function when there is none).

## 10. Native host extension (2026-10-23)

Implemented by the Omega-written compiler in `seed/omc/omc.om`. The original
om4 parent builds this extended compiler; the extended compiler reproduces
itself byte for byte. The original subset retains its machine code. The new
words are reserved and require program mode (error 9 in function mode).

All results below have type `long`. Successful kernel results are nonnegative;
failures return minus the Linux error number. Programs must check errors.
These operations do not form a sandbox: they operate with the process's
existing permissions. They expose no pointer values and no arbitrary syscall.

In signatures, `fd`, `flags`, `mode`, `pid`, `signal`, `events`, `domain`,
`kind`, `protocol`, `backlog`, `target`, `options`, `level`, `option` and `whence` are `int`.
Offsets, byte counts, capacities and argument indices are `ulong`. File offsets
and time intervals are `long`. `A` is a `uchar` array. Every slice checks
`n <= len A` and `off <= len A - n` before kernel access, otherwise trap 3
(program exit 103). Writable operations reject `view` and `text` (error 20).
Non-byte arrays and mismatched scalar types are error 7.

| Expression | Meaning |
| --- | --- |
| `fopen A off n flags mode` | Open a length-delimited path relative to the current directory. Return descriptor. |
| `fclose fd` | Close descriptor. |
| `fsync fd` | Sync file data and metadata. |
| `fdsync fd` | Sync file data only (`fdatasync`). |
| `memfd A off n flags` | Create an anonymous in-memory file and return its descriptor (`memfd_create`). |
| `fchmod fd mode` | Set permissions on an open descriptor. |
| `chmod A off n mode` | Set permissions on a checked path (`fchmodat`, flags 0). |
| `chown A off n uid gid` | Set ownership on a checked path (`fchownat`, flags 0). |
| `faccess A off n mode` | Check access to a checked path (`faccessat`, flags 0): mode 0/1/2/4. |
| `getuid` / `geteuid` / `getgid` / `getegid` | Process identity (syscalls 174-177). |
| `fchdir fd` | Change the working directory to an open directory descriptor. |
| `getppid` | Parent process id. |
| `setsid` | Create a session and become its leader. |
| `getpgid pid` | Process group id of `pid` (0 = self). |
| `getsid pid` | Session id of `pid` (0 = self). |
| `syncfs fd` | Sync the filesystem containing an open descriptor. |
| `shutdown fd how` | Shut down part of a connected socket. |
| `setpgid pid pgid` | Set a process's group (0 = self/own pid). |
| `umask mask` | Set the file-mode mask; return the previous mask. |
| `fseek fd offset whence` | Seek and return the resulting signed file offset. |
| `ftrunc fd length` | Set an open descriptor's length (Linux `ftruncate`). |
| `truncate A off n length` | Set a checked path's length (Linux `truncate`). |
| `unlink A off n` | Remove the named file. |
| `makedir A off n mode` | Create a directory. |
| `rmdir A off n` | Remove an empty directory. |
| `cwd A off capacity` | Write the current directory into writable bytes. Return the kernel byte count including the trailing zero byte. A short buffer returns a negative error. |
| `chdir A off n` | Change the working directory to a checked, nonempty path. Return 0 or -errno. |
| `readlnk PATH off n OUT off capacity` | Read a symbolic link through a checked, nonempty path into writable output bytes. Return the byte count without a terminator. A result equal to capacity may be truncated. |
| `getdnts fd A off n` | Write raw Linux `getdents64` records; return bytes written. |
| `fstat fd A off n` | Write the 128-byte Linux ARM64 stat structure; requires capacity at least 128. |
| `stat A off n B off capacity` | Metadata for a checked path via `newfstatat`; writes the same 128-byte ARM64 structure; a short output traps 3. |
| `lstat A off n B off capacity` | As `stat`, but does not follow a final symbolic link (`AT_SYMLINK_NOFOLLOW`). |
| `renames A off n flags` | Rename the two paths in a packed list using `renameat2`; flag 1 refuses replacement. |
| `link A off n` | Create a hard link from a packed two-path list (old, new). |
| `symlnk A off n` | Create a symbolic link from a packed two-path list (target text, link path). |
| `monons` | Monotonic nanoseconds, suitable for elapsed-time measurement. |
| `realns` | Realtime nanoseconds since the Unix epoch, suitable for timestamps. |
| `sleepns interval` | Sleep for nonnegative nanoseconds. Negative intervals trap 3. Interruptions return the kernel error. |
| `socknew domain kind protocol` | Create a socket. |
| `bind fd A off n` | Bind the socket to the address bytes in the slice. |
| `connect fd A off n` | Connect using the address bytes in the slice. |
| `listen fd backlog` | Listen for connections. |
| `accept fd flags` | Accept a connection with `accept4`; return descriptor, without peer-address output. |
| `sockname fd A off n` | Write socket address bytes; return the full address length, which may exceed capacity when truncated. |
| `sockopt fd level option A off n` | Set a socket option from a readonly byte slice. Option values and layouts follow the Linux ARM64 interface. |
| `recv fd A off n flags` | Receive into a writable slice. |
| `send fd A off n flags` | Send a slice with `MSG_NOSIGNAL` added, so a closed peer returns an error. |
| `waitio fd events interval` | Poll one descriptor. Return readiness bits, zero on timeout, or negative errno. Interval is nanoseconds; negative means infinite. Events outside 0..65535 trap 3. |
| `fork` | Copy the current process: zero in the child, child PID in the parent, negative errno on failure. |
| `getpid` | Return the current process ID. |
| `killpid pid signal` | Send a signal; zero signal checks existence/permission. |
| `waitpid pid A off n options` | Wait with `wait4`; write a four-byte status, return PID or zero for an unfinished nonblocking wait. Capacity at least four. |
| `pipefds A off n flags` | Create a pipe with `pipe2`; write two little-endian 32-bit descriptors. Capacity at least eight. |
| `dupfd fd target flags` | Duplicate onto a chosen descriptor using `dup3`. |
| `execprog A off n` | Replace the process using a packed argument list. Only returns on failure. |
| `argcnt` | Return argument count, including the executable name. |
| `argread index A off capacity` | Copy argument bytes without a terminator; return byte length. An invalid index traps 3. Insufficient capacity returns -105 without changing the slice. |
| `envcnt` | Return the number of entries in the process environment (the `envp` the kernel supplied). |
| `envread index A off capacity` | Copy one environment entry without its terminator; return its byte length. An index at or past `envcnt` traps 3; insufficient capacity returns -105 without changing the slice. |
| `random A off n flags` | Fill a writable byte slice from Linux `getrandom`. Return the bytes written or -errno. `flags` is `int`: 0 blocks until the pool is ready, 1 `GRND_NONBLOCK`, 2 `GRND_RANDOM`, 4 `GRND_INSECURE`; other flags return -EINVAL. |

### Path and process boundaries

Paths are at most 4095 bytes. Embedded zero bytes and longer paths trap 3
before a kernel operation. The compiler privately copies and terminates paths
at the Linux boundary; Omega array lengths remain explicit. Private stack
space is checked against the existing stack floor before writing.

`readlnk` additionally rejects an empty path with trap 3. Its path may be a
readonly slice; its output must be writable. Both slices receive the ordinary
array bounds and byte-type checks. The private path copy is complete before
the kernel writes the output, so overlapping path and output slices are safe.
`cwd` requires writable bytes. It preserves the kernel's count convention:
the returned count includes its trailing zero byte. `readlnk` never appends a
zero byte. These operations are Linux `getcwd` and `readlinkat(AT_FDCWD)`.
`sockopt` is Linux `setsockopt`; it accepts readonly option bytes and does not
provide pointer values or arbitrary system-call access.

A packed list is a little-endian `u16` count followed by that many records,
each a little-endian `u16` byte length and those bytes. There is no padding or
terminator in the public format. The whole packet is at most 4096 bytes and
the count is 1..64. Embedded zero bytes, truncated records, extra trailing
bytes and out-of-range counts trap 3. `execprog` uses the first record as both
the executable path and argument zero; it must be nonempty. Other arguments
may be empty. `renames` requires exactly two nonempty path records.

The compiler constructs private, bounded pointer lists for the kernel. They
are never exposed as language values. `execprog` supplies an empty environment,
preserves the working directory and ordinary inherited descriptors, and does
not search PATH or invoke a shell. Programs should explicitly request
close-on-exec descriptors where needed. Forked children are independent
processes and must be reaped; this extension does not add shared-memory threads.

Clock results require nonnegative seconds and a signed nanosecond total;
values outside that range trap 1. `argread` bounds
its internal string scan at 1 MiB; reaching that bound traps 3. Linux ARM64 is
the only qualified host. OS flags, address layouts and metadata layouts are
host-specific; portable library contracts remain separate work.

Qualification is `sh seed/host/check.sh`; see `seed/host/README.md`. The seed
loader and shell test coordinator do not implement these operations. No
foreign-language compiler or runtime is on the new implementation path.

The proposed device/memory extension adds checked `devctl`, `mapnew`,
`mapdrop`, `mapread` and `mapwrite` to this same compiler. Its exact type,
ownership, range and refusal contract is [seed/host/MAPPING.md](../../seed/host/MAPPING.md).
It adds process-local mapped resources while the compiler's own storage remains
fixed pools. GPU launch and model execution are separate, still-unqualified gates.

## 11. OSC IR output (omc only, experimental checkpoint)

A leading line `ir`, before any other line, puts omc in IR mode: it writes OSC IR version 1
(`spec/one-layer-format.md`) for a unit of up to 32 functions instead of AArch64 machine words, and
the AArch64 path is not taken. Every function is encoded in source order. A function may call an
**earlier** function (self and forward calls are error 3) with at most six arguments; a
value-returning call composes as a value, a void call is a statement, and using one where the other
is needed is error 7. Argument types are checked at compile time (error 7). `break` lowers to a
`BR` to the innermost loop's exit and marks the path dead; `continue` is still refused (error 3).

`seed/omc/gate-ir.sh` compares 10 dev programs, nine hand-written multi-function call units and 30
randomized two-function call units byte-for-byte with the C-built `omegese` + `omega_emit` oracle, requires `checker2` to ACCEPT every unit, requires
a mul-to-add mutant to differ, and pins 14 refusal cases. Those C tools are referees only; they
never write omc's bytes.

Boolean `and`, `or` and `xor` lower to the IR bitwise sub-operations, just as on the
native path. `gate-ir.sh` checks all three against the C encoder and checker2.
Arithmetic (`add` through `rem`) and shifts of booleans remain error 7.

Limits, said plainly: scalar values, calls to earlier functions, at most 32 functions and 6
parameters. IR mode refuses a `struct`, `global` or `text` declaration with error 9 and refuses a
float word, a host word, `continue`, `sizeof` or `offsetof` with error 3; it never skips a
declaration it cannot lower. `note` lines are skipped. Struct IR remains open. The gate needs the
C-built oracles, so `seed/host/check.sh` runs it only when `build/omegese/omegese`,
`build/omega_emit/omega_emit` and `build/checker2/checker2` exist, and says `SKIP` otherwise.
