# Round 1 interpretation: both precommitted gates FAIL

Measured 2026-10-19 14:30:43 CDT under the GB10 lock and the quiet lock (`measured/LOCK.txt`),
load1 11.7 at start, MemAvailable 10.1 GB at start (below the 12 GiB headroom rule; the probe
took 2 GB for about 20 s; this is a rule breach and is recorded here, not hidden), GPU utilisation
91% from another agent's chip job during the window (`~/yardsticks/sensors/2026-10-19.csv`,
14:30:36 to 14:31:20), timer overhead 23 ns per read.

Result (`heldout_results.md`): the evaluator predicted 35.8 / 53.7 / 65.4 / 65.4 GB/s for
k = 2 / 3 / 5 / 6 and the probe measured 85.7 / 107.0 / 123.9 / 113.5 GB/s. Every held-out row
is 1.7x to 2.4x faster than predicted; no interval covers; both controls fail the same way
(lookup -42 to -79%, spec roofline -5 to -58%, and +111% on k=16 where it is meaningless).

## What was falsified

Not the DAG mechanics: the per-op floors, the processor-sharing pipe, bounds and attribution
all passed 171 analytical checks and the fixture arithmetic is exact. What failed is the
**transfer of the M1 block4 calibration (09:16 CDT, load 15 to 29, other agents' GPU jobs
streaming the same unified DRAM) to the box state at 14:30 CDT (load 11, one GPU job).**
Both probes pre-touch pages and copy per-thread slices (`bench/m1/block4_membw.c` on branch
m1/hardware-passport vs `tools/dagsim/memcpy_probe.c`; mmap vs malloc is the only difference),
so apparatus is not the explanation. The fitted per-thread rate (17.9 GB/s) and pipe capacity
(65.4 GB/s) were pressure-depressed numbers, and the schema's validity envelope carried only
`load1`, which does not see another tenant's DRAM traffic.

Consequences for the schema and the evaluator (recorded, not yet implemented):

1. The calibration envelope needs a DRAM-pressure indicator next to `load1` (GPU utilisation or
   GB10 lock holders from the sensor log at the time of the receipt). `rx_costmodel` already
   has the concept (pressure buckets); OEV1 calibration records do not, so a v2 field is owed.
2. A fitted bandwidth figure from a loaded window is a lower bound on the quiet figure, not a
   capacity. Until pressure is in the envelope, the evaluator should label such caps
   `measured_under_pressure` and refuse to rank candidates whose order depends on them.
3. The precommitted interval (receipt repeat spread) understates day-to-day variance by an
   order of magnitude. `sd_scale`-style widening on a separate calibration set is required
   before G-cov means anything (open item 583-a in the handoff).

## Round 2 (same box state, same apparatus)

Fit set measured fresh with the same probe at k=1 and k=8 (`round2_fit/`), new fixtures and
predictions committed before measuring k=2,3,5,6 again (`round2_heldout/`). Gates unchanged.
If round 2 passes, the structural model is retained for same-state use and the finding above is
the cost of the day; if it also fails, the per-thread-plus-shared-pipe model itself is wrong on
this box and goes in the dossier as falsified.
