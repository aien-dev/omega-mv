# seed/: a compiler chain born from nothing

Drake's question: C was built from B, B from something older. If every old tool added mistakes or waste,
we would inherit all of it. So this folder grows a toolchain from a few hundred hand-written bytes, step by
step, with the existing C compiler and the GNU tools used only as a referee that checks the answers, never
as a builder.

Think of it as a ladder. Each rung is a small program, written in the language the rung below understands,
and built by the rung below. Every rung must rebuild itself to the exact same bytes, which proves it reads
its own language the same way the rung below does.

The only source for instruction bits is the public Arm Architecture Reference Manual (Arm document DDI 0487,
revision M.d, sha256 80d589a4...e3d5). Every instruction in every hand-written file names the manual section
it came from.

## Rung 0: seed-0 (done)

- `rung0/seed0.hex`: the whole program, written by hand as 102 lines of 8 hex digits: 30 lines are the
  file header Linux needs to start a program (each field explained on its line), 72 are machine
  instructions (each one named, with its manual section).
- What it does: reads text, turns every 8 hex digits into one 32-bit word, writes the words out. It skips
  comments (`#` to the end of the line) and spaces, and stops with an error on anything else.
- `rung0/seed0.bin`: the program, 408 bytes.
- The one outside tool: the very first `seed0.bin` was made by `rung0/load0.sh`, a 30-line shell script
  that uses only the shell's own `printf`. After that first build, seed-0 builds itself.
- Checks (`rung0/gate0.sh`, results in `evidence/SEED-0/rung0/`):
  - seed-0 reading its own text produces itself, byte for byte;
  - every one of the 72 instructions is read back by two independent decoders (our own, in
    `src/aarch64_decoder.c`, and GNU objdump), and both agree with the name written beside it:
    0 disagreements (`rung0/seed0.audit`, every word listed);
  - controls: a wrong name is caught; a bad character and a half-written word are refused.
  - Limit, said plainly: our own decoder does not know the "system call" instruction (SVC, 3 of the 72
    words) and does not read the immediate number of the compare instructions; for those, objdump and
    the hand decode in the comment are the checks.

## Rung 1: hex1, labels (done)

- `rung1/hex1.hex`: 164 instructions plus the 30-word header, written in seed-0's language and built by
  seed-0 (776 bytes).
- What it adds: names for places. `:c` marks a spot in the program, `@c` after a branch word fills in
  "how far to jump to c". That saves the writer from counting distances by hand, which is the most
  error-prone part of writing machine words. Labels are one character long; there is one branch form
  (the 19-bit distance field that conditional branches use; unconditional jumps use the "always"
  condition so they fit the same form).
- Checks (`rung1/gate1.sh`, results in `evidence/SEED-0/rung1/`):
  - seed-0 builds hex1; hex1 then rebuilds itself to the same bytes;
  - I counted all 50 jump distances inside hex1 by hand. As a cross-check, a copy of hex1 with those
    distances wiped out and replaced by labels (`rung1/hex1.hex1`) is fed to hex1: it gives back exactly
    the same program, so hex1's arithmetic and my counting agree on every jump;
  - both decoders agree with every named instruction (0 disagreements, `rung1/hex1.audit`);
  - controls: seed-0 refuses labels (it never learned them); missing, doubled and misplaced labels are
    refused with their own error codes.

## Rung 2: asm2, instructions by name (done)

