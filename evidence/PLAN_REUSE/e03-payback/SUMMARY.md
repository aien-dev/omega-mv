## Predictions (development partition only, committed before the held-out runs they apply to)

### wall e2e (01-prediction.json @23472463, applies to runs 1 to 3)

| stream | p | Delta us/hit [q1,q3] | C us | m us | predicted net(40) us [band] | predicted break-even (tasks) | no finite payoff |
|---|---|---|---|---|---|---|---|
| S1_reverse | 1.00 | -15.2 [-41.5, -3.7] | 74.1 | 0.0 | -665.6 [-1272.9, -218.5] | -1 | True |
| S1_merge_P+ | 1.00 | +42.5 [+14.5, +61.3] | 91.8 | 0.0 | +1564.9 [+326.3, +2298.8] | 4 | False |
| P0_no_payoff | 0.00 | +0.0 [+0.0, +0.0] | 70.3 | 63.2 | -2535.1 [-2535.1, -2535.1] | -1 | True |

### cognition CPU (01-prediction-cpu.json @64be1932, applies to runs 4 and 5)

| stream | p | Delta us/hit [q1,q3] | C us | m us | predicted net(40) us [band] | predicted break-even (tasks) | no finite payoff |
|---|---|---|---|---|---|---|---|
| S1_reverse | 1.00 | +1.0 [-0.8, +3.0] | 76.6 | 0.0 | -39.2 [-100.4, +41.3] | 81 | False |
| S1_merge_P+ | 1.00 | +46.8 [+42.5, +49.6] | 99.4 | 0.0 | +1725.8 [+1129.7, +1835.3] | 4 | False |
| P0_no_payoff | 0.00 | +0.0 [+0.0, +0.0] | 72.6 | 68.4 | -2738.4 [-2738.4, -2738.4] | -1 | True |

### run1: arm B = existing reuse with adaptation/repair (frozen protocol); commit 23472463; binary sha256 7127e0f51a6f; load 16.56 (runnable 6/2501); checks 3058 failures 52 bad_rows 1; reconciliation MISMATCH (688 rows, 1 bad, unattributed 6224 us)

| stream | hits/misses | acq | adapt | refusals | rederiv | restarts | floors | neg transfer | wall net sum us | wall break-even | wall per-task median us [q1,q3] | saved_gross us | spent_learning us (acq/maint/repair/restart) | reinvested / unfunded us | templates (bytes) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| S1_reverse | 39/1 | 1 | 0 | 0 | 0 | 0 | ok | 0 | -7.1 | 2 | +0.0 [+0.0, +0.0] | +150.9 | 77.8 (74.6/3.3/0.0/0.0) | 0.0 / 77.8 | 1 (34056) |
| S1_merge_P+ | 39/1 | 1 | 0 | 0 | 0 | 0 | ok | 0 | +2322.8 | 2 | +0.0 [+0.0, +0.0] | +2252.8 | 94.7 (92.0/2.7/0.0/0.0) | 0.0 / 94.7 | 1 (34056) |
| S2_warmup_dev_seeds | 22/2 | 2 | 1 | 12 | 0 | 0 | VIOLATED | 11 | -15149.6 | -1 | +0.0 [+0.0, +0.0] | -13275.1 | 196.9 (186.2/5.6/5.1/0.0) | 0.0 / 196.9 | 2 (68112) |
| S2_heldout_bindings_compositions | 39/1 | 1 | 0 | 13 | 0 | 0 | VIOLATED | 13 | -15139.4 | -1 | +0.0 [+0.0, +0.0] | -15032.1 | 61.8 (58.1/3.7/0.0/0.0) | 0.0 / 61.8 | 3 (102168) |
| S3_interference | 35/5 | 5 | 3 | 48 | 0 | 0 | VIOLATED | 14 | -20079.7 | 2 | +0.0 [+0.0, +0.0] | -25652.3 | 523.3 (463.2/28.3/31.9/0.0) | 0.0 / 523.3 | 5 (170280) |
| S4_changed_preconditions | 37/3 | 3 | 2 | 30 | 0 | 0 | VIOLATED | 9 | -10138.0 | -1 | +0.0 [+0.0, +0.0] | -8559.0 | 270.5 (247.0/12.5/11.0/0.0) | 0.0 / 270.5 | 3 (102168) |
| S5_stale_generations | 30/10 | 10 | 0 | 9 | 9 | 0 | ok | 0 | -6355.9 | -1 | +0.0 [+0.0, +0.0] | -6158.5 | 695.7 (655.0/40.7/0.0/0.0) | 0.0 / 695.7 | 1 (34056) |
| S6_restart | 39/1 | 1 | 0 | 0 | 0 | 1 | ok | 0 | +1661.6 | 2 | +0.0 [+0.0, +0.0] | +1715.8 | 250.8 (74.1/2.9/0.0/173.8) | 113.4 / 137.4 | 1 (34056) |
| P0_no_payoff | 0/40 | 40 | 0 | 39 | 39 | 0 | ok | 0 | -5769.2 | 30 | +0.0 [+0.0, +0.0] | +0.0 | 2799.6 (2663.2/136.3/0.0/0.0) | 0.0 / 2799.6 | 1 (34056) |

