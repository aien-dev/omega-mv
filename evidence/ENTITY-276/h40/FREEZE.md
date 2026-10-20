# ENTITY-276 h40 FREEZE (precommitted before any run of record)

Follow-up to the ENTITY-276 run of record (evidence/ENTITY-276/README.md, head 21913232),
ledger L166. Goal: make G5 (retained knowledge reduces cumulative cost after restart)
decidable by measuring longer horizons with the arms interleaved inside one process.
Base sha 2191323242f7af2a67fec288729c6b84a3ba2e50. Harness commit da2a8a2f
(tests/research/entity276/entity276.c, mode `horizon`). No runtime, visor or allen edits.
The earlier FREEZE (evidence/ENTITY-276/FREEZE.md) still governs everything not named here.
Changes after this file is committed go to AMENDMENTS.md in this folder, never into this file.

Disclosure: development runs on this machine (not of record, unreceipted) were seen before
this file was written: run.sh with all mutants red (same bad rows as the run of record),
and horizon runs at H 3, 10 and 40 whose printed summaries show searches, hits and the
rebuild CPU per arm (475,088 to 738,112 ns). No cumulative CPU sums were computed or looked at.
The development runs also found the limit in section 2, which shaped the horizons below.

## 1. Arms (one process, one copy of the pilot journal each)

Every arm restarts from its own copy of one pilot journal (process 1 of run.sh), boots its
own World, attaches its own journal copy, has its own plan cache, and then solves the same
repeated objectives k = 2 .. H+1 (frozen objective shapes of the earlier FREEZE section 1).

