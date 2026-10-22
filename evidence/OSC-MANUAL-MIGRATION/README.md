# Manual OSC retirement into seeded Omega — L550

## Binding scope

Drake requested personal, handwritten replacement of legacy OSC with the new seeded Omega. No automated transpiler, generated C, C bootstrap compiler, C runtime, or wrapper counts as a replacement. Production compilers must be Omega-written and compiled by the seed-descended Omega compiler. C interoperability belongs at the converter boundary.

Archive source pin: `57f5f4258cebf3d5b2c22853cd062e351265c525`.
Archive tag: `archive/legacy-osc-20261022-L550`.
The tag retains the entire source tree and history, including old evidence. `legacy-inventory.json` records exact blob and SHA256 identities for the pinned OSC/compiler scope. `remote-refs-at-archive.txt` records other branches for reconciliation: those branch-only variants are NOT claimed migrated or fully archived by this single tag.

Archiving is preservation, not production retirement. Existing consumers are not deleted while they still depend on legacy code. This branch is a migration candidate, not a completed system conversion.

## Compiler ancestry

The unchanged pinned `seed/omega-from-seed.sh` was executed on AArch64 Linux. The encoded seed reconstructs seed0, hex1, asm2, om4, then Omega-written omc; stage1, stage2 and stage3 agree at:

`01b1545b23e8ff40cb5691e20b5df910ea4acaefde3495f0d62d270465b98cd6`

The retained assembler used by mk4 matches the reconstructed assembler. This ancestry has no C compiler or generated-C stage. Shell, file tools and Linux remain HOST dependencies; this is NOT a claim that the current boot environment is C-free. `seed-gate6.txt` is the fresh seed compiler regression output. Historical C-based oracles are not build inputs to the new primitive; its test oracle is Python integer arithmetic, outside the runtime and construction chain.

## First handwritten compiler primitive

`seed/native/literal.om` implements an unsigned integer-literal reader: decimal, `0x` hexadecimal and `0b` binary, up to 64 bits. This carries over integer-reading behavior from the archived lexer without implementing or preserving the OSC surface language. It is a standalone primitive for integration, NOT a replacement of the entire lexer or a modification to omc yet.

The standalone test transport reads one literal through stdin, accepts at most 4096 bytes, and writes exactly eight little-endian bytes on success. Exit 2 means malformed/over-capacity, 3 overflow, 4 I/O failure. Refusals produce no stdout. It uses fixed pools and length-carrying input, no null-terminated strings, heap, foreign library or dynamic linker. It handles short reads and writes. Syntax errors take precedence over overflow if a later digit is invalid.

The overflow quotient and remainder are computed once per literal instead of dividing for every digit. This is an operation-level optimization, NOT a measured speedup.

Tests: independent host oracle, 796 executed cases including 64-bit boundaries, malformed encodings, full capacity, overflow, and deterministic random decimal/hex/binary inputs. The compilable refusal-only RED stub failed 778 cases. The handwritten implementation passes all 796. Three successfully compiled mutants (overflow refusal, digit value, capacity) are detected; reports retain actual mismatch counts. After each mutant the original source is restored, recompiled and verified. Executables are checked for nonempty ELF before execution.

Reproduce from this branch on the Spark:

```
sh seed/omega-from-seed.sh
python3 tests/seed-migration/literal.py "$PWD"
python3 tests/seed-migration/archive-and-controls.py
python3 tests/seed-migration/literal.py "$PWD"
sh seed/omc/gate6.sh
```

Python is test/archive orchestration only, never imported, linked or invoked by the Omega binary. The archive/control script snapshots refs and rewrites its reports; run in a disposable worktree to preserve this packet.

## Remaining work — no retirement credit yet

1. Reconcile branch-only OSC work and every active consumer against the pinned inventory.
2. Hand-port remaining compiler functionality where not already supplied by the seeded compiler; reuse qualified native implementations instead of duplicating them.
3. Verify lexical/diagnostic, parsing, type/ownership checking, IR, native/GPU emission and consumer contracts independently. A primitive gate is not whole-compiler equivalence.
4. Integrate with the compiler owner without overwriting ongoing R6 IR changes. The mutable `omega-in-omega` working tree was neither copied nor edited.
5. Cut consumers over to verified seed-native artifacts, then remove their legacy build/runtime dependencies. Preserve failed and historical evidence.
6. Continue the boot-up driver/system replacement under L332. No module was removed, driver loaded, service stopped or machine rebooted for this milestone.

Independent review, full repository suite, production integration, whole-OSC retirement and hardware qualification: NOT RUN / NOT COMPLETE.