Controls: wall sum P+ positive, P0 negative (expected P+ positive, P0 negative); stale refused/rederived/revalidated True/True/True; corrupt 3/3 refused (decode; canonical bytes differ from the carried body; canonical bytes differ from the carried body).

Prediction vs measurement (wall):

| stream | predicted net(40) us [band] | measured sum us | in band | predicted break-even | measured break-even | measured Delta/hit median us [q1,q3] |
|---|---|---|---|---|---|---|
| S1_reverse | -665.6 [-1272.9, -218.5] | -7.1 | no | -1 | 2 | -5.6 [-160.4, +220.7] |
| S1_merge_P+ | +1564.9 [+326.3, +2298.8] | +2322.8 | no | 4 | 2 | +46.8 [-103.2, +355.7] |
| P0_no_payoff | -2535.1 [-2535.1, -2535.1] | -5769.2 | no | -1 | 30 | +0.0 [+0.0, +0.0] |

### run2: arm B = existing reuse with adaptation/repair (frozen protocol); commit 0012d413; binary sha256 3e966f267a49; load 15.81 (runnable 23/2357); checks 3058 failures 53 bad_rows 2; reconciliation OK (688 rows, 0 bad, unattributed 6742 us)

| stream | hits/misses | acq | adapt | refusals | rederiv | restarts | floors | neg transfer | wall net sum us | wall break-even | wall per-task median us [q1,q3] | saved_gross us | spent_learning us (acq/maint/repair/restart) | reinvested / unfunded us | templates (bytes) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| S1_reverse | 39/1 | 1 | 0 | 0 | 0 | 0 | ok | 0 | -906.6 | 2 | +19.1 [-70.2, +309.8] | -987.8 | 77.9 (75.2/2.6/0.0/0.0) | 0.0 / 77.9 | 1 (34056) |
| S1_merge_P+ | 39/1 | 1 | 0 | 0 | 0 | 0 | ok | 0 | -6519.3 | 4 | +39.4 [-680.0, +257.1] | -5918.2 | 111.6 (108.9/2.7/0.0/0.0) | 0.0 / 111.6 | 1 (34056) |
| S2_warmup_dev_seeds | 22/2 | 2 | 1 | 12 | 0 | 0 | VIOLATED | 11 | -13676.9 | -1 | -483.0 [-1472.5, -50.1] | -13171.5 | 201.5 (189.2/6.0/6.2/0.0) | 0.0 / 201.5 | 2 (68112) |
| S2_heldout_bindings_compositions | 39/1 | 1 | 0 | 13 | 0 | 0 | VIOLATED | 13 | -18924.4 | -1 | -252.8 [-1277.9, +26.4] | -18325.4 | 72.4 (57.5/14.9/0.0/0.0) | 0.0 / 72.4 | 3 (102168) |
| S3_interference | 35/5 | 5 | 3 | 48 | 0 | 0 | VIOLATED | 14 | -17619.1 | -1 | -276.8 [-1122.7, +180.3] | -15451.8 | 472.3 (422.9/23.5/26.0/0.0) | 0.0 / 472.3 | 5 (170280) |
| S4_changed_preconditions | 37/3 | 3 | 2 | 30 | 0 | 0 | VIOLATED | 9 | -14959.4 | 5 | -23.8 [-1346.9, +253.0] | -14421.1 | 268.0 (247.2/11.0/9.9/0.0) | 0.0 / 268.0 | 3 (102168) |
| S5_stale_generations | 30/10 | 10 | 0 | 9 | 9 | 0 | ok | 0 | +342.9 | 2 | -37.2 [-335.5, +515.3] | -636.5 | 713.7 (680.6/33.1/0.0/0.0) | 357.4 / 356.3 | 1 (34056) |
| S6_restart | 39/1 | 1 | 0 | 0 | 0 | 1 | ok | 0 | -6555.3 | 11 | -25.8 [-132.4, +152.8] | -5022.2 | 265.0 (72.3/2.5/0.0/190.2) | 0.0 / 265.0 | 1 (34056) |
| P0_no_payoff | 0/40 | 40 | 0 | 39 | 39 | 0 | ok | 0 | +6661.5 | 4 | -57.1 [-197.2, +210.5] | +0.0 | 2811.0 (2682.5/128.4/0.0/0.0) | 0.0 / 2811.0 | 1 (34056) |

