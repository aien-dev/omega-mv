# seed/tensix: vecadd for the Tenstorrent Tensix RISC-V cores

Each Tensix tile on Tenstorrent Wormhole (n150, n300) and Blackhole (p100, p150) cards contains five
small RISC-V cores: RISCV B, NC, T0, T1 and T2. This directory writes a 32-bit integer vector add for
these cores, `out[i] = a[i] + b[i]` modulo 2^32, and checks the result with outside tools.

The route is the same as `seed/sm121` (rung 6):

- the unchanged rung-2 assembler `seed/rung2/asm2.bin`;
- a plain-text instruction table, `rv32im.tbl` (all 48 RV32I + M instructions);
- hand-written listings.

No C, Rust or Python program produces any instruction word.

| File | What it is |
|---|---|
| `rv32im.tbl` | RV32IM encodings as asm2 table lines. Registers are written as numbers 0-31, because asm2's register parser uses ARM names. |
| `vecadd.s2` | The kernel: 11 instructions, 44 bytes. a0 = a, a1 = b, a2 = out, a3 = n; it returns through ra. |
| `vecadd.bin` | The committed kernel bytes. The gate rebuilds them twice and requires the same bytes both times. |
| `boot-tt.s2` | 9 instructions placed at L1 0x0, where RISCV B starts after reset. It loads the arguments from 0x1000, calls vecadd, then writes the done mark 0x0600d000 to 0x1010 and spins. |
| `boot-qemu.s2` | Linux user-mode start code for qemu. It calls vecadd, then makes the `write(1, out, 4n)` and `exit(0)` system calls. |
| `edges.s2`, `edges.expect` | Every table entry once, with fields at their edge values. `edges.expect` is the disassembly written by hand before any disassembler was run. |
| `vecadd.expect` | The expected disassembly of the kernel. |
| `a.hex`, `b.hex`, `expect.hex` | 64 little-endian words each. The first six are hand-picked edge cases; 61 of the 64 sums wrap. |
| `oracle.py` | The oracle driver: interop oracle, not Omega-native, and it never generates kernels. It loads ttsim and talks to it over PCIe the way a host driver would, or wraps an image in an ELF file for qemu. |
| `gate-tensix.sh` | The gate script. |

Expected values were computed in Wolfram with this query:
`WolframLanguageEvaluator: a=Join[{0,2^32-1,2^31,2^31-1,2^32-1,16^^12345678}, Table[Mod[2654435761(i+1),2^32],{i,6,63}]]; b=Join[{0,1,2^31,1,2^32-1,16^^87654321}, Table[Mod[2^32-1-40503 i^2+977 i,2^32],{i,6,63}]]; Mod[a+b,2^32]`.
It reported 64 words, of which 61 have a+b ≥ 2^32.

## Run

```sh
mkdir -p build/oracle/ttsim-v1.15.0 && cd build/oracle/ttsim-v1.15.0
for a in wh bh; do curl -LO https://github.com/tenstorrent/ttsim/releases/download/v1.15.0/libttsim_${a}_aarch64.so; done
sha256sum libttsim_*   # the gate refuses anything but the pinned sha256 values below
cd - && nice -n 19 sh seed/tensix/gate-tensix.sh   # 0 PASS, 1 FAIL, 3 NOT_RUN
```

The host must be AArch64 Linux, because asm2.bin is an AArch64 program. The gate also needs:

- `riscv64-unknown-elf-objdump` and `riscv64-unknown-elf-objcopy`;
- `llvm-objdump` (version 18);
- `qemu-riscv32`;
- `python3`.

If any of these, or a ttsim library, is missing, the gate prints NOT_RUN and exits with code 3.

## What the gate checks

All of these checks are interop oracles, not Omega-native.

1. **Same bytes every build.** Each image is assembled twice and both builds must match byte for byte. The kernel must also equal the committed `vecadd.bin`, and both start images must end with exactly those bytes.
2. **Two independent disassemblers.**
   - GNU objdump must print exactly `vecadd.expect` and `edges.expect`. These cover all 48 table names: 51 words in total.
   - LLVM objdump must agree with GNU on every mnemonic and register.
3. **Three runs that compare results.** The kernel runs on:
   - ttsim Wormhole (Tensix tile (1,1));
   - ttsim Blackhole (Tensix tile (1,2));
   - qemu-riscv32.

   Each run must give all 64 sums equal to `expect.hex`. On ttsim, the output buffer is first filled with 0xee bytes, so a kernel that never ran cannot pass.
4. **Negative controls.** Every one of these must be caught:
   - C1: one bit flipped in the table (add becomes sub). Caught by GNU objdump and by ttsim.
   - C2: one wrong expected value.
   - C3: the wrong register in the listing. Caught by qemu.
   - C4: one wrong branch-offset bit. ttsim stops with "invalid pc".
   - C5: an out-of-range immediate. asm2 refuses it.

