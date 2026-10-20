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
  encoder, so the edge-case check covers their instruction forms, but not those exact programs. (The listing
  tool can reach the elementwise ones when it is built together with the GPU library; this run did not.)

## Results (gate run 2026-10-20 02:28 UTC, `evidence/SEED-0/sm121/gate6.log`)

- **Every program matches.** The C generators make 6,091 different GPU programs over the sweep (2,398,274
  instructions, 2,419,440 counting the padding at the end of each program). `asm2` with `sm121.tbl` turned
  every listing into exactly the C code's words, bit for bit. Zero differences between the table and the C code.
- **Every instruction form at the edges matches**: 65,352 cases, bit for bit.
- **The vector add**: the hand-written listing becomes 512 bytes identical to the launcher's vector add, and
  the same twice in a row.
- **The controls work**: a wrong register, a wrong offset, a register number too large, one wrong bit in the
  table and a field moved by one bit are each caught.
- **On the chip** (2026-10-20 03:02 UTC, `evidence/SEED-0/sm121/chip/`): the vector add `asm2` made ran on the
  GB10 through the bytes tool and our own launcher, no CUDA library: 1,048,576 additions, 0 wrong. The
  control (the same program with one word changed) got all 1,048,576 wrong and failed, as it must. The run
  record says `dirty=3`: those are the bytes tool copied in from its own branch (ledger L098, not part of
  this branch) and the chip folder being written; the program bytes are `seed/sm121/vecadd.bin` as committed
  (fingerprint 39f3dfdf...). Receipt: `chip/receipt.json`.

### What the check found in our C code (not in the table)

1. **A crash in the tensor-core matrix multiply generator.** It accepts "4 row tiles, 1 warp", then writes 16
   entries into lists that hold 8 and overwrites memory. The 48 settings that do this are skipped here and
   named in `notes-summary.tsv`; a fixer agent is repairing the generator (ledger L149).
2. **Two labels swapped in the header.** The instruction listed as "uncached load" is really the evict-first
   load, and the one listed as "evict-first store" is really the uncached store. The encoder's own comments
   and NVIDIA's disassembler agree on what the bits mean, so programs behave as the encoder intends; only the
   labels in `src/omega_blackwell_codegen.h` are wrong.
3. **A compare that is unsigned where the name suggests signed.** The register-against-uniform form of
   "greater or equal" compares as unsigned numbers. No program uses that form today, so nothing is affected yet.
4. **Some numbers are taken raw, unchecked.** For shared-memory loads and stores the number is the size field
   itself, and for the floating-point compare it is the whole condition word. The C code accepts any value,
   including ones that are no real size or condition (the disassembler shows them as other sizes or refuses
   them). Real programs only use valid values (32-bit and 128-bit shared-memory accesses).
5. **Two instructions ignore a requested schedule.** For IADD3 and EXIT the C code always writes its own
   scheduling bits, whatever the program asked for (`notes-summary.tsv`); the table does the same.

### The disassembler check (reference only)

NVIDIA's disassembler (`nvdisasm` 13.0, used only to read our bits back, never to make any) read 45,024 of the
65,352 edge cases. It refused 20,328: 20,104 of them have the scheduling word set to all ones on purpose, and
224 are the floating-point compare with a made-up condition word (item 4). Of 68 instruction forms, 59 got the
same name from the disassembler as from our C code; the 9 others are the items above plus naming detail
(for example `IMAD.MOV` is how the disassembler shows a multiply-add whose inputs make it a plain copy).
