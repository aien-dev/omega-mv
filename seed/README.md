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

## How the checks stay honest

- `check/a64check.c` and `check/audit.sh` are reference checks only. They call the repo's existing C
  decoder and encoder and GNU objdump to compare; they never write a byte of any rung.
- `evidence/SEED-0/rung0-worksheet.sh` is the arithmetic I did while writing the words: each manual field
  layout with the numbers put in. It is kept so anyone can redo the sums. It caught one typing slip of
  mine (word 26) before the first run.
