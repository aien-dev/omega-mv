# ENTITY-276 h40: does the retained plan pay back over longer horizons?

Follow-up to the ENTITY-276 run of record (../README.md), ledger L166. Frozen rules and
prediction: FREEZE.md (committed 5180a697 before the run of record). Amendments: none.
The earlier run of record and its folder are untouched.

## What was shown, in plain words

After a restart, keeping the learned plan does **not** make the entity measurably cheaper
over 3, 6, 10 or 40 repeated instructions when it is compared with an entity that forgets
the plan at restart but relearns it on its first instruction (the earlier control). On
planning work alone, keeping it costs more at every horizon, by an almost constant amount
(384,368 to 399,488 ns), because rebuilding it from the journal costs more than the one
search it saves, and the horizon cannot change that: the forgetful entity pays for one
search per restart, however long the run is. The prediction said exactly this.

Against an entity that never keeps any plan (it searches every time), keeping the plan wins
on planning work from about 6 instructions on: measured break-even 6.14 instructions,
predicted 5.83 (band 3.16 to 11.01). That saving is real but small, and over 40
instructions it does not show up in total process CPU: the entity that never keeps a plan
used the least total CPU in every run, probably (inferred, not isolated) because the reuse path writes one more journal
record per instruction (the plan index) and the saved search is about 75 us next to a median of 14.8 to
15.4 ms of execution and disk writes per instruction (r1 h40r medians per arm).

A second finding: one World can only carry out 6 instructions after a restart. Each
finished plan run leaves its 35 objects and 34 reactions in the World, which holds 256
objects. Horizons 10 and 40 ran only because the test program releases each finished run
itself (MISSING PREREQUISITE F below).

## Run of record

