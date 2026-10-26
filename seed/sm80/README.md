# seed/sm80/: a second GPU table for asm2 (Ampere sm_80/86/87, Ada sm_89)

`seed/sm121/` taught `asm2` (rung 2, unchanged) the GB10's instructions with a plain-text table. This directory
does the same for an older NVIDIA generation: Ampere (A100 is sm_80; RTX 30xx and A-series workstation cards
are sm_86; Jetson Orin is sm_87) and Ada (RTX 40xx, L4, L40 are sm_89), which share one instruction encoding.
Nothing was compiled to add it. The new part is text, plus a check.

Status: IMPLEMENTED and host-verified against Mesa NAK's encoder. No sm_80/86/87/89 GPU has run any of it
(NOT_RUN). The hardware needed is listed at the end.

## What is here

- `sm80.tbl`: the instruction table, same format as `seed/sm121/sm121.tbl` (`= name 0xBASE field...`). Each
  instruction is four 32-bit lines: `.0` opcode, guard predicate, destination, first source; `.1` second source
  or memory fields; `.2` modifiers and third source; `c` the scheduling word (stall, yield, write scoreboard,
  read scoreboard, wait mask, reuse). Unlike sm121.tbl, the guard predicate and every scheduling field are
  operands, so no `ctl` override is needed.
- `vecadd.s2`: the vector add (`c[i] = a[i] + b[i]`), written by hand with the table's names. It is the GB10
  kernel from `seed/sm121/vecadd.s2` moved to sm_80 forms: `ULDC` instead of `LDCU`, and global memory as
  `[R.64]` with no uniform descriptor. `vecadd.bin` is what `asm2` makes of it (320 bytes, 20 instructions).
- `gate80.sh`: the checks (below). Results go to `evidence/SEED-0/sm80/`, which is not committed.
- `vectors/`: the referee's output. `edges.s2.gz` is a listing in the table's language (13,889 cases, 124,066
  instructions). `edges.nak.bin.gz` holds the bytes NAK writes for the same instructions. `cases.tsv.gz` maps
  each case to its first instruction, form, kind and operands. `vecadd.nak.bin` is NAK's bytes for the vector
  add. `sched80.tsv` holds NAK's own schedule for the vector add, for reference. `SHA256SUMS` covers the
  uncompressed files.

## Sources

Every base word and field was read by hand from Mesa NAK, `src/nouveau/compiler/nak/sm70_encode.rs` at mesa
`02f13375231bf0a9ed20b09cb21171351053be52`. NAK is MIT-licensed, Copyright (c) 2022 Collabora, Ltd. No code was
copied: the table is field positions and opcode numbers, written in this repo's own format. No NVIDIA compiler,
assembler or disassembler was run to write or check any of it, and no NVIDIA binary was read.

## The referee: nakref

The vectors come from `nakref`, a small Rust program kept outside this repo. It links the pinned NAK sources
unmodified and uses NAK as a library: for each case it builds NAK's own IR instruction and calls NAK's encoder
(`ShaderModelInfo::encode_shader`). It writes the asm2 listing for the same instruction next to NAK's bytes.

Each listing field is turned into NAK's own enum (comparison, rounding mode, memory order and so on) and NAK
encodes it, so a table that puts the right number in the right place under the wrong meaning fails. Operand
combinations NAK refuses, or that are not valid instructions (misaligned register pairs, reserved encodings),
are not emitted: 11,244 were refused and none of them came from a NAK assertion. Cases per form:

- every guard predicate (0..6, PT, and their negations);
- each field at low, high and default, at low + 1 and high - 1, and at powers of two and their neighbours;
- each scheduling field at its edges;
- branch distances 0, 1, 2, 3, 7, 63, 64, 1000, 4095, -2, -3, -64, -1000 and -4096;
- 64 random valid combinations, using a fixed seed.

The program and its build files are in `/home/drakestapleton/handoffs/CudaArch/nakref` (oracle `main.rs`
sha256 `6ca07b6da0bfc219e8237c05becfaac24c2a0e09dd4bb355a925ce2526bca6d0`). Whether it should live in this
repo is open. It is Rust, and seed referees are .om
or the existing C precedent (`seed/sm121/sm121list.c`).

The same oracle shows that NAK's bytes for all 124,066 instructions, and for the vector add, are identical at
sm_80, 86, 87 and 89. sm_90 (Hopper) differs: NAK changes float atomics and other forms from sm_90 on
(`sm70_encode.rs` 1183, 3799, 3828, 4537, 4593). This table does not cover sm_90.

## How the table was checked, and what was corrected

The table was written once, from reading the source, before any comparison. The first comparison found 136
differing words out of 496,264. Each one came from the oracle, not the table: NAK rewrites four operand
choices into a different encoding.

- An immediate 0 becomes the zero register, which uses the register form.
- `URZ` as a uniform source uses the register form with `RZ`.
- `ULDC` with no destination becomes `LDC` to `RZ`.
- `ATOMG` with no destination becomes `RED`.

The oracle now builds those operands explicitly (an `Imm32(0)`, UGPR 63, GPR 255), so the listing keeps the
encoding it names. No table line changed. After that, all 124,066 instructions were bit-identical.

## Checks (`sh seed/sm80/gate80.sh`, from the repo root)

1. The vectors match `vectors/SHA256SUMS`.
2. `asm2` with `sm80.tbl` assembles the edge listing. Every word must equal NAK's, bit for bit.
3. `vecadd.s2` assembles twice to the same bytes, and those bytes equal NAK's for the same instructions.
4. Controls, each of which must change the output: a wrong register, a wrong constant offset, an out-of-range
   register (`asm2` error 8), a flipped table bit, a field moved one bit, and a moved field in a form only the
   edge listing uses (SHFL with two immediates). As a whole-gate negative control, flipping one bit of
   `isetp.2`'s base word makes GATE80 FAIL with exit status 1.

## The vector add's schedule

The scheduling words are the ones the GB10 ran in `seed/sm121/vecadd.s2`, carried over. `sched80.tsv` sets them
beside the schedule NAK's own pass gives the same instructions (`calc_instr_deps` with `SM80Latency`). Every
stall is at least NAK's, and every scoreboard NAK waits on before a read is also waited on here, except in two
places, where this kernel takes NAK's stricter words:

- The store holds its registers on read scoreboard 0 with stall 2, where the GB10 kernel used stall 1 and no
  read scoreboard.
- The final `EXIT` waits on that scoreboard.

Total stall cycles are 65 here and 48 in NAK's schedule. Whether the GB10 schedule is also correct on sm_80
cannot be known without running it: NOT_RUN.

## Not covered (no open referee, so not in the table)

- HMMA with BF16 or TF32 inputs: NAK encodes F16 inputs only.
- F2FP.BF16 and the packing variants.
- Source modifiers (absolute value, negation) on ALU sources: the oracle never sets them.
- Uniform-datapath arithmetic and `CS2R`.
- Everything above sm_89.

Branch distances above 28 bits (`bra.2`'s high field) are checked only at 0 and -1, because a listing that
branches farther would exceed `asm2`'s 8 MiB output.

## Running it on hardware (NOT_RUN)

Nothing here has been on an sm_80/86/87/89 GPU. To qualify it, one of these is needed: an A100 (sm_80); an
RTX 30xx or A-series workstation card such as an A5000 (sm_86); a Jetson Orin (sm_87); or an L4, L40 or RTX
40xx (sm_89). It also needs a launcher for that chip: CUDA++'s runtime builds sm_121 launch descriptors
(QMD 5.0), and Ampere and Ada use QMD 3.0.
