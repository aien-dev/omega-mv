# ORIG4-SHADOW-v2: per-decision overhead of the shadow advisor (omega-mv#731)

Overhead campaign of benchmarks-mv `docs/orig3/PROTOCOL-v2.md` s.4 at this branch's commit
`85641e1e` (v2 admission, retrieval advisor kind), over the post-freeze collection C2 (1938
records, 1714 admitted under v2), candidate image aien-train `mlp-scratch-v2-seed19024.img`
(sha256 `16abb8fc...965c`). Host CPU only, `nice -n 19`; load 4.33 before and 4.30 after;
4 established TCP connections on 18091 before and after (nothing sent to it). GB10 NOT_RUN.

```
ORIG4_OVERHEAD=1 ORIG4_REPS=7 ORIG4_RECORDS=<C2> ORIG4_ALLOW=<allow-v2.txt> \
ORIG4_IMAGE=<candidate image> OUT=$PWD/evidence/ORIG4-SHADOW-v2/overhead nice -n 19 sh tests/orig4/run.sh
```

Medians of 7 interleaved repetitions (wall us): disabled 31291, MLP scorer 169856, retrieval
141121; 1714 scored decisions each. Per decision: MLP 80.84 us, retrieval 64.08 us (record
admission, features, inference or nearest-neighbour scan, argmin, fallback checks, one output
line). Against a median decision of a few hundred microseconds of matvec work this overhead
decides the v2 verdict: **NO-GO** (benchmarks-mv `evidence/ORIG3-SELECTOR-v2/`). The advisor
stays default-off and advisory; nothing in `src/` reads it.