| Field | Value |
|---|---|
| date | 2026-10-20, 14:05:53Z to 14:14:58Z (receipt 544.9 s) |
| host | spark-b87b (DGX Spark, GB10) |
| head sha | 5180a697affe71394ea4639464efb64b52e03b9a (build of record and run, same commit) |
| build | `quiet-build nice -n 10 make -j4 PHYSICS_DIR=~/workspace/physics-pf-6d7cf0d4 AIENOS_LOCK_REPO=~/workspace/aienos-repo OUT_DIR=~/workspace/targets/omega-entity-276-h40/record .../tests-research/entity276`, exit 0 |
| command | run-of-record/command.txt: `quiet-measure flock /tmp/aien-gb10.lock spark-receipt entity-276-h40 -- env ENTITY276_BIN=... PILOT_OUT=... bash tests/research/entity276/measure-h40.sh` (run.sh once, then 5 repetitions of h3, h6, h6r, h10r, h40r) |
| load wait | load1 never went under 15: polled every 60 s from 12:21Z, 86 to 138; the 90-minute cap hit at 13:51:00Z at load1 133.15 (run-of-record/load-wait.log). The measurement then waited 14 minutes in the quiet-lock queue (an aien-test run held it; /tmp/aien-gb10.lock had been held by the model server for 17 h) and started at 14:05:53Z under both locks |
| load / MemAvailable | start 94.85 / 72,387,944 KiB; end 93.39 / 63,973,068 KiB; per process in run-of-record/runs.csv and every CSV header. **All numbers were taken at load 90.34 to 96.80, six times the brief's limit of 15 (calc:88b798c9)** |
| instrument | CLOCK_PROCESS_CPUTIME_ID and CLOCK_THREAD_CPUTIME_ID per step, getrusage, journal file size, rx_world_footprint; receipt run-of-record/receipt-entity-276-h40.json (~/yardsticks/receipts/2026-10-20-entity-276-h40.json) |
| result | every process exit 0: run.sh `RESULT fails=0` with every mutant red (r0/run-verdict.txt); all 25 horizon processes passed every check (warm and lazy 0 searches and H hits, cold 1 search and H-1 hits, forget H searches and 0 hits, every goal held, identity against process 1, every release call succeeded) |
| energy | UNMEASURED for this work. The receipt reads 133,181 J of whole-UPS energy over 545 s with a negative increment over the idle baseline (-18,298 J): the box's other work changed during the run, and the run alternates processes, so no joule figure is claimed |
| claim_strength | measured (CPU columns, counts); inferred (the cause of the forget arm's lower total CPU) |

## G5 verdict per horizon (frozen rule: warm against cold, cumulative process CPU over 5a + objectives)

| Variant | warm min / median / max ns | cold min / median / max ns | verdict | warm lower (paired, same process) |
|---|---|---|---|---|
| h3 (control) | 20,600,656 / 21,026,320 / 24,566,672 | 20,945,168 / 21,292,864 / 23,576,912 | **INCONCLUSIVE** (reproduces the earlier verdict) | 3 of 5 |
| h6 | 43,623,008 / 44,571,904 / 47,243,200 | 43,919,152 / 44,423,168 / 47,673,472 | **INCONCLUSIVE** | 4 of 5 |
| h6r | 47,786,320 / 48,869,584 / 49,534,992 | 47,963,088 / 49,391,328 / 51,288,720 | **INCONCLUSIVE** | 4 of 5 |
| h10r | 87,793,152 / 88,061,392 / 89,322,128 | 88,209,344 / 88,759,120 / 91,772,480 | **INCONCLUSIVE** | 5 of 5 |
| h40r | 613,317,936 / 619,331,504 / 626,847,680 | 619,961,504 / 625,505,248 / 633,435,184 | **INCONCLUSIVE** | 5 of 5 |

Every sum has an aien-calc receipt in run-of-record/sums.csv (one per run, variant and
arm). Warm searched 0 times in every run. The paired column is the frozen secondary, not the
gate: at h10r and h40r warm used less process CPU than cold in all 5 runs (h40r warm minus
cold -3,186,704 calc:e66f5578, -6,587,504 calc:4f33b336, -4,611,232 calc:017a2a5d,
-6,643,568 calc:1f798b68, -9,279,280 ns calc:c915e561), but the ranges across runs overlap,
so by the frozen rule G5 is INCONCLUSIVE at every horizon. Planning CPU, where warm is
higher, does not explain that sign; its cause is not established here.

## Planning CPU (reported, same range rule)

| Variant | warm median | cold median | lazy median | forget median | warm - cold (medians) |
|---|---|---|---|---|---|
| h3 | 480,848 | 96,480 | 470,960 | 242,576 | 384,368 (calc:19a9a1e8) |
| h6 | 503,152 | 115,536 | 492,464 | 495,600 | 387,616 (calc:da6abb09) |
| h6r | 502,400 | 118,896 | 486,832 | 476,336 | 383,504 (calc:e9a027e2) |
| h10r | 528,576 | 142,272 | 509,104 | 802,304 | 386,304 (calc:ea5340a9) |
| h40r | 736,192 | 336,704 | 706,336 | 3,280,560 | 399,488 (calc:d50a8ed7) |

(ns; min and max per arm in run-of-record/verdicts.txt.) Warm against cold: warm higher in
every run at every horizon (warm min above cold max). The gap stays flat as H grows, as
model B predicted (predicted -588,736 ns for warm, band -685,840 to -311,376; measured gap
384 to 399 us, inside the band).

Warm against forget (model A): warm higher at h3 and h6r, ranges overlap at h6, warm lower
in every run at h10r and h40r. Measured C (median warm rebuild at h40r) 461,424 ns; measured
Delta (median over runs of the saving per objective) 75,162.8 ns (calc:235c53fc,
17abe1fc, 8ae9e150, a70a0941, 7d439925); **measured H* = 6.14 (calc:2680d554) against
predicted 5.83 (calc:9408aba7), band 3.16 to 11.01.** Measured Delta is smaller than the
predicted 121,856 ns (the search ran cheaper here than in the earlier run's objective 2);
the measured rebuild is cheaper too, so the ratio landed close.

Lazy against warm: lazy planning CPU lower in every run at h6, h6r, h10r and h40r (h40r
medians 29,856 ns lower, calc:92c3eb72), overlapping at h3. The lazy rebuild itself
measured 441,376 to 453,456 ns against warm's 451,488 to 478,816 ns. Laziness saves no
search (every horizon has a lookup that needs the template); why the rebuild runs slightly
cheaper inside the first lookup than at boot is not established.

## Total process CPU, warm against forget (reported)

At h40r the forget arm used the least process CPU in every run (warm minus forget 3,000,704
calc:f2b1ce42, 2,172,176 calc:07d67ca6, 1,842,896 calc:3353335c, 1,758,032 calc:b9d1ee9a,
529,440 ns calc:86c15ee8), ranges overlapping. Inferred cause (not isolated): every reused
objective publishes the plan index and journals one more record (110 against 109 records,
40,144 against 39,800 B per objective in r1 h40r), each journal append is synchronous, and
the template's first use runs the sequential reference once. A saved search (75 us) is small
next to the 6,559,584 to 24,394,688 ns of process CPU per objective in r1 h40r at this load.

## Release of finished runs (h6 against h6r)

Release adds 4,297,680 ns of process CPU to the warm arm over 6 objectives (medians,
calc:b084fd92), 716,280 ns per objective (calc:5749bebe), the same work in every arm. h6
and h6r give the same G5 verdict.

## Prediction against measurement

| Item | Predicted (FREEZE section 4) | Measured |
|---|---|---|
| G5 warm vs cold, process CPU | INCONCLUSIVE at every H | INCONCLUSIVE at every H |
| planning, warm vs cold | warm higher at every H, flat gap -588,736 ns (band -685,840 to -311,376) | warm higher at every H, gap 384,368 to 399,488 ns |
| planning, warm vs forget, H* | 5.83 (band 3.16 to 11.01) | 6.14 (crossover between h6 and h10r) |
| lazy vs warm | equal within the rebuild's spread | lazy lower from h6 on by 10,688 (calc:2a477e9d), 15,568 (calc:a4632c6b), 19,472 (calc:c70c9aeb) and 29,856 ns (calc:92c3eb72) |
| warm vs forget, process CPU at H 40 | inside the spread | inside the spread by the range rule; forget lower in 5 of 5 paired runs |


## Checks that must fail (mutants)

- The earlier mutants (SHADOW, VIEWWRITE, IDLEPOLL, SHADOW restart, TAMPER) went red in the
  run of record with the same bad rows as before: 1, 2, 1, 1, 7 (run-of-record/r0/run-verdict.txt).
- The horizon-mode checks were given their own mutants after the run of record (commit
  dc204484; the hooks are inert without the ENTITY276_HMUTANT variable, and the run-of-record
  binary did not contain them). Dev build, H 3 with all arms and --release
  (dev-after-record/hmutants-verdict.txt): clean run exit 0, 0 bad rows; FORGETSTORE (forget
  arm stores a template) red, 1 bad row; LAZYSKIP (lazy arm never rebuilds) red, 2; COLDKEEP
  (cold arm rebuilds theta) red, 1; DOUBLERELEASE (each run released twice) red, 3. Not
  of record, unreceipted.
- The World limit itself: horizon 10 without --release exits 1; objects_live climbs 55, 90,
  125, 160, 195, 230 and sticks at 256 from the seventh objective
  (dev-after-record/limit-h10-no-release.txt).

## MISSING PREREQUISITES

- F. (found here) No release of a finished plan run in the runtime: rx_plan_execute leaves
  the lowered cells, the run token and the reactions in the World (35 objects and 34
  reactions per objective, measured with rx_world_footprint), and a World holds
  RX_MAX_OBJECTS = 256 objects, so one session fails during its seventh objective after a
  restart. The harness works around it with `--release` (rx_world_remove_reaction and
  rx_world_retire on the run's AgLowered reactions, cells and run token). A runtime release
  call, or the executor releasing its own run, is a separate reviewed change.
- A to E from ../README.md still stand.

## UNVERIFIED (choices the contract did not pin)

Four arms share one process and one capability library, each with its own World, journal
copy and plan cache; process CPU per step includes the other arms' blocked threads. The
forget arm is added as the comparator the r = 1 model needs. Release is harness-side.
Williams order over four arms; H 3 uses three of the four rows. One pilot journal feeds all
25 horizon processes. Single seed (276), one objective shape. All at load 90 to 97.

## Files

- FREEZE.md: arms, horizons, rule, prediction (receipts).
- run-of-record/: command.txt, load-wait.log, runs.csv (load and memory per process),
  sums.csv (every cumulative sum with its aien-calc receipt), verdicts.txt (min/median/max
  per arm and every comparison), the receipt, r0/run-verdict.txt (run.sh and mutants), and
  r1..r5/<variant>/horizon.out plus per arm the step CSV (with the order and footprint
  columns), summary and identity JSON. Screens and journals are not committed.
- dev-after-record/: hmutants-verdict.txt, limit-h10-no-release.txt (not of record).
- Harness: tests/research/entity276/entity276.c (mode `horizon`), measure-h40.sh, hmutants.sh,
  wait-load.sh, aggregate-h40.sh.
