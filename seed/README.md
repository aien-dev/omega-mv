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
  about 4,000 lines in asm2's language, built by asm2 (13,648 bytes, `rung4/om4.bin`); `rung4/table4.s2`
  adds 3 instruction lines for om4's own use, each naming its manual section; `rung4/mk4.sh` writes the
  file's own size into its header (asm2 cannot add up sizes).
- What it adds, and why each one is needed (every new word is used by the Omega compiler of rung R5 or
  by the test programs):
  - many functions, calls and recursion (a compiler is many small jobs; reading nested values is a
    function that calls itself);
  - whole programs: a source with `fn main` comes out as a complete Linux program, file header laid
    out by hand, no linker; it runs, reads its input, writes its output and exits;
  - fixed pools (`global`): memory for the source text, the tables and the output, sized when the
    program is translated. There is no heap at all, by Drake's rule 24;
  - local arrays (`new`), array inputs to functions (`view` read-only, `edit` writable), `get`, `put`,
    `len`; every array carries its length and every index is checked;
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
- Checks (`rung4/gate4.sh`, results in `evidence/SEED-0/rung4/`), 195 cases, all pass:
  - asm2 rebuilds om4 to the same bytes; objdump (the referee) reads all 3,026 instructions back to
    what `om4.s` says (0 disagreements); five planted mistakes in the text are each caught;
  - the 10 practice tasks: om4 makes exactly the same machine words as om3, and the same results as the
    C path (80 inputs) and the expected answers;
  - 9 new programs in the old calling form that the C path can also express (signed divide and
    remainder at their limits, signed shifts, calls, computed loop counts, local arrays, a bad index):
    om4 and the C path give the same result or the same trap on all 69 inputs;
  - 4 whole programs against twins written in C and built by gcc (gcc as the referee only): recursive
    Fibonacci, sorting 1,000 numbers, echo of 50,000 random bytes, a byte copy that reverses its input:
    same output every time;
  - 9 programs that must stop with a given status: divide by zero, bad index, read past the end, loop
    limit, bad conversion, overflow, stack used up, `exit`, and a void call: each stops as written;
  - 33 broken or forbidden programs are refused, each with its own error number, among them a value
    nested 1,025 deep (om4 used to crash on very deep nesting; it now stops at 1,024 with error 14);
  - a copy of om4 with one instruction word changed (signed divide made unsigned) gives different
    results, so the checks would notice such a slip.
- Machine words per program, where the C path can express it (`evidence/SEED-0/rung4/words.tsv`):
  the 10 practice tasks are the same 164 words as om3 (C path 600). The 9 new programs: C path 552,
  om4 267. One is larger than the C path: the program with three calls (79 against 59), because each
  call saves and restores every register in use.
- Limits, said plainly: variable names are at most 8 characters; a function may use at most 16
  registers for its names and working values (om4 stops with error 12 beyond that); `omegese next` (the
  C tool that lists the legal next words) does not know the new words yet, so its fuzz test does not
  cover them yet (owed, see the handoff).


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