Controls: wall sum P+ negative, P0 positive (expected P+ positive, P0 negative); stale refused/rederived/revalidated True/True/True; corrupt 3/3 refused (decode; canonical bytes differ from the carried body; canonical bytes differ from the carried body).

Prediction vs measurement (wall):

| stream | predicted net(40) us [band] | measured sum us | in band | predicted break-even | measured break-even | measured Delta/hit median us [q1,q3] |
|---|---|---|---|---|---|---|
| S1_reverse | -665.6 [-1272.9, -218.5] | -906.6 | yes | -1 | 2 | +19.1 [-70.2, +309.8] |
| S1_merge_P+ | +1564.9 [+326.3, +2298.8] | -6519.3 | no | 4 | 4 | +39.4 [-680.0, +257.1] |
| P0_no_payoff | -2535.1 [-2535.1, -2535.1] | +6661.5 | no | -1 | 4 | +0.0 [+0.0, +0.0] |

### run3-noadapt: arm B = reuse only (no adaptation/repair); commit 0012d413; binary sha256 3e966f267a49; load 16.54 (runnable 24/2376); checks 3058 failures 2 bad_rows 2; reconciliation OK (688 rows, 0 bad, unattributed 5503 us)

| stream | hits/misses | acq | adapt | refusals | rederiv | restarts | floors | neg transfer | wall net sum us | wall break-even | wall per-task median us [q1,q3] | saved_gross us | spent_learning us (acq/maint/repair/restart) | reinvested / unfunded us | templates (bytes) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| S1_reverse | 39/1 | 1 | 0 | 0 | 0 | 0 | ok | 0 | -1913.5 | 37 | -11.1 [-678.3, +562.2] | -1724.9 | 84.2 (81.5/2.8/0.0/0.0) | 0.0 / 84.2 | 1 (34056) |
| S1_merge_P+ | 39/1 | 1 | 0 | 0 | 0 | 0 | ok | 0 | -5999.7 | 2 | -153.9 [-877.4, +848.1] | -6093.2 | 102.0 (99.1/2.9/0.0/0.0) | 0.0 / 102.0 | 1 (34056) |
| S2_warmup_dev_seeds | 22/2 | 2 | 0 | 12 | 0 | 0 | ok | 0 | -10219.3 | -1 | -171.3 [-1007.3, +138.1] | -8389.8 | 176.4 (170.6/5.8/0.0/0.0) | 0.0 / 176.4 | 2 (68112) |
| S2_heldout_bindings_compositions | 39/1 | 1 | 0 | 13 | 0 | 0 | ok | 0 | +753.5 | 11 | +30.4 [-320.1, +477.5] | +3858.2 | 57.6 (54.5/3.2/0.0/0.0) | 0.0 / 57.6 | 3 (102168) |
| S3_interference | 35/5 | 5 | 0 | 48 | 0 | 0 | ok | 0 | -6206.0 | 2 | -23.3 [-828.0, +237.2] | -5813.0 | 413.6 (391.2/22.4/0.0/0.0) | 0.0 / 413.6 | 5 (170280) |
| S4_changed_preconditions | 37/3 | 3 | 0 | 30 | 0 | 0 | ok | 0 | -9832.5 | -1 | -67.8 [-868.7, +287.4] | -5595.3 | 259.3 (246.0/13.3/0.0/0.0) | 91.2 / 168.1 | 3 (102168) |
| S5_stale_generations | 30/10 | 10 | 0 | 9 | 9 | 0 | ok | 0 | -6789.5 | 4 | +10.3 [-793.9, +155.3] | -3850.5 | 697.0 (664.7/32.3/0.0/0.0) | 278.4 / 418.6 | 1 (34056) |
| S6_restart | 39/1 | 1 | 0 | 0 | 0 | 1 | ok | 0 | +995.6 | 2 | -7.1 [-127.0, +406.4] | +982.7 | 224.0 (72.8/2.5/0.0/148.7) | 148.7 / 75.4 | 1 (34056) |
| P0_no_payoff | 0/40 | 40 | 0 | 39 | 39 | 0 | ok | 0 | +2824.0 | 2 | -38.2 [-157.9, +239.6] | +0.0 | 2783.5 (2657.0/126.5/0.0/0.0) | 0.0 / 2783.5 | 1 (34056) |