| Arm | Retained knowledge at restart | Learns in its session |
|---|---|---|
| warm | theta rebuilt from the journal at boot (step 5a), as the earlier `restart` | yes |
| cold | template records ignored, as the earlier `restart-cold` (the G5 comparator) | yes |
| lazy | theta rebuilt from the journal by the first lookup that needs it (inside objective 2's planning CPU), not at boot | yes |
| forget | template records ignored | no: never stores a template, so every objective searches |

The forget arm is added by this FREEZE: it is the comparator the r = 1 model of section 4
describes (every opportunity saves one search). It is reported, it is not the gate.

Order: arms open (steps 5a, 5b) in the order warm, cold, lazy, forget. With `--interleave`,
objective i runs once per arm before objective i+1 starts, in a balanced (Williams) order
of the four arms: row i is (0, 1, 3, 2) + i mod 4, so for every pair of arms each goes
first in 2 of every 4 objectives. H = 3 uses rows 0 to 2 only (not balanced; disclosed).
Process CPU (CLOCK_PROCESS_CPUTIME_ID) per step includes the other arms' blocked reaction
threads; the earlier G2 control put a whole idle World at 73,984 to 193,968 ns over 3 s.

## 2. Runtime limit found in development, and the harness-side release

A World holds RX_MAX_OBJECTS = 256 objects. rx_plan_execute leaves each finished run's
lowered cells, run token and reactions in the World: measured 35 objects and 34 reactions
per executed objective, so one restarted session reaches 256 objects during its seventh
objective, which then fails (goal not reached, template refused afterwards). Horizons
above 6 therefore need `--release`: after each run quiesces, the harness removes that run's
reactions (rx_world_remove_reaction) and retires its cells and run token (rx_world_retire),
all public World calls, a CHECK on every one. The World then stays at 20 live objects and
0 active reactions. This is recorded as MISSING PREREQUISITE F (no release of a finished
plan run in the runtime). Release adds the same work to every arm's objectives.

## 3. Horizons and runs of record

| Variant | H | Flags (all: `--interleave --lazy-rebuild --forget`) | Role |
|---|---|---|---|
| h3 | 3 | none extra | control: must reproduce the earlier G5 verdict (INCONCLUSIVE) |
| h6 | 6 | none extra | largest horizon the runtime allows without release |
| h6r | 6 | `--release` | bridge: same horizon with release, shows what release changes |
| h10r | 10 | `--release` | |
| h40r | 40 | `--release` | |

Runs of record: 5 repetitions; each repetition runs the five variants in the order above,
each as its own process on a fresh copy of the same pilot journal. Before the repetitions,
run.sh runs once (pilot, determinism, cold control, idle control, every mutant) and must
end `RESULT fails=0` with every mutant red; its pilot journal and identity file feed all
horizon processes. Seed 276 only. Journal mode CX_OPEN_SYNC as before.

Load: the run of record starts when the 1-minute load is under 15, polled every 60 s for
at most 90 minutes; after that it starts anyway and the load is recorded. MemAvailable must
be above 12 GiB at start. Command: `quiet-measure flock /tmp/aien-gb10.lock spark-receipt
entity-276-h40 -- ... measure-h40.sh`. Load and MemAvailable are recorded at start and end
of every process (CSV headers) and of every repetition (runs.csv).

## 4. Prediction before measuring (aien-calc receipts, from the earlier run of record)

Inputs: C = template rebuild CPU after restart, 455,840 to 754,368 ns (5 runs), central
710,592 ns (run 1, the only per-run value committed). Delta = search CPU saved per hit =
cold objective 2 planning CPU minus warm objective 2 planning CPU: median 139,664 - 17,808 =
121,856 ns (calc:a2d40c14), band 68,528 (calc:b45f83c8) to 144,464 ns (calc:29143293) from
min and max. p = 1 (15 of 15 hits). r = 1. m = 0 (the hit-path lookup is inside Delta).

Model A, warm against forget (the #246 E03 model, r = 1): net(H) = H p Delta - C.
Predicted break-even H* = C / (p Delta) = 5.83 (calc:9408aba7), band 3.16 (calc:d568d1b4)
to 11.01 (calc:9ded6848).

| H | net(H) central ns | band ns |
|---|---|---|
| 3 | -345,024 (calc:8a2d76a3) | -548,784 (calc:83128cc1) to -22,448 (calc:0525330e) |
| 6 | 20,544 (calc:1f4ec26f) | -343,200 (calc:77285824) to 410,944 (calc:0f8a2af9) |
| 10 | 507,968 (calc:02678042) | -69,088 (calc:f3ffd69d) to 988,800 (calc:fc66354c) |
| 40 | 4,163,648 (calc:c6b0acdc) | 1,986,752 (calc:b238b554) to 5,322,720 (calc:791f7b92) |

Model B, warm against cold (the G5 comparator). The cold arm learns in its own session, so
warm saves one search per restart at any horizon: net(H) = p Delta - C for every H (the
r = 0 shape), central -588,736 ns (calc:588a54cf), band -685,840 (calc:29b26528) to
-311,376 ns (calc:1c4ad072). No break-even exists while C > Delta. Prediction: on planning
CPU, warm costs more than cold at every horizon; the horizon does not change that.

Lazy: the same rebuild, moved into objective 2. Predicted: lazy planning CPU equals warm
within the rebuild's own spread; no saving from laziness, because every horizon here has
at least one lookup that needs the template.

Process CPU (the gate column): per objective it was 11.4 to 17.8 ms in the earlier run, so
run-to-run spread of the cumulative sum grows with H while Model B's effect stays below
0.7 ms. Prediction: G5 INCONCLUSIVE (ranges overlap) at every horizon; the model A effect
(at most 5.3 ms at H 40) is also predicted to stay inside the process CPU spread.

## 5. Gate rule (unchanged from the earlier FREEZE) and reported columns

Per horizon variant, over the 5 runs: cumulative proc_cpu_ns over steps 5a + 5_obj2 ..
5_obj(H+1), per arm and run.

| Gate | PASS | FAIL | otherwise |
|---|---|---|---|
| G5(H): warm against cold | warm max < cold min, with 0 warm searches in every run | warm min > cold max | INCONCLUSIVE |

Every horizon process must also pass all its checks (exit 0): warm and lazy 0 searches and
H hits; cold 1 search and H-1 hits; forget H searches, 0 hits, empty cache; every objective
committed with the goal holding; identity against process 1 at restart; with `--release`,
every release call succeeds. A failed check makes that variant NOT_RUN for the gate.

Reported with the same range rule, not the gate: lazy against cold, warm against forget,
lazy against warm, on proc CPU and on planning CPU. Planning CPU = cog_cpu_ns summed over
the objectives plus rebuild_cpu_ns from the identity file (lazy: cog only, its rebuild is
inside cog). Also reported: paired sign per run (the arms share a process, so warm minus
cold in the same run is a paired difference): "warm lower in n of 5 runs".

Measured H* (model A): C_meas = median warm rebuild_cpu_ns over the 5 h40r runs; Delta_meas
= median over the 5 h40r runs of (forget planning CPU - warm planning CPU + warm rebuild
CPU) / 40; H*_meas = C_meas / Delta_meas, compared with the predicted 5.83 and band. Every
sum, difference and ratio in a verdict goes through aien-calc.

Energy: UNMEASURED unless a receipt's wall joules cover a steady run of 30 s or more; the
whole UPS meter covers the box, so even then the figure is whole-box, not this process.
