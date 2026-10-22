# Native self-host binary comparison — L550

`seed/native/byteequal.om` is handwritten seeded Omega. It compares byte streams inherited on descriptors 3 and 4. Status: 0 equal, 1 different, 2 read failure, 3 compared equal-length input exceeds 64 MiB. No stdout/stderr. It fills two 4096-byte buffers independently, so different short-read boundaries do not create false inequality. No heap, C library, external comparator or hash utility runs inside it. The data buffers occupy 8192 bytes; the compiler's standard stack reservation is separate.

The limit is checked as comparison progresses, not as an upfront file-size assertion. A difference can return 1 before either stream's unconsumed suffix is inspected. On mismatched lengths it returns 1. Equal streams of exactly 64 MiB pass; equal streams one byte larger refuse. I/O errors are fail-closed; EINTR is returned as an I/O failure, not silently retried. A peer that never produces input can block: timeout supervision is host orchestration, not implemented in this reader.

## Actual caller migration

`tests/seed-migration/native-closure.sh` now builds the comparator with the pinned seed-born compiler, checks its ELF with the native checker, rebuilds the Omega compiler three successive times, checks each executable before running it, and invokes the Omega comparator on:

- qualified compiler vs stage1;
- stage1 vs stage2;
- stage2 vs stage3.

That is an actual self-host verification path using native Omega for binary comparison. It is not a claim that the owner's original `seed/omega-from-seed.sh` has been modified: its early seed checks still use host `cmp`. Shell, SHA256, file permissions and temporary-directory orchestration remain explicitly on loan. Those files and the compiler owner's mutable R6 worktree were not edited. The new path is supplementary until integrated by the existing compiler owner.

## Executed evidence

- RED: compilable refusal stub fails 12 of 47 cases.
- GREEN: 47 cases pass: empty files, mismatches at beginning/middle/end, unequal lengths around buffer boundaries, missing descriptors, differently fragmented FIFOs and exact/over-capacity sparse files.
- Three compiled mutants removing byte, length or capacity checks are detected; original source is restored and rebuilt afterward.
- The actual three-stage self-host check passes.
- A negative integration run appends a byte to stage3 after ELF inspection: the native comparator stops the workflow with exit 1 and no success message.
- Earlier 148 ELF cases, 796 literal cases and 26 seed struct cases pass again.

Reproduce:

```
sh seed/omega-from-seed.sh
python3 tests/seed-migration/byteequal.py "$PWD"
python3 tests/seed-migration/byteequal-mutants.py "$PWD"
python3 tests/seed-migration/byteequal.py "$PWD"
sh tests/seed-migration/native-closure.sh
python3 tests/seed-migration/selfhost-negative.py "$PWD"
```

Python and shell here are test/build supervision, not the comparator implementation. Compiler ancestry is the pinned seed chain; fixpoint equality is not independent proof of compiler correctness. Full repository verification, independent review, production retirement and hardware cutover remain incomplete.
