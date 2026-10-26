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
Per-function revision (coordinator, after CompilerS4's argument): oracle/s4fuel/fuel_e16f.py parses each fixture
and compiles every function, inline ones included, in source order with its own fuel (E14 per function, OMEGESE
9.13). fuel-d2/d3/d4 are unchanged (2: compiles, 3: line 7, 4: line 8). fuel-inl: f4 0, f3 128, f2 8320, then
f1 standalone exhausts at line 8 (the f4 call): 64 + 7 x 8321 = 58311; +1 +64 = 58376; 55 x 129 = 65471; the
56th f3 loop reaches 65536 and the next f4 call makes 65537. fuel-inl-mainfirst (main first): main exhausts at
line 11, the f3 unroll. fuel_e16.py (main only) is superseded.
