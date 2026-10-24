# PRECOMMIT round 3: is the low-k shortfall a core-class effect?

Committed before measuring. Round 2 found two unpinned threads reach 1.47x of one thread
(55.7 vs 37.9 GB/s) while the pipe (95 GB/s) was far from binding. The GB10 has two core
classes: Cortex-X925 at 3.9 GHz on cpus 5-9 and 15-19, Cortex-A725 at 2.8 GHz on cpus 0-4 and
10-14 (`/sys/devices/system/cpu/cpu*/cpufreq/cpuinfo_max_freq`, midr parts 0xd85 and 0xd87).

Experiment: `PIN=5-9,15-19 KS="1 2 3" measure.sh ... round3_x925` and
`PIN=0-4,10-14 KS="1 2 3" measure.sh ... round3_a725`, 10 reps each, both locks, headroom rule.

Precommitted reading:
- If X925-pinned k=2 is >= 1.8x X925-pinned k=1 (and k=3 >= 2.6x), the round-2 shortfall is
  thread placement across classes; the fix is a per-class `compute` resource with its own
  per-thread rate in the fixture (OEV1 can express it today). Verdict: RETAINED with a
  placement caveat.
- If X925-pinned k=2 is still < 1.6x of k=1, the shortfall is inside one class (cluster-level
  memory path or interconnect), and the per-op busy-time model needs a sublinear term:
  Verdict: FALSIFIED for k < 5 on this box, schema v2 item.
- A725 rows are the control: the per-thread rate should be lower than X925's; if A725 k=1 is
  about 1/1.47 of X925 k=1, the unpinned k=2 figure is consistent with one thread per class.