- `rung2/table.s2`: the instruction table, plain text. One line per instruction: its name, the bits that
  never change, and where each operand goes, for example `= add.x 0x8b000000 r0 r5 r16` ("add, 64-bit:
  destination register at bit 0, first source at bit 5, second at bit 16"). Each line names its
  manual section. Anyone can read and check it without knowing how asm2 works inside.
- `rung2/asm2.hex1`: the assembler, 580 instructions written in hex1's language (with labels), built by
  hex1 (2464 bytes). It reads the table first, then a program written with those names and with labels
  of up to 16 characters, and writes the machine words. Mistakes stop it with the line number.
- Checks (`rung2/gate2.sh`, results in `evidence/SEED-0/rung2/`):
  - hex1 builds asm2; asm2 written in its own language (`rung2/asm2.s`) assembles back into exactly
    the same 2464 bytes, so the readable table and my hand-made words agree on all 580 instructions;
  - both decoders agree with every named instruction (0 disagreements, `rung2/asm2.audit`);
  - against the C encoder (the referee): every one of the 25 instruction forms it knows, 5,496 cases
    with registers and numbers at their limits and jumps out to the farthest distance each form can
    hold: asm2 and the C encoder give the same word every time (`encoder-compare.tsv`). There was no
    disagreement, so the manual did not have to settle one;
  - both refuse the same out-of-range numbers; each of asm2's error messages fires on a broken input.
  - Limit, said plainly: the farthest plain jump (`b`, about 128 MiB away) cannot be tested through a
    label because asm2's output buffer is 8 MiB; it is tested at its limits with the number written in,
    and through labels up to 1,048,575 words.

## Rung 3: om3, a translator for Omegese (done)

- `rung3/om3.s`: a translator from Omegese (the small program language the model writes, see
  `docs/one_layer/OMEGESE.md`) to machine words. It is 1,486 instructions written in asm2's language and
  built by asm2 (7,092 bytes, `rung3/om3.bin`). `rung3/table3.s2` adds 5 instruction lines to asm2's
  table, each naming its manual section.
- What it reads: one Omegese function. What it writes: that function's machine words, ready to be called
  the same way the existing C compiler's output is called (same rules for inputs, result and errors).
- The subset grew to what the 10 public practice tasks actually use: all nine number types, if/else,
  while with a limit, for, return, and add, subtract, multiply, divide, remainder, and/or/xor, shifts,
  comparisons, negate, not, and type conversions, each with the same error stops ("traps") as the C path:
  overflow of a signed number, division by zero, a loop that runs past its limit, a conversion that does
  not fit, a shift that is too far. It refuses, with its own error number, what the tasks do not use
  (signed division, calls, arrays, more than one function).
- How it builds code: every name gets its own register for as long as it exists. The C path keeps every
  value in memory and loads and stores it around each step (that is its documented design, OSC-1 design
  section 7, "no register allocation"), so a difference in word counts below measures that choice.
- Checks (`rung3/gate3.sh`, results in `evidence/SEED-0/rung3/`):
  - asm2 builds om3 twice to the same bytes; objdump (the referee) reads every one of the 1,486
    instructions back to the name, registers, numbers and jump targets written in `om3.s`:
    0 disagreements (`rung3/om3.audit`); five planted mistakes in the text are each caught;
  - the 10 public practice tasks, 80 test inputs in all, 9 of them ending in an error stop: the functions
    om3 makes give exactly the same result as the C path (`omegese emit`, `omega_emit`, the C back end),
    down to the raw 64-bit register, and the same as each task's expected answers. The same runner
    (`check/runner.c`, a referee) calls both;
  - every word om3 made is a real instruction; om3 refuses 14 broken or out-of-subset programs, each
    with its own error number.
- Words of machine code per task (counts only, no timing; `evidence/SEED-0/rung3/words.tsv`):

  | task | C path | seed chain |
  |---|---:|---:|
  | dev01 clamp_add | 58 | 17 |
  | dev02 absdiff | 48 | 16 |
  | dev03 popcount | 80 | 15 |
  | dev04 gcd | 71 | 20 |
  | dev05 checked_mul | 23 | 9 |
  | dev06 safe_div | 21 | 8 |
  | dev07 isqrt | 95 | 29 |
  | dev08 collatz_steps | 111 | 28 |
  | dev09 bitrev8 | 73 | 14 |
  | dev10 to_i8 | 20 | 8 |
  | total | 600 | 164 |

  Said plainly: the seed chain uses fewer words on every task, 164 against 600 in total (27%). Most of
  the gap is the C path's chosen design (every value goes to memory and back, see above), not a defect
  carried down from older tools.
## Rung 4: om4, the systems words a compiler needs (done)

- `rung4/om4.s`: om3 grown, feature by feature, into a translator that can build a real program. It is
  about 4,000 lines in asm2's language, built by asm2 (14,224 bytes, `rung4/om4.bin`); `rung4/table4.s2`
  adds 3 instruction lines for om4's own use, each naming its manual section; `rung4/mk4.sh` writes the
  file's own size into its header (asm2 cannot add up sizes).
- What it adds, and why each one is needed (every new word is used by the Omega compiler of rung R5 or
  by the test programs):
  - many functions, and a names pass so a call may name any function in the unit, earlier or
    later (a later function is a call, never a new scalar). Program mode may recurse mutually.
    Function mode refuses a call cycle, including a function calling itself, as error 9;

  - whole programs: a source with `fn main` comes out as a complete Linux program, file header laid
    out by hand, no linker; it runs, reads its input, writes its output and exits;
  - fixed pools (`global`): memory for the source text, the tables and the output, sized when the
    program is translated. There is no heap at all, by Drake's rule 24;
  - local arrays (`new`), array inputs to functions (`view` read-only, `edit` writable), `get`, `put`,
    `len`; every array carries its length and every index is checked. There is no `free`: nothing is
    ever given back by hand (rule 24); fm7 and fm8, shared with the C path whose language needs `free`,
    are given to om4 without their `free` lines;
  - `text`: a fixed piece of bytes (an error message, the keyword list), stored with its length;
  - `read`, `write`, `exit`: the three Linux calls a compiler needs (stdin in, stdout out, a status);
  - signed divide, remainder and shift, `true`/`false`, `for` with a computed count, `note` lines.
  The exact meaning of every new word, and which check tests it, is in `docs/one_layer/OMEGESE.md`
  section 9.
- Rules from the B-to-C lessons (Drake, decisions 16-24), and how om4 keeps them:
  - no terminator bytes: arrays and texts know their length (om4 itself no longer looks for an end
    byte in its input); an array written without its length is refused;
  - no pointers: you can only index an array, and the index is checked;
  - no hidden sharing: the same array cannot be passed twice in one call, and a global pool is never
    passed at all, so two names never point at the same memory;
  - no undefined behaviour: unsigned numbers wrap, signed numbers stop with an error ("trap") when they
    overflow, division by zero traps, a bad index traps, running out of stack traps; the spec has no
    "undefined" in it (the gate checks with a search);
  - no automatic conversions: mixing two types in one operation is refused.
- Checks (`rung4/gate4.sh`, results in `evidence/SEED-0/rung4/`), 217 cases, all pass:
  - asm2 rebuilds om4 to the same bytes; objdump (the referee) reads all 3,170 instructions back to
    what `om4.s` says (0 disagreements); five planted mistakes in the text are each caught;
  - the 10 practice tasks: om4 makes exactly the same machine words as om3, and the same results as the
    C path (80 inputs) and the expected answers;
  - 9 new programs in the old calling form that the C path can also express (signed divide and
    remainder at their limits, signed shifts, calls, computed loop counts, local arrays, a bad index):
    om4 and the C path give the same result or the same trap on all 69 inputs;
  - 4 whole programs against twins written in C and built by gcc (gcc as the referee only): recursive
    Fibonacci, sorting 1,000 numbers, echo of 50,000 random bytes, a byte copy that reverses its input:
    same output every time;
  - 14 whole programs that must stop with a given status or value (among them 2^64 - 1 and 2^63 read as
    ulong numbers, fixed after the independent interpreter L412 found om4 refused them): divide by zero, bad index, read past the end, loop
    limit, bad conversion, overflow, stack used up, `exit`, and a void call: each stops as written;
  - 41 broken or forbidden programs are refused, each with its own error number, among them a value
    nested 1,025 deep (om4 used to crash on very deep nesting; it now stops at 1,024 with error 14);
  - a copy of om4 with one instruction word changed (signed divide made unsigned) gives different
    results, so the checks would notice such a slip.
- Machine words per program, where the C path can express it (`evidence/SEED-0/rung4/words.tsv`):
  the 10 practice tasks are the same 164 words as om3 (C path 600). The 9 new programs: C path 552,
  om4 267. One is larger than the C path: the program with three calls (79 against 59), because each
  call saves and restores every register in use.
- Limits, said plainly: variable names are at most 8 characters; a function may use at most 16
  registers for its names and working values (om4 stops with error 12 beyond that). `omegese next` (the
  C tool that lists the legal next words) does not know the new words, so the next-word check for them
  is our own (decision 22, gate4 section 10, `check/nextfuzz.sh`): 20 random programs using every new
  word are generated (`check/nextgen.awk`) and all accepted; then each is cut after every word and has
  each word swapped for a wrong one, 4,860 inputs, and om4 answers every one with its result or an error
  number, never a crash.


## Rung 5: Atlas reborn (done)

- `atlas/atlas.s2`: the Atlas boot image (the first program the emulated machine runs, which checks the
  next stage's fingerprint before handing over), written line for line from `~/workspace/atlas/atlas.s`
  and its SHA-256 code in asm2's language. The way the old linker placed the pieces (code at 0, messages,
  fingerprint, constants, the SHA-256 code from byte 384, its table at byte 1224) is written out by hand
  in the same file. `atlas/table5.s2` adds 28 instruction lines, each naming its manual section.
- Built by asm2, with no GNU assembler and no linker: `atlas/atlas.bin`, 1,480 bytes.
- Checks (`atlas/gate5.sh`, results in `evidence/SEED-0/rung5/`):
  - asm2 builds it twice to the same bytes, and it is byte for byte the audited Atlas image
    (sha256 f7802501...a5): no difference, so the manual had nothing to settle;
  - objdump (the referee) reads all 263 instructions back to what `atlas.s2` says: 0 disagreements;
    five planted mistakes are caught;
  - the existing Atlas M1 tests (`run_m1_gates.sh`, not edited) pass on a scratch copy of the Atlas folder
    holding the asm2-made image: the SHA-256 known answers, the static audits, and in QEMU the boot
    (AWAKEN, VERIFY, HANDOFF) and the refusal of every corrupted next stage. `~/workspace/atlas` was
    only read.

## Rung R5: omc, the Omega compiler written in Omega (done)

- Born from the seed, not from C: `make omega-from-seed` (or `sh seed/omega-from-seed.sh`) turns
  the hand-written `seed0.hex` into seed0 with the one named outside step (`load0.sh`, POSIX printf),
  then seed0 builds hex1, hex1 builds asm2, asm2 builds om4, om4 builds omc, and omc builds itself
  twice. The two self-builds must be the same bytes. The compiler is left at `seed/out/omc`, with
  its sha256 beside it. That file is the Omega compiler. Nothing in that path is C.

- `omc/omc.om`: the compiler itself, written in Omegese, using only words om4 understands. It reads a
  program on its input and writes a ready-to-run Linux program (or, without `fn main`, bare machine
  words) on its output, with the file header laid out by hand and no linker. 3,212 lines, 125 functions.
  `omc/genkw.awk` once wrote the keyword chain into `omc.om`; the committed `omc.om` is the source,
  and the seed path does not run awk.
- It is meant to do exactly what om4 does, word for word: the same machine words, the same error
  numbers, on the same line. That is on purpose: om4 then becomes a strict referee for omc, because any
  difference at all is a mistake. It is checked on every input listed below, not on every possible one.
- How it is built: om4 translates `omc.om` into a program (stage 1, 76,748 bytes). Stage 1 then
  translates `omc.om` again (stage 2), and stage 2 translates it a third time (stage 3).
- The few places where the language shaped the design:
  - a names pass reads every signature first, so a call may name a function written later.
    Reading a value is still one function that calls itself, because a value contains values;
  - every working number is kept as a plain 64-bit value, because the language refuses mixed types;
  - every loop over the input is two loops inside each other. A single loop stops itself after a
    million passes, and om4 has no such limit on input size.
- Checks (`omc/gate5.sh`, results in `evidence/SEED-0/omc/`), 177 cases, all pass:
  - (a) self-rebuild: stages 1, 2 and 3 are the same file (sha256 191b776e...98e9), so omc translates
    itself the same way om4 translates it;
  - (b) a second route: the C path (the older compiler written in C) cannot read `omc.om`. It lacks
    global pools, a function calling itself, arrays over 64 cells, text, `read`/`write`, `exit`, and a
    colon inside a note. Also it makes a file for a test runner, not a program. The gate tries each of
    these and requires the refusal to name that word, so it notices if the C path learns one. So, as the brief allows, the two parents
    are om4 and omc itself: both build the same omc. This is weaker than a truly independent route,
    because one author designed both;
  - om4 and stage 2 agree on 153 inputs: every input gate4 gives om4, every Omegese file in the repo,
    and `omc.om`. They agree on output, exit status, and the error number and line of all 57 refusals;
  - what that list missed, found by an outside review: no program in it wrote out a text successfully,
    and error numbers 1, 2, 4, 11, 12, 13 and 14 never came up. Now a program writes a text with every
    escape and must give fixed bytes and a fixed exit value (204, worked out by hand, not taken from om4),
    and each of those errors has its own case. The gate then checks that every error number from 1 to 23
    comes up somewhere, from both om4 and omc;
  - the next-word fuzz (decision 22, below): om4 and omc give the same answer on 4,860 cut or altered
    programs;
  - (c) the whole of gate4 run with stage 2 in om4's place passes (116 checks): the 10 practice tasks
    and 9 programs give the C path's results and traps; the 4 whole programs match their gcc-built
    twins; its refusals give the same numbers. Table: `evidence/SEED-0/omc/corpus-words.tsv`;
  - (d) planted mistakes. A changed word that omc itself uses (`or` made into `and`) breaks the
    self-rebuild and the comparison with om4. A changed word omc does not use (signed divide made
    unsigned) keeps the self-rebuild, which shows a self-rebuild alone proves too little, but the om4
    comparison catches it. The same wrong word put into both om4 and omc fools both of those checks,
    and only the C path catches it (fm1, fm2: -7 / 2). A broken program is refused with error 3;
  - the output does not change with the run: same bytes under another environment and a second later.
- Size and speed (`size.tsv`, `size-history.tsv`, `omc-words.tsv`, `compile-time.tsv` in `evidence/SEED-0/omc/`;
  `size-history.tsv` gets one row per self-rewrite, and the gate fails if the current omc has no row):
  - omc is 19,143 machine words; the largest functions are the value reader (2,073 words) and local
    arrays (695);
  - on the programs the C path can also translate, omc makes the same words as om4. The 10 practice
    tasks take 164 words (C path 600); the 9 others take 267 (C path 552). The one larger than the C
    path is still fm5, the program with three calls (79 against 59);
  - time to translate one program (10 practice tasks and 9 others), 200 runs each, including starting it:
    omc 0.4 to 0.6 ms, the C path 5.9 to 6.7 ms (it runs two programs and writes a file in between).
    omc translates itself in 2.6 ms. These are not compared against the machine's limit (not measured).
- Limits, said plainly: omc has om4's limits, because it copies om4 (names up to 8 characters, 16
  registers per function, values nested up to 1,024 deep, 8 MiB of input).

