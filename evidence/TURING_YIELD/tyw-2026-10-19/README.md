# TYW evidence (TY-4/TY-5 wall amendment, run 2026-10-20 01:59Z to 02:14Z)

Protocol docs/turing/TURING_YIELD_ENERGY_PROTOCOL_V0.md amendment 2; result docs/turing/TURING_YIELD_TYW_RESULT.md.
raw/*.jsonl are the unchanged ty_energy_window records (meter_ok 0, no aien_spbm hwmon), raw/SHA256SUMS, plan files;
offsets.txt maps each window's monotonic GO time to epoch time; sensors-slice.csv is the wall sensor log for the run;
receipts/ holds the spark-receipt copy; build-and-fixture-tests.log the build and the 54-check + 9-fixture runs;
reducer.txt the tools/ty_energy_wall.c output (reproduces from this folder). Verdict: check (a) FAIL on S6 and S2,
check (b) NOT RUN.
