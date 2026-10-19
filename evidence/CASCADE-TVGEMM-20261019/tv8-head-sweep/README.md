# tv8-head-sweep (clean binary fba0f728, both locks)
lm_head (n 248320, k 5120, 1.27 GB), rows 1/16/32, tiles 2:2:4,2:4:4,4:2:8,4:1:8,1:4:2 plus automatic.
32 rows: automatic 6.92 ms (183.6 GB/s); every listed tile 6.95 to 7.42 ms: no tile beats automatic beyond run-to-run noise (the same 2:2:8 tile read 6.92 and 7.42 ms in two samples of one run). 1 row: automatic 6.69 ms, 2:2:4 6.60 ms.
Bits: all identical to the reference.
So lm_head stays on the automatic tile; it is already at 184 to 190 GB/s against a measured streaming ceiling of 195 to 208 GB/s.