## Rung R6, first piece: structs (done for the language; GPU words still open)

om4 does not learn structs. It remains the parent that rebuilds omc, and omc.om does not use the new
words, so the two parents still build the same compiler. A struct program is a stack of 8-byte cells,
given back when the function returns. There is no `free`.

- `seed/omc/gate6.sh`, 26 cases, RC=0. The compiler built by om4 and the compiler built by itself are
  the same file, sha256 01b1545b23e8ff40cb5691e20b5df910ea4acaefde3495f0d62d270465b98cd6 (93,184 bytes).
  omc.om is 3,772 lines, 149 functions.
- Twelve programs run and exit with the value worked out in Wolfram (`evidence/SEED-0/r6-structs/wolfram.txt`),
  not copied from this compiler. Three of them (t7, t8, t9) would change that value if a field were
  stored at the wrong width. A one-word field swap changes the exit from 20 to 22, so a gate that
  ignored field names would fail.
- Thirteen broken programs are refused with the stated error number. om4 itself refuses a struct
  program (exit 16). `free` is still refused.
- Not in this piece: a kernel on the chip. That stays on L408. The experimental single-function
  OSC IR emitter from `fix/seed-ir-trap` is now part of this line; see rung R8.

## Rung R6b: one compiler line, structs and native host operations (done)