Controls: wall sum P+ negative, P0 positive (expected P+ positive, P0 negative); stale refused/rederived/revalidated True/True/True; corrupt 3/3 refused (decode; canonical bytes differ from the carried body; canonical bytes differ from the carried body).

Prediction vs measurement (wall):

| stream | predicted net(40) us [band] | measured sum us | in band | predicted break-even | measured break-even | measured Delta/hit median us [q1,q3] |
|---|---|---|---|---|---|---|
| S1_reverse | -665.6 [-1272.9, -218.5] | -1913.5 | no | -1 | 37 | -11.1 [-678.3, +562.2] |
| S1_merge_P+ | +1564.9 [+326.3, +2298.8] | -5999.7 | no | 4 | 2 | -153.9 [-877.4, +848.1] |
| P0_no_payoff | -2535.1 [-2535.1, -2535.1] | +2824.0 | no | -1 | 2 | +0.0 [+0.0, +0.0] |

### run4: arm B = existing reuse with adaptation/repair (frozen protocol); commit 64be1932; binary sha256 4849084542ca; load 69.02 (runnable 66/2454); checks 3060 failures 52 bad_rows 1; reconciliation OK (688 rows, 0 bad, unattributed 5153 us)

| stream | hits/misses | acq | adapt | refusals | rederiv | restarts | floors | neg transfer | wall net sum us | wall break-even | wall per-task median us [q1,q3] | CPU net us | CPU break-even | CPU Delta/hit us [q1,q3] | saved_gross us | spent_learning us (acq/maint/repair/restart) | reinvested / unfunded us | templates (bytes) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| S1_reverse | 39/1 | 1 | 0 | 0 | 0 | 0 | ok | 0 | +10549.9 | 2 | +21.7 [-135.0, +980.2] | -87.7 | -1 | -0.7 [-2.1, +0.2] | +9443.0 | 76.9 (74.4/2.4/0.0/0.0) | 0.0 / 76.9 | 1 (34056) |
| S1_merge_P+ | 39/1 | 1 | 0 | 0 | 0 | 0 | ok | 0 | -10045.0 | 14 | -82.7 [-903.0, +282.3] | +1486.9 | 3 | +40.2 [+37.6, +42.5] | -9032.3 | 100.3 (97.7/2.6/0.0/0.0) | 0.0 / 100.3 | 1 (34056) |
| S2_warmup_dev_seeds | 22/2 | 2 | 1 | 12 | 0 | 0 | VIOLATED | 11 | -42157.1 | -1 | -631.6 [-1896.8, +12.9] | +277.4 | 10 | +18.1 [+2.5, +37.9] | -29950.1 | 202.9 (191.0/5.8/6.0/0.0) | 0.0 / 202.9 | 2 (68112) |
| S2_heldout_bindings_compositions | 39/1 | 1 | 0 | 13 | 0 | 0 | VIOLATED | 13 | -11588.2 | 3 | -81.8 [-1199.8, +8.4] | +517.4 | 2 | +2.0 [+0.7, +38.9] | -24216.6 | 58.3 (54.8/3.5/0.0/0.0) | 0.0 / 58.3 | 3 (102168) |
| S3_interference | 35/5 | 5 | 3 | 48 | 0 | 0 | VIOLATED | 14 | -11376.7 | 11 | -464.7 [-1123.1, +6.7] | -159.0 | -1 | +1.2 [-3.2, +2.5] | -5818.8 | 470.2 (424.4/21.9/23.9/0.0) | 0.0 / 470.2 | 5 (170280) |
| S4_changed_preconditions | 37/3 | 3 | 2 | 30 | 0 | 0 | VIOLATED | 9 | -54704.4 | -1 | -43.3 [-1210.1, +15.4] | -264.0 | -1 | -0.1 [-2.2, +1.3] | -48685.8 | 273.0 (250.3/11.7/11.0/0.0) | 0.0 / 273.0 | 3 (102168) |
| S5_stale_generations | 30/10 | 10 | 0 | 9 | 9 | 0 | ok | 0 | -10950.9 | 4 | -49.2 [-245.8, +33.5] | -713.9 | -1 | -1.2 [-2.3, -0.5] | -749.4 | 696.0 (667.1/28.9/0.0/0.0) | 349.7 / 346.2 | 1 (34056) |
| S6_restart | 39/1 | 1 | 0 | 0 | 0 | 1 | ok | 0 | -122.6 | 8 | +18.5 [-25.4, +93.0] | -233.6 | -1 | +1.2 [+0.4, +2.8] | +252.6 | 300.5 (70.4/2.5/0.0/227.5) | 0.0 / 300.5 | 1 (34056) |
| P0_no_payoff | 0/40 | 40 | 0 | 39 | 39 | 0 | ok | 0 | -9247.1 | -1 | -71.9 [-285.5, -4.6] | -2762.0 | -1 | +0.0 [+0.0, +0.0] | +0.0 | 2764.4 (2642.1/122.3/0.0/0.0) | 0.0 / 2764.4 | 1 (34056) |

