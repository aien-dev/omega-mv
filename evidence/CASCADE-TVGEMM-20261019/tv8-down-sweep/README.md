# tv8-down-sweep (clean binary fba0f728, quiet lock + GPU lock)
down projection (5120 x 17408, weights 89 MB), rows 1, 16, 32, 21 reps, tiles 2:2:4,2:2:2,2:4:4,2:4:2,1:4:2,1:4:8,4:1:4 (row tile : column tile : warps).
- Automatic tile: 32 rows 583 us median (153 GB/s), 1 row 566 us, 16 rows 581 us.
- Best tile 2:2:2 (fewer warps per block): 32 rows 538 us (165.6 GB/s), 16 rows 525 us, 1 row 530 us: about 8 percent faster than automatic. 1:4:2 next (543 to 550 us).
  Tile 4:1:4 is the slowest (641 to 660 us).
- Bits: every listed output identical to the reference and to the N=32 pass.
- refused-0-early.log: an earlier attempt that died at GPU channel creation (out of memory, resident servers); not a result.
Next: make 2:2:2 the automatic pick for this shape family (low N, k large) and re-measure.