Structs and the native host extension were built on separate branches from R5. They are now one
compiler: `seed/omc/omc.om`, 4,616 lines and 173 functions, accepts both. The keyword table keeps
both islands with no id shared (struct 85; host words 100 to 131).

- self-rebuild: stage1 = stage2 = stage3, sha256
  `1a25cf0be96972206d6904638ded6729c65b69a12c788d7fe370e6dadd22e533` (126,528 bytes).
- `seed/omc/gate6.sh` passes its 26 struct cases; struct programs stay omc-only (om4 error 16).
- `seed/host/check.sh` passes every host case and the 70 original corpus fixtures. The om4/omc
  corpus leaves out `seed/omc/tests/struct/`: om4's refusal there is the documented om4-only limit
  (decision 32), and gate6 owns that check.
- `seed/host/tests/structfile.om` is one program that builds a struct, creates and writes a file
  through the native host words, closes and unlinks it, and returns a struct field; `gate.sh` runs it.

## Rung R7: break and trap (done)

Section 3 listed both words while the compiler refused them (keyword 77). omc now implements them
(86 and 87) with the following semantics, and `seed/omc/gate7.sh` checks each one (11 cases plus the
om4 refusal); `seed/host/check.sh` runs gate7.

- `break` leaves the innermost `while` or `for`. Outside a loop it is error 9. Like `ret` it ends the
  path, so a later line in the same block is error 17. Its branch is recorded with the loop's block
  index, patched when that loop closes, and marked spent, so a later loop reusing the slot cannot move
  an earlier break. 1024 sites per function (error 14).
