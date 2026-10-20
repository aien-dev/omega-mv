# seed/sm121/: rung 6, GPU words from the same chain

The ladder in `seed/` (see `seed/README.md`) grows a toolchain from a few hundred hand-written bytes. Rung 2 is
`asm2`, a small assembler: it reads a plain-text table of instructions, then a program written with those names,
and writes machine words. Until now it only knew the main processor (Arm). This rung teaches the same `asm2`,
unchanged, the instructions of the GB10's graphics chip (NVIDIA calls this generation sm_121), by giving it a
second table. Nothing new was compiled to do it: the new part is text.

## What is here

- `sm121.tbl`: the GPU instruction table, plain text, same format as `rung2/table.s2`. A GPU instruction is
  128 bits long, four times an Arm instruction, and `asm2` writes 32 bits per line, so each GPU instruction is
  four lines, one per quarter. For example `ldg.0 2 2` / `ldg.1 4` / `ldg.2` / `ldg.3` is
  "load from memory into register 2, address in register 2, memory descriptor in uniform register 4".
  Every line says which line of our own C code generator (`src/omega_blackwell_codegen.c`) it was read from.
- `vecadd.s2`: the GB10 vector add (`out[i] = a[i] + b[i]`), the kernel the existing launcher has run on the
  chip since the first GPU cut, written by hand with the table's names. `vecadd.bin` is what `asm2` makes of it.
- `gate6.sh`: the checks (below). `names.tsv`: the name our C code gives each instruction form, for the
  disassembler check.
- `sm121list.c`: a reference tool. It is built from our C code generators and is only a referee: for every GPU
  program those generators can make, it writes the program as a text listing in `asm2`'s language and, next
  to it, the bytes the C code made. It never writes a byte under `seed/`.

## How the checks work

The C code generator is the referee, never an input to the table: I read its source and wrote the table by
hand; the gate then compares.

1. Every program our C generators make today, over a sweep of their settings (sizes, warp counts, depths,
   and the deliberately broken "mutant" versions the tests use), is written out as a listing; `asm2` with the
   new table turns each listing into words; the words must equal the C code's words, bit for bit.
2. Every instruction form, with registers and numbers at the edges of their fields, the same way.
3. The hand-written vector add must equal, byte for byte, the words the existing launcher runs.
4. Controls: a wrong register, a wrong offset, an out-of-range register, a wrong bit in the table and a field
   in the wrong place must each be caught.
5. Reference only: NVIDIA's disassembler (`nvdisasm -b SM121`) reads every instruction form back, and its
   name is compared with the name our C code gives it. Every difference is listed.
6. On the chip: the seed-made vector add runs on the GB10 through the existing launcher (no CUDA library),
   with a receipt.

Results: `evidence/SEED-0/sm121/`.

## Limits, said plainly

- Numbers in a listing are written the way the bits hold them. For example a constant-bank offset of 0x380
  bytes is written 224 (0x380 / 4) for `LDC` and 112 (0x380 / 8) for `LDCU`, and a branch distance is written
  as its two pieces (low 6 bits, the rest), because the instruction stores it split. The listing writer does
  that arithmetic, as a person would; `asm2` itself has no GPU-specific code.
- There are no GPU labels: `asm2`'s label arithmetic counts 32-bit words from the line it is on, while GPU
  branches count 128-bit instructions from the next instruction. Branch distances are written as numbers.
- The scheduling word (the fourth quarter: stall counts and waits) is part of the program, like in NVIDIA's
  own listings. The table holds the C code's default for each instruction; any other value is written out
  with `ctl`.
- The kernel sweep covers the settings ranges each generator documents; settings a generator refuses are
  skipped. Four small programs are built inside other files' private functions (attention, elementwise,
  command list, resident runtime) and are not reachable without changing those files; they use the same
  encoder, so the edge-case check covers their instruction forms, but not those exact programs.

RESULTS_PLACEHOLDER
