# evidence/SEED-0/sm121: rung 6 results

Made by `sh seed/sm121/gate6.sh` (checks) and `seed/sm121/chip6.sh` (chip run). What each file is:

- `gate6.log`: the whole check run, top to bottom, ending in `GATE6 PASS` or `GATE6 FAIL`.
- `kernels.tsv`: every GPU program the C generators made in the sweep, with its size.
- `kernels-result.tsv`: for each program, a fingerprint of the C code's bytes and `SAME` or `DIFF` against `asm2`.
- `notes-summary.tsv`: things the C code does quietly (ignores a requested schedule, settings skipped and why).
- `vecadd.sha256`: fingerprint of `seed/sm121/vecadd.bin`, the vector add `asm2` made.
- `vecadd.sass.txt`: NVIDIA's disassembler reading that vector add back (reference only).
- `forms-nvdisasm.tsv`, `name-differences.tsv`: the name NVIDIA's disassembler gives each instruction form,
  and where it differs from the name in our C code.
- `nvdisasm-refused*.txt`: the edge cases the disassembler would not read, counted by reason, by
  scheduling word and by instruction form.
- `chip/`: the chip run (`steps.txt` is the verdict; `window.txt` names the commit, the bytes and the tool).

The plain-English write-up is `seed/sm121/README.md`.