- `trap N` takes 1..14 (error 8 out of range, error 4 when the line ends, 16 when the token is not a
  number). Program mode exits 100 + N; function mode calls `OscRt.trap` through the same stub the
  runtime checks use. A later line in the same block is error 17.

## Rung R8: OSC IR output, one line (experimental checkpoint)

`fix/seed-ir-trap` added a leading `ir` line that writes OSC IR version 1 for one function instead of
AArch64 words. It branched from R6, so it lacked the native host words and break/trap. It is now part
of the same compiler: `seed/omc/omc.om`, 5,395 lines and 209 functions, sha256
`2b029a217e8f2d033b484dec3a5a24570370b8a139157f4204358e226d1e5bd9` (149,904 bytes).

- `seed/omc/gate-ir.sh` passes: 10/10 dev programs are byte-identical to the C-built `omegese` +
  `omega_emit` oracle, `checker2` ACCEPTs every unit, and the mul-to-add mutant differs. The C tools
  are referees only.
- IR mode is still one function and scalar values. It refuses `struct`, the native host words and
  `break` with error 3, because none has an IR lowering yet. Multifunction IR, struct IR and refusal
  hardening remain open.
- `seed/host/check.sh` runs gate-ir only when the three oracle binaries exist, and prints `SKIP`
  with the build command otherwise; the compiler itself still needs no C tool.

