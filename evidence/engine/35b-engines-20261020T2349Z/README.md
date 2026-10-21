# 35b-engines-20261020T2349Z: engine count x coupling x buffers on the 35B (the motor)

`run-engines-35b.sh`, 12 cells, the same fixed schedule each (8 experts x 40 layers x 40 tokens = 1,600 beats, 25.2 MB per
beat of 3.146 MB experts), movement engines alone with a 2 ms stand-in fire per beat, host load 36 to 46 at every start
(the box was shared with other agents' CI and the 35B hum tests; `quiet-wait.sh` held cells until the 1-minute load was
under 50). Table: `bash table-engines.sh 35b-engines-20261020T2349Z`.

Binary: the script calls the binary by path per cell, so cells started after a rebuild use the newer file. Cells 1 to 9
(23:49Z to 00:04:48Z) ran on the binary before the staging-ring fix for most of that window (the fixed binary landed at
00:04:05Z); cells 10 to 12 (buffers 3, engines 2; 00:18Z to 00:25Z) ran on d46ec3e31545e0d8, the binary with the fix
(sc 3a9285da). The `move` path did not change between those builds except for the fix. Every cell passed closed
(rc 0, 0 late beats, 0 misfires); the three buffers-3 / engines-2 cells that failed on the 30B before the fix
("staging slot not published") pass here with it.

| buffers | engines | coupling | beats | late | beat (median) | spread | ignition gap | idle | fill | GB/s in fill | NVMe stage | RAM stage | hand-off wait | load at start |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 2 | 1 | brigade | 1600 | 0 | 13.99 ms | 1.19 | 279 us (p90 667 us) | 11.79 ms | 13.68 ms | 1.29 | 13.68 ms | 0 ms | 0 us | 43.31 |
| 2 | 1 | clock | 1600 | 0 | 10 ms | 0.32 | 2869 us (p90 4429 us) | 8 ms | 6.68 ms | 3.3 | 6.68 ms | 0 ms | 0 us | 42.21 |
| 2 | 1 | reservoir | 1600 | 0 | 6.96 ms | 0.86 | 21 us (p90 200 us) | 4.81 ms | 6.86 ms | 3 | 6.86 ms | 0 ms | 0 us | 39.78 |
| 2 | 2 | brigade | 1600 | 0 | 15.8 ms | 2.29 | 17 us (p90 1744 us) | 13.65 ms | 25.5 ms | 0.62 | 12.54 ms | 11.04 ms | 180 us | 39.61 |
| 2 | 2 | clock | 1600 | 0 | 20.14 ms | 2.25 | 31 us (p90 1933 us) | 18.01 ms | 42.19 ms | 0.5 | 17.44 ms | 17.64 ms | 267 us | 41.19 |
| 2 | 2 | reservoir | 1600 | 0 | 14.17 ms | 1.56 | 23 us (p90 248 us) | 12.03 ms | 24.18 ms | 0.75 | 12.29 ms | 11.78 ms | 16 us | 40.04 |
| 3 | 1 | brigade | 1600 | 0 | 12.4 ms | 1 | 33 us (p90 469 us) | 10.16 ms | 12.28 ms | 1.65 | 12.28 ms | 0 ms | 0 us | 42.50 |
| 3 | 1 | clock | 1600 | 0 | 10.65 ms | 0.84 | 24 us (p90 407 us) | 8.58 ms | 10.55 ms | 2 | 10.55 ms | 0 ms | 0 us | 44.42 |
| 3 | 1 | reservoir | 1600 | 0 | 13.55 ms | 1.43 | 134 us (p90 586 us) | 11.37 ms | 13.27 ms | 1.1 | 13.27 ms | 0 ms | 0 us | 45.77 |
| 3 | 2 | brigade | 1600 | 0 | 25.25 ms | 2.59 | 231 us (p90 7819 us) | 22.79 ms | 62.09 ms | 0.31 | 22.96 ms | 22.82 ms | 5167 us | 45.82 |
| 3 | 2 | clock | 1600 | 0 | 30.01 ms | 2.43 | 160 us (p90 9574 us) | 26.81 ms | 75.23 ms | 0.25 | 26.51 ms | 26.91 ms | 7147 us | 42.82 |
| 3 | 2 | reservoir | 1600 | 0 | 11.84 ms | 0.81 | 26 us (p90 2975 us) | 9.63 ms | 27.08 ms | 0.9 | 11.4 ms | 11.33 ms | 3185 us | 36.32 |
