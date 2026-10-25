E14 expansion-fuel fixtures (#748, L836). Written from erratum E14 alone; the expected lines come from
oracle/s4fuel/fuel.py (pre-order charge: a unit is charged when its copy or expansion starts, and the unit
that takes the total above 65536 names the error line).
- fuel-d2: 64 + 4096 = 4160 units, compiles, runs exit 0 (E14 control 1).
- fuel-d3: error 14 at line 6 (the b loop) (E14 control 2).
- fuel-d4: E14's 1.58 s case; error 14 at line 8, under a 2 s compile deadline.
- fuel-inl: inline variant (f1..f4, each under a 64-copy unroll); error 14 at line 8 (the f4 call).
Deadlines (gate `deadline`) make runaway compile work INVALID (timeout) or FAIL (no error), never PASS.
Compile times are recorded in evidence/phaseB/s4fuel/compile-times.txt.
S2 baseline defect (c23b3376): d3, d4 and inl compile with exit 0 and no error. d4 takes 1.58 s.