## What is established, and what is NOT

**Established (EXERCISED on oracles, 2026-10-26):**

- The table encodes RV32IM the way two independent disassemblers read it.
- The kernel computes the right sums on ttsim's models of Wormhole B0 and Blackhole A0 RISCV B, and on qemu.

**NOT established:**

- **Nothing has run on a Tenstorrent card** (NOT_RUN on hardware).
- ttsim is Tenstorrent's software model, not silicon.
- The fp32 SFPU path and the Tensix coprocessor (matrix unit) are not used.
- No `.om` program emits these words yet; they come from a hand-written listing through the table. The next step is an `.om` IR → RV32 emitter like `seed/spirv/irspirv.om`.
- Only RISCV B is used, on a single tile.
- There is no NoC traffic, no DRAM, and no tt-metal runtime.

## What is open (sources read 2026-10-26)

- **[tt-isa-documentation](https://github.com/tenstorrent/tt-isa-documentation)** at be833101 (code Apache-2.0, images CC BY-ND 4.0; `LICENSE_understanding.txt` notes that patent rights may be needed). It documents:
  - the baby RISC-V cores. Wormhole is RV32IM. Blackhole adds Zicsr, Zaamo, Zba, Zbb, part of F/Zfh, and `.ttinsn`; T2 also has V.
  - L1, soft reset, the PCIe TLB windows and NoC coordinates;
  - about 120 Tensix coprocessor instructions, each with a bit-encoding diagram and a functional model (for example `TensixCoprocessor/MVMUL.md`).

  Coprocessor instructions are pushed by RISC-V `sw` (`BabyRISCV/PushTensixInstruction.md`). So the RISC-V level, the coprocessor instruction level, and the host-to-tile path are all documented well enough for Omega to emit code without closed tools.
- **[ttsim](https://github.com/tenstorrent/ttsim)** (Apache-2.0), source at b3620244. [Release v1.15.0](https://github.com/tenstorrent/ttsim/releases/tag/v1.15.0) was published 2026-10-25. Pinned libraries:
  - `libttsim_wh_aarch64.so`: `e2a56ffe04072a10d477db1ef85cdbd5d392d9fc52043bb949b90a4c01ff6b4f`;
  - `libttsim_bh_aarch64.so`: `2367dccc964c0ce4a20b14520e5f7ac6191bb227fb20e87aca1e94bf7432ced2`.
- **[tt-metal](https://github.com/tenstorrent/tt-metal)**, **[tt-umd](https://github.com/tenstorrent/tt-umd)**, **[sfpi](https://github.com/tenstorrent/sfpi)** (GCC-based SFPU compiler) and **[tt-llk](https://github.com/tenstorrent/tt-llk)** are Apache-2.0. **[tt-kmd](https://github.com/tenstorrent/tt-kmd)** (the kernel driver) is GPL-2.0. Licences were read from the GitHub API on 2026-10-26. None of these is used here.
- **RISC-V Unprivileged ISA 20191213**, chapters 2, 7 and 24, for the instruction formats.

## Before a hardware receipt

1. **A card.** An n150 or p150 card, or Tenstorrent cloud access. We have neither.
2. **The kernel driver.** tt-kmd (GPL-2.0) must be loaded to map BAR0. The same TLB and soft-reset sequence that `oracle.py` uses on ttsim then works on the card. tt-umd does this from C++.
3. **Firmware and tile choice.** On real cards the ARC firmware and the tt-metal firmware own the tiles. A raw run must use a tile that nothing else is using and must restore soft reset afterwards. Harvested rows and columns change which NoC coordinates are Tensix tiles.
4. **Running beyond one tile.** NoC reads and writes are needed for DRAM inputs and for using many tiles.
5. **What the receipt must contain.**
   - the card model and serial;
   - the firmware version;
   - the tt-kmd version;
   - the image sha256;
   - the tile coordinates;
   - the output compared with `expect.hex`;
   - the same negative controls, run on the card.

## Host dependencies (oracles only)

| Dependency | Version | Role | Omega replacement |
|---|---|---|---|
| GNU objdump | binutils 2.42 | disassembler | none needed (read-only oracle) |
| LLVM objdump | 18.1.3 | second disassembler | none needed (read-only oracle) |
| ttsim | v1.15.0 | Tensix simulator | none needed (vendor oracle) |
| qemu-riscv32 | 8.2.2 | RV32 executor | none needed (read-only oracle) |
| Python | 3.12.3 | `oracle.py` byte transport | an `.om` host loader (dlopen/FFI word plus ELF byte writer; proposal) |
