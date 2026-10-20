# evidence/SEED-0: receipts for the seed chain (seed/README.md)

- `rung0/gate0.log`: rung 0 gate run (self-rebuild, both decoders, controls). `rung0/seed0.readelf`:
  GNU readelf (reference only) reading the hand-written ELF header. `rung0/seed0.sha256`.
- `rung0-worksheet.sh`, `rung1-worksheet.sh`: the arithmetic behind seed0.hex and hex1.hex (manual field
  layouts with values put in; every branch distance counted by hand).
- `rung1/gate1.log`: rung 1 gate run (build by seed0, self-rebuild, labelled cross-check, audit, controls).
- Manual: Arm DDI 0487 revision M.d, sha256 80d589a4645feba6bdb9d77edfaf04d4394d5d5444a4c41f97cf4fdd20eee3d5
  (downloaded 2026-10-19 from documentation-service.arm.com, the public Arm documentation site).
- `rung2-worksheet.sh` + `rung2-listing.sh`: the arithmetic behind asm2.hex1 (branch distances left to hex1).
- `rung2/gate2.log`: rung 2 gate run. `rung2/encoder-compare.tsv`: every case, the C encoder's word and
  asm2's word side by side (5,496 cases, all "same").
- `rung3/gate3.log`: rung 3 gate run (om3 built twice, audit, controls, the 10 tasks, error controls).
  Per task: `devNN.seed.bin` (the words om3 made), `devNN.seed.dis` (objdump reading them, reference
  only), `devNN.seed.out` and `devNN.c.out` (the runner's answers for the seed path and the C path).
  `rung3/words.tsv`: word counts per task. `rung3/om3.sha256`.
- `rung3-worksheet.sh`: the arithmetic behind om3's keyword table.
- `rung5/gate5.log`: rung 5 gate run (build twice, sha256 pin, byte compare, audit, controls).
  `rung5/m1-gates.log`: the Atlas M1 gates (run_m1_gates.sh, unedited) on a scratch copy holding the
  asm2-made atlas.bin, including the QEMU boot. `rung5/atlas.sha256`, `rung5/cmp.txt` (empty = identical).
- `rung5-worksheet.sh`: the arithmetic behind Atlas's message bytes and constants in atlas.s2.
