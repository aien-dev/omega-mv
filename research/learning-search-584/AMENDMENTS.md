# AMENDMENTS to FREEZE.md (recorded, never edited into the freeze)

## A1 (2026-10-19, after calibration, before the sealed run): shuffled-feedback control wording
FREEZE.md section 8 said the shuffled-feedback control must make learning arms "not commit
wrong programs (success must drop to abstention, not to wrong answers)". That clause cannot
be met by any arm and was mis-specified: under shuffled feedback the arm is handed
examples of another objective, solves them correctly (0 inconsistent commits), and the
sealed check against the hidden rule of the stated key scores the answer wrong. What the
control can show, and what is now the criterion, is: (a) learning arms do not beat A under
shuffled feedback (equal success and wrong counts), and (b) no arm commits a program
inconsistent with the feedback it was given. Calibration: A/B/C0/C1 all 1, 1, 2 successes
and 208, 217, 212 wrong on seeds 0x5851..0x5853, bit-identical across arms.

## A2 (2026-10-19, after the restart check, before the sealed run): C's ordering counts are not persisted
FREEZE.md section 9 said a cut-and-restart run must reproduce the counts of an uncut run.
B does (0 differing rows). C1 differs on 53 row cells of 248 after the cut because its
shape and constant frequency counts (part of z) live only in memory; only the table and
the realizations go through Cortex, and those reload bound (32 entries, 16 realizations
with recomputed triple ids). The sealed verdict does not depend on this (C0 is selected
by the same table mechanism); the gap is reported under recovery, and persisting the
counts as a Cortex record is listed as follow-up work, not done here.

## A3 (2026-10-19, before the sealed run): C1 is also run on the sealed seeds
The freeze runs A, B and the selected C on the sealed seeds. C1 is run as well, labelled
unselected and reported for completeness (SELECTION.md explains why the frozen rule
selected on noise). The verdict uses C0 only.