## Rung R9: f32 and f64 (done)

IEEE-754 binary32 and binary64 are now part of the language, written in omc (`f32`, `f64`,
`bitcast`, decimal literals, arithmetic, comparisons and conversions). A float value is its bit pattern in an
ordinary integer register; arithmetic moves it to v0/v1, runs one scalar FP instruction and moves it
back, with FPCR untouched and no fused operation.

- `seed/omc/omc.om`, 6,241 lines and 233 functions, sha256
  `f769b4a8c2ecd9006ade38b5d10c6d3e12bd3044b7418a3a87b6b4e2731c824a` (169,276 bytes).
- `seed/omc/gate8.sh` passes 21 cases: exact bits for 1.5, 0.1, pi, 1e-3 and -0.0 in both widths;
  arithmetic; IEEE comparisons including NaN; fsqrt, fabs, fmin and fmax with minNum/maxNum and -0.0
  ordering; int<->float and f32<->f64 conversions with saturation; float arrays, pools, parameters,
  returns and struct fields; and refusals for mixed types, integer intrinsic operands, ambiguous
  literals, float indices, float `for` counts, `read`/`write` on float arrays, malformed literals,
  out-of-range literals and IR mode. om4 refuses float syntax, and the gate checks that.
- Limits, said plainly: the significant digits of a literal must fit in 2^63-1 (error 8 beyond
  that); the decimal scale is unlimited, and the full IEEE range is converted exactly, subnormals
  included. A literal that rounds to infinity is refused (error 8); underflow gives signed zero.
  Struct fields may be floats (an f32 field still uses an 8-byte cell). IR lowering of floats is not
  implemented and is refused, not miscompiled.

