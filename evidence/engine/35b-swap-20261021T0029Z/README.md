# 35b-swap-20261021T0029Z: head swap mid-run on the 35B (H4)

`run-swap-35b.sh`, binary sha256 0367f0be4341f76c, cells pinned to the X925 cores, load 20 to 35 at each start.
Head A = L308's measured prose head (90 percent coverage, 48 to 168 experts per layer), resident in host RAM before the
run; head B = the measured code head, fired from token 12 of 24; a loader brings head B into RAM `lead` tokens before
the swap (lead -1 = never: every B part comes from the disk when its beat needs it). Stand-in fire 2 ms per beat, so
the steady time per token is the RAM copy of the head (about 12 GB per token on the host). Table:
`bash table-swap.sh 35b-swap-20261021T0029Z`; each `lead*/summary.json` holds the cell, `run.log` one line per cell.

| loader lead (tokens before the swap) | steady ms/token before | first token after swap: extra | total extra the user sees | hit rate before | hit rate after | first all-RAM token after swap | head B load | B load start vs swap |
|---|---|---|---|---|---|---|---|---|
| 0 | 1808.26 ms | 1651.12 ms | 8793.33 ms | 1 | 0.963 | 13 | 3238.02 ms | 2.01 ms |
| -1 | 1717.05 ms | 1194.05 ms | 14846.34 ms | 1 | 0.356 | never | -1 ms | 0 ms |
| 1 | 1852.41 ms | 580.42 ms | 7400.16 ms | 1 | 1 | 12 | 3251.32 ms | -2167.07 ms |
| 2 | 1249.89 ms | 289.09 ms | 4322.58 ms | 1 | 1 | 12 | 2203.98 ms | -2750.86 ms |
| 4 | 1675.29 ms | 360.61 ms | 7858.69 ms | 1 | 1 | 12 | 3063.37 ms | -6537.57 ms |