Controls: wall sum P+ negative, P0 negative; on cognition CPU: P+ positive, P0 negative (expected P+ positive, P0 negative); stale refused/rederived/revalidated True/True/True; corrupt 3/3 refused (decode; canonical bytes differ from the carried body; canonical bytes differ from the carried body).

Prediction vs measurement (wall):

| stream | predicted net(40) us [band] | measured sum us | in band | predicted break-even | measured break-even | measured Delta/hit median us [q1,q3] |
|---|---|---|---|---|---|---|
| S1_reverse | -665.6 [-1272.9, -218.5] | +10549.9 | no | -1 | 2 | +21.7 [-135.0, +980.2] |
| S1_merge_P+ | +1564.9 [+326.3, +2298.8] | -10045.0 | no | 4 | 14 | -82.7 [-903.0, +282.3] |
| P0_no_payoff | -2535.1 [-2535.1, -2535.1] | -9247.1 | no | -1 | -1 | +0.0 [+0.0, +0.0] |

Prediction vs measurement (cognition CPU):

| stream | predicted net(40) us [band] | measured net us | in band | predicted break-even | measured break-even | measured Delta/hit median us [q1,q3] |
|---|---|---|---|---|---|---|
| S1_reverse | -39.2 [-100.4, +41.3] | -87.7 | yes | 81 | -1 | -0.7 [-2.1, +0.2] |
| S1_merge_P+ | +1725.8 [+1129.7, +1835.3] | +1486.9 | yes | 4 | 3 | +40.2 [+37.6, +42.5] |
| P0_no_payoff | -2738.4 [-2738.4, -2738.4] | -2762.0 | no | -1 | -1 | +0.0 [+0.0, +0.0] |

### run5-noadapt: arm B = reuse only (no adaptation/repair); commit 64be1932; binary sha256 4849084542ca; load 68.62 (runnable 65/2456); checks 3060 failures 2 bad_rows 2; reconciliation OK (688 rows, 0 bad, unattributed 4814 us)