## Rung R10: grouping parentheses (done)

The design says brackets are optional and separate words; the lexer did not implement that, so
`(5)` was read as one unknown name. `tok2` now emits `(` and `)` as one-character tokens even when
they touch a value, and the existing group path returns the inner value with no code.

- `seed/omc/omc.om`, 6,252 lines and 233 functions, sha256
  `89887ff473e89f870f8f1dbd1e2b46f008ad076ef168480238ac1ec1bc3e557a` (169,428 bytes).
- `seed/omc/gate9.sh` passes 4 cases: grouped integers, nested groups, a grouped float expression,
  the still-ambiguous `x = (5)` (error 11) and an unclosed group (error 16).
- No existing program changes meaning: the om4/omc byte-equality corpus continues to pass with the
  paren fixtures excluded, since om4's lexer does not split them.

## Rung R11: full-range decimal literals (done)

The first float slice converted `N * 10^p` only while both factors fit u64 (scale at most 18). The
converter now keeps a normalized 64-bit significand, a binary exponent and one sticky bit
(`fmul10`/`fdiv10`), rounds once to nearest even (`froundsh`) and packs with subnormal handling
(`fpack`). No 128-bit integer and no double rounding are needed.

- `seed/omc/omc.om`, 6,259 lines and 234 functions, sha256
  `7453603d943a5bb75e85b672bbbbd200abfe12a38b040b8521cf00a2beac29ef` (169,636 bytes). The restricted
  long-division converter and its state are removed, not left beside the new one.
- The expected bits for 50 literals were produced with `strtod`/`strtof` outside this compiler and
  matched exactly: 0.1, pi, 6.02214076e23, 1.602176634e-19, 1e308, 5e-324, 1e-45, the maximum finite
  values, values just below the overflow boundary, and values that underflow to zero.
- `gate8` now has 23 cases: the two literal fixtures cover large and subnormal values, and
  `neg_overflow2`/`neg_overflow3` check that a literal which rounds to infinity is refused.

## Rung R12: layout queries (done)

`sizeof T` / `sizeof S` and `offsetof S f` are compile-time constants of type ulong over the same
8-byte-cell layout that `new`/`get`/`put` use. They add no code and compose in expressions, which is
the primitive that structure formats and the future C ABI work need in order to describe a layout
without hard-coded numbers.

- `seed/omc/omc.om`, 6,315 lines and 236 functions, sha256
  `1912bef8638c39ef8730d2ec3bddff3a9169f00f21613b5f5e68a55375847f34` (171,076 bytes).
