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