| stream | hits/misses | acq | adapt | refusals | rederiv | restarts | floors | neg transfer | wall net sum us | wall break-even | wall per-task median us [q1,q3] | CPU net us | CPU break-even | CPU Delta/hit us [q1,q3] | saved_gross us | spent_learning us (acq/maint/repair/restart) | reinvested / unfunded us | templates (bytes) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| S1_reverse | 39/1 | 1 | 0 | 0 | 0 | 0 | ok | 0 | -11982.2 | -1 | -5.8 [-195.2, +19.3] | -54.4 | -1 | +0.4 [-0.7, +0.9] | -9748.3 | 73.9 (71.5/2.4/0.0/0.0) | 0.0 / 73.9 | 1 (34056) |
| S1_merge_P+ | 39/1 | 1 | 0 | 0 | 0 | 0 | ok | 0 | -1803.5 | 2 | +43.1 [-96.3, +983.3] | +1440.9 | 3 | +38.0 [+34.2, +44.4] | -2733.9 | 96.2 (93.4/2.8/0.0/0.0) | 0.0 / 96.2 | 1 (34056) |
| S2_warmup_dev_seeds | 22/2 | 2 | 0 | 12 | 0 | 0 | ok | 0 | +5456.9 | 4 | +21.6 [-99.3, +276.1] | +265.9 | 12 | +18.6 [+0.4, +36.4] | +5493.7 | 167.7 (161.9/5.8/0.0/0.0) | 0.0 / 167.7 | 2 (68112) |
| S2_heldout_bindings_compositions | 39/1 | 1 | 0 | 13 | 0 | 0 | ok | 0 | +6710.4 | 5 | +11.5 [-89.2, +701.5] | +447.0 | 2 | +1.2 [-0.0, +36.9] | +6689.7 | 55.9 (52.7/3.2/0.0/0.0) | 0.0 / 55.9 | 3 (102168) |
| S3_interference | 35/5 | 5 | 0 | 48 | 0 | 0 | ok | 0 | +5095.2 | 2 | -21.6 [-131.5, +173.8] | -178.0 | -1 | -1.2 [-4.2, +2.1] | +6407.2 | 407.0 (384.5/22.5/0.0/0.0) | 0.0 / 407.0 | 5 (170280) |
| S4_changed_preconditions | 37/3 | 3 | 0 | 30 | 0 | 0 | ok | 0 | -15698.3 | -1 | -10.9 [-1001.3, +47.7] | -294.1 | -1 | -1.0 [-3.9, +0.4] | -15376.7 | 248.1 (236.3/11.8/0.0/0.0) | 0.0 / 248.1 | 3 (102168) |
| S5_stale_generations | 30/10 | 10 | 0 | 9 | 9 | 0 | ok | 0 | -16528.8 | -1 | -23.1 [-589.3, +241.4] | -713.3 | -1 | -1.2 [-2.0, +0.0] | -10060.9 | 692.7 (662.3/30.5/0.0/0.0) | 0.0 / 692.7 | 1 (34056) |
| S6_restart | 39/1 | 1 | 0 | 0 | 0 | 1 | ok | 0 | +14431.1 | 4 | -0.8 [-428.7, +715.3] | -255.6 | -1 | -0.4 [-1.1, +1.1] | +14474.9 | 251.5 (72.2/2.6/0.0/176.6) | 176.6 / 74.8 | 1 (34056) |
| P0_no_payoff | 0/40 | 40 | 0 | 39 | 39 | 0 | ok | 0 | +102600.1 | 30 | -61.0 [-244.6, +908.8] | -2723.2 | -1 | +0.0 [+0.0, +0.0] | +0.0 | 2751.2 (2627.0/124.1/0.0/0.0) | 0.0 / 2751.2 | 1 (34056) |

Controls: wall sum P+ negative, P0 positive; on cognition CPU: P+ positive, P0 negative (expected P+ positive, P0 negative); stale refused/rederived/revalidated True/True/True; corrupt 3/3 refused (decode; canonical bytes differ from the carried body; canonical bytes differ from the carried body).

Prediction vs measurement (wall):

| stream | predicted net(40) us [band] | measured sum us | in band | predicted break-even | measured break-even | measured Delta/hit median us [q1,q3] |
|---|---|---|---|---|---|---|
| S1_reverse | -665.6 [-1272.9, -218.5] | -11982.2 | no | -1 | -1 | -5.8 [-195.2, +19.3] |
| S1_merge_P+ | +1564.9 [+326.3, +2298.8] | -1803.5 | no | 4 | 2 | +43.1 [-96.3, +983.3] |
| P0_no_payoff | -2535.1 [-2535.1, -2535.1] | +102600.1 | no | -1 | 30 | +0.0 [+0.0, +0.0] |

Prediction vs measurement (cognition CPU):

| stream | predicted net(40) us [band] | measured net us | in band | predicted break-even | measured break-even | measured Delta/hit median us [q1,q3] |
|---|---|---|---|---|---|---|
| S1_reverse | -39.2 [-100.4, +41.3] | -54.4 | yes | 81 | -1 | +0.4 [-0.7, +0.9] |
| S1_merge_P+ | +1725.8 [+1129.7, +1835.3] | +1440.9 | yes | 4 | 3 | +38.0 [+34.2, +44.4] |
| P0_no_payoff | -2738.4 [-2738.4, -2738.4] | -2723.2 | no | -1 | -1 | +0.0 [+0.0, +0.0] |