- `seed/omc/gate10.sh` passes 5 cases: every scalar size, a struct with scalar and array fields
  (size 40, offsets 0/8/16), composition in an expression, and refusals (unknown type 16, unknown
  struct or field 3, `sizeof void` 9).

## Rung R13: keyword compatibility (done)

The first float slice spelled its words `bits`, `sqrt`, `abs`, `min` and `max`. Auditing the 103
`.om` files of the benchmark corpus against the pinned compiler (`e5d60db8…`) showed seven refusals:
existing code uses `bits` as a parameter or variable in six files and `min` in one. A keyword may not
take a name that existing code uses, so the float words are now `bitcast`, `fsqrt`, `fabs`, `fmin`,
`fmax` (ids unchanged).

- Re-running the same audit after the rename: 103 files, 0 output differences, 0 crashes against the
  pinned compiler, so every existing program still compiles to identical machine code.
- `seed/omc/gate11.sh` pins the rule: `bits`, `min`, `max`, `abs` and `sqrt` are usable as names
  (`names.om`, exit 10), while `bitcast` is a keyword (`neg_reserved.om`, error 16).
- `seed/omc/omc.om`, 6,315 lines and 236 functions, sha256
  `c7f4fc4fac585653226468251ca86e1900ff7f51e9f731687cc2e176702c7f8d` (171,088 bytes).
- The audit method and result are recorded in `evidence/SEED-0/keyword-compat/README.md`.

## Rung R14: continue (done)

`break` had no counterpart. `continue` now finds the innermost loop like `break`, but its branch is
patched to the loop's latch instead of its exit: the head of a `while`, so the condition and the
hidden bound count run again, or the increment of a `for`, so the counter still advances. It uses the
same fixup discipline (recorded with the loop's block index, patched when that loop closes, then
marked spent) and the same 1024-site limit per function.

- `seed/omc/gate7.sh` now has 18 cases: while (18), for (12), nested (36), an empty `for` body that
  still terminates, a bounded while that still traps 4 (exit 104), and refusals (error 9 outside a
  loop, error 17 for a later line). om4 refuses the word, which the gate also checks.
- Auditing the benchmark corpus again after adding the keyword: 103 files, 0 output differences,
  0 crashes against the pinned compiler.
- `seed/omc/omc.om`, 6,374 lines and 237 functions, sha256
  `068a9675ca20699433cd51b468703012cdbc12cfd5ea0e9204a1d0dc1ca7544e` (172,296 bytes).

## Rung R15: benchmark parity audit (evidence)

The current compiler was checked against the benchmark-pinned compiler over the real benchmark
workspace, not just this repository's fixtures:

- 103 authored `.om` sources: 0 output differences, 0 crashes.
- 53 `om/build.plan` targets (each a concatenation of 2 to 21 units, including `prime_race`, the
  JSON/measure/stream tools and every test target): 0 differences, 0 crashes.
- `prime-reference` runtime behaviour on six requests (two primes, a pseudoprime, two counts and a
  raw bitmap): identical stdout, stderr and exit status.

Method and per-scope counts: `evidence/SEED-0/benchmark-parity/README.md`. The corpus is external,
so this is evidence rather than a gate.

## How the checks stay honest

- `check/a64check.c` and `check/audit.sh` are reference checks only. They call the repo's existing C
  decoder and encoder and GNU objdump to compare; they never write a byte of any rung.
- `check/audit2.sh` (GNU objdump) and `check/runner.c` (built with the C compiler) are reference checks
  for rungs 3 and 5 in the same way: one reads every instruction back, the other calls the functions
  both paths made and compares the answers. Neither writes a byte of any rung.
- `evidence/SEED-0/rung3-worksheet.sh` and `rung5-worksheet.sh` are the arithmetic behind the keyword
  table of om3 and the message bytes and constants of Atlas, kept so anyone can redo the sums.
- `evidence/SEED-0/rung0-worksheet.sh` is the arithmetic I did while writing the words: each manual field
  layout with the numbers put in. It is kept so anyone can redo the sums. It caught one typing slip of
  mine (word 26) before the first run.
