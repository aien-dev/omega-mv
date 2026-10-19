# tv8-gu-sweep (clean binary fba0f728, quiet lock + GPU lock)
Full gate|up (34816 x 5120, 178.3 MB), rows 1, 16, 32, 21 reps, tile list 2:4:8,2:4:4,2:4:2,2:2:2,4:2:8,4:2:4,4:1:4,1:4:8 (row tile : column tile : warps).
Result (receipt.json: exit 0, box load 28 to 49, so a busy box):
- 32 rows: automatic tile 945 us median (min 931), best listed tile 940 us; 1 row 916 us (195 GB/s); 16 rows 931 us.
  The 1.0 ms milestone for gate|up at 32 rows is met on this run (earlier tv4/tv6 on a busier box gave 0.99 to 1.04 ms).
- Every row of every N and every tile that ran: 0 outputs differ from the reference bits and from the N=32 pass; emulation check 0 of 69632 differ.
- Tile 4:1:4 is 17 to 20 percent slower (1.12 to 1.14 ms); 4:2:8 and 2:2:8 are the fastest alternatives, within noise of the automatic tile.
- refused-0-early.log, refused-1..3.log: earlier attempts that died before measuring ("out of memory" while creating a GPU channel, kernel log NV_ERR_NO_MEMORY in
  kgrctxAllocMainCtxBuffer) because three model servers were resident. They are kept as evidence of the retries; they are not results.
