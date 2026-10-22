# How I built Omega from its seed

I want a language whose implementation I can account for from the first instruction. That means more than writing a compiler in a new syntax while C or Rust still builds everything underneath it.

I directed the development of a small bootstrap chain, then a compiler written in Omega's own source language. AI coding agents helped write the code, run the experiments and record failures. The work is visible in the source and commit history. I am not claiming that I personally typed every instruction or that using AI makes the result correct.

This walkthrough describes the seed-born compiler at [`29b12760f4c962606411d6bf25032aedef063301`](https://github.com/aien-dev/omega-mv/commit/29b12760f4c962606411d6bf25032aedef063301). It is available on a development branch, not a claim that the legacy implementation on `main` has already been replaced. The first seed commit is [`cd51427c`](https://github.com/aien-dev/omega-mv/commit/cd51427c3c4928ba3aba208add6d9aa9ad0ee798).

## 1. Start with instruction words, not a C program

The root source is [`seed0.hex`](https://github.com/aien-dev/omega-mv/blob/29b12760f4c962606411d6bf25032aedef063301/seed/rung0/seed0.hex). It contains explicit 32-bit words for AArch64 instructions and the ELF header Linux needs to start the executable. Each instruction has a comment naming the instruction and its Arm manual section.

For example, the source contains:

```text
d28007e8  # movz x8, #63 (Linux read)
d4000001  # svc #0
```

Those are encoded processor instructions, not input to gcc. The seed reads hexadecimal text, skips whitespace and comments, writes each completed word as bytes, and rejects malformed input. It is a hex loader, not the finished language compiler.

The root executable is **408 bytes**. Its source names the Arm Architecture Reference Manual, DDI 0487 revision M.d, and the System V and Arm ELF specifications. Those instruction and executable formats are existing standards, not my inventions.

## 2. Put the first bytes on disk

Before the seed can run, something has to write its bytes. [`load0.sh`](https://github.com/aien-dev/omega-mv/blob/29b12760f4c962606411d6bf25032aedef063301/seed/rung0/load0.sh) does that with shell operations, `printf` and `tr`. It converts the written hex words directly to little-endian bytes. It does not invoke a C compiler, assembler or linker.

That is an explicit host dependency. The shell and utilities already exist on the host, and Linux loads the result. I do **not** claim that the complete machine, operating system or every host utility was born from this seed. Later bootstrap scripts also use ordinary file and text utilities, including `cat`, `sed` and `stat`; the scripts are part of the inspectable procedure.

## 3. Make the seed reproduce itself

The new executable reads `seed0.hex` and writes another executable. The output must match the loaded seed byte for byte and match the committed `seed0.bin`.

A fresh root replay on AArch64 Linux produced:

```text
root_bytes=408
root_sha256=aca00cf3677236809f0f223437398c6bb429e4c65f13d58762bcf96d13799474
root_self_rebuild=IDENTICAL
committed_root=IDENTICAL
RC=0
```

[Run the root check](../evidence/seed-birth-20261022/verify-root.sh) and [read its output](../evidence/seed-birth-20261022/root-replay.log). This proves the tested reconstruction, not that an arbitrarily long history of every tool that ever touched the files can be proved free of C.

## 4. Add labels: hex1

Writing branch distances by hand is fragile. The next program, [`hex1`](https://github.com/aien-dev/omega-mv/blob/29b12760f4c962606411d6bf25032aedef063301/seed/rung1/hex1.hex), adds labels so a branch can name a location rather than depend on a manually counted distance.

Seed0 builds hex1. Hex1 then rebuilds itself. Its tests also reject missing or duplicate labels. This adds a useful capability while keeping the parent program small enough to inspect.

## 5. Add instruction names: asm2

The next step is an assembler, [`asm2`](https://github.com/aien-dev/omega-mv/blob/29b12760f4c962606411d6bf25032aedef063301/seed/rung2/asm2.s). Its [instruction table](https://github.com/aien-dev/omega-mv/blob/29b12760f4c962606411d6bf25032aedef063301/seed/rung2/table.s2) records fixed instruction bits and the fields that receive operands.

Hex1 builds asm2 from its lower-level source. Asm2 assembles its named-instruction source back to the same bytes. C encoders and GNU disassembly tools are used in separate checks to compare instruction encodings. They are referees in those tests, not the programs generating the seed-chain compiler executables.

## 6. Grow the source language: om3 and om4

[`om3`](https://github.com/aien-dev/omega-mv/blob/29b12760f4c962606411d6bf25032aedef063301/seed/rung3/om3.s) adds translation from the early Omegese function language to machine instructions. Omegese began as the low-level source layer between named instructions and the language I wanted to grow. It was a stepping stone, not a claim that the final language was already complete.

[`om4`](https://github.com/aien-dev/omega-mv/blob/29b12760f4c962606411d6bf25032aedef063301/seed/rung4/om4.s) adds the systems features needed to build a compiler: whole programs, calls, fixed buffers, arrays with lengths, checked operations, text, input, output and exit. Asm2 builds it. The executable header and machine instructions are emitted directly, without a C compiler or linker in that construction path.

Checks compare real programs, including Fibonacci, sorting and byte copying, against reference answers and separately compiled C programs. Broken inputs and deliberately damaged implementations must fail. Agreement with a reference is evidence for the tested cases, not a proof that every possible program is correct.

## 7. Write the compiler in its own language

The next compiler is [`omc.om`](https://github.com/aien-dev/omega-mv/blob/29b12760f4c962606411d6bf25032aedef063301/seed/omc/omc.om), written in the language that om4 understands.

The build proceeds as follows:

```text
explicit hex source -> seed0 -> hex1 -> asm2 -> om4
                                               |
                                               v
                                       omc stage 1
                                               |
                         same omc.om source -> stage 2
                                               |
                         same omc.om source -> stage 3
```

Stage 1, stage 2 and stage 3 must agree byte for byte at this pinned revision. The compiler's recorded SHA-256 is:

```text
191b776ef98821597c3e30655da6d680a9b66edbc9ceaca3c39c61ea6df098e9
```

The [bootstrap script](https://github.com/aien-dev/omega-mv/blob/29b12760f4c962606411d6bf25032aedef063301/seed/omega-from-seed.sh) contains the comparisons and refusal probes. Self-hosting here means compiling the actual compiler source, not copying a precomputed compiler binary to the output. A fixed point demonstrates reproducibility and self-consistency. It does not alone prove freedom from compiler bugs or malicious behavior; independent reference checks remain necessary.

## 8. Reproduce the pinned build

Use a **fresh checkout on AArch64 Linux**. This is not a Windows, macOS or x86-native qualification claim. The host needs Git, a POSIX shell and the file/text/hash utilities used by the linked scripts.

```sh
git clone https://github.com/aien-dev/omega-mv.git omega-seed-replay
cd omega-seed-replay
git checkout --detach 29b12760f4c962606411d6bf25032aedef063301
sh seed/omega-from-seed.sh
sha256sum seed/out/omc
```

The script must finish with `RC=0` and the compiler hash above. Inspect the scripts before running them. The checkout includes intermediate binaries, but the documented procedure reconstructs the chain from the root source and compares the intermediate results. Existing host utilities still participate in file handling and build orchestration.

## What I made, what I used, and what remains

The contribution described here is the project's explicit seed implementation, the chain of translators, the Omega-written compiler source and the tests connecting those stages. The source and history let a reader inspect that construction rather than accept a new name on an opaque binary.

I did not invent processor instructions, ELF, bootstrapping, self-hosting or compiler verification. I used published architecture specifications and established ideas. The [original seed notes](https://github.com/aien-dev/omega-mv/blob/29b12760f4c962606411d6bf25032aedef063301/seed/README.md) name technical references. Historical context includes [Ritchie's account of C](https://www.nokia.com/bell-labs/about/dennis-m-ritchie/chist.html), [bootstrappable builds](https://bootstrappable.org/) and [Wheeler's diverse double-compiling work](https://dwheeler.com/trusting-trust/). A citation to those ideas is not a claim that this project invented them or reproduced all of their guarantees.

The legacy C implementation and Rust-built services still exist. Retiring them is active work. The destination is native programs built by the seed-born compiler, with behavior checked before replacement. The optional Omega-to-C bridge is for compatibility with other tools; C output is not the required foundation or bootstrap of the seeded language.

This is the reason for the work: I want the language to stand on an inspectable foundation that I can rebuild, change and explain. I want it to connect to existing software without making that software its permanent base. It does not establish full feature parity, universal hardware support, zero external dependencies, absence of all bugs or an automatic performance advantage.
