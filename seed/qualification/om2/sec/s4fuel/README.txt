E14/E16 expansion-fuel fixtures (#748, L836). Expected lines come from oracle/s4fuel/fuel_e16.py, which
implements erratum E16: an unrolled loop is charged K at its own line when it starts, before any copy
expands; an inline expansion is charged 1 at its call line; error 14 names the construct whose charge takes
the total above 65536. fuel.py is the superseded pre-E16 model (pre-order, 1 per copy), kept for the record.
- fuel-d2: 64 + 64*64 = 4160 units; compiles and runs with exit 0.
- fuel-d3: error 14 at line 7, the c loop. Hand check: a = 64; each a-copy adds b 64 + 64 c-loops x 64 = 4160;
  after 15 a-copies the total is 62464; the 16th adds b 64 (62528), then 47 c-loops reach exactly 65536,
  and the 48th c-loop (line 7) makes 65600.
- fuel-d4: error 14 at line 8 (the d loop), under a 2 s compile deadline.
- fuel-inl: error 14 at line 7, the f3 unroll. Total 65537.
Deadlines make runaway compile work INVALID (timeout) or FAIL (no error), never PASS.
Compile times: evidence/phaseB/s4fuel/compile-times.txt. S2 baseline defect (c23b3376): no error 14 at all.
