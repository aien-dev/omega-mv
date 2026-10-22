# Handwritten native executable checker

L550 follow-on: `seed/native/elfcheck.om` is handwritten Omega, compiled by the unchanged qualified seed-born compiler. `tests/seed-migration/native-closure.sh` now uses it to check the compiler, itself and the native literal reader. It does not invoke readelf or a C/Rust parser. Shell, SHA256 and temporary-file tools remain explicitly host orchestration.

Supported contract: one complete ELF64 little-endian AArch64 ET_EXEC on stdin, at most 8 MiB. It checks ELF/version/header shape, the bounded program-header table, load-segment file/memory/address ranges and an entry inside file-backed executable load bytes. It refuses PT_INTERP and PT_DYNAMIC. Status 0 accepted shape, 2 malformed/unsupported shape, 3 dynamic segments, 4 capacity, 5 read error. No stdout. It is intentionally not a general ELF validator or a linker; section tables, relocations and ABI-specific mappings are outside this gate.

**No dynamic segments does not prove no statically linked C.** Source provenance and seed ancestry remain separate mandatory gates. The binary is Linux-hosted, not a bootloader or kernel replacement. No old production artifact is counted retired here.

Tests use independently assembled ELF ABI fields in a host-only Python harness. The initial compilable refusal stub fails six cases; adding entry validity tests then exposes three additional acceptance bugs, fixed before this source was published. Final 148 cases pass, including every truncation of a minimal ELF, offsets near integer limits, incompatible types/architecture, invalid segment lengths, dynamic loading, entry in zero-fill and capacity. The actual compiler and the checker itself are positive cases. Three successfully compiled mutants remove dynamic, segment or entry checks; each is detected. A final rebuild restores the original source and tests it again. The earlier literal reader still passes its 796 cases and seed gate6 passes 26 cases.

Reproduce on AArch64 Linux:

```
sh seed/omega-from-seed.sh
python3 tests/seed-migration/elfcheck.py "$PWD"
python3 tests/seed-migration/elfcheck-mutants.py "$PWD"
python3 tests/seed-migration/elfcheck.py "$PWD"
sh tests/seed-migration/native-closure.sh
python3 tests/seed-migration/literal.py "$PWD"
sh seed/omc/gate6.sh
```

Independent review, full repository suite, whole compiler replacement, production retirement and hardware qualification remain NOT RUN / NOT COMPLETE. This is one functioning checker integrated into the owned migration check, not a claim that host tools have all been replaced.
