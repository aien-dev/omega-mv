# L492 Omega600 receipt

HOST_QUALIFIED. Frozen delayed-payoff hypothesis FAIL. Hardware/model NOT_RUN;
joules UNMEASURED; independent parent reproduction/review PENDING. No promotion,
live changes, extra agents, runtime edits, model copies or hardware timing runs.

## Result

C preserves the immutable reference while retaining inferior intermediate states.
It crosses the development valley that greedy A misses, reaches the exact finite
optimum in25/25 held-out cases, and repays synthetic work by horizon32 in20/25.
It loses129 work units on each of5 no-payoff cases. C beats A in20/25 and random D
in15/25. The predeclared rule requires positive net AND beating A/D on EVERY case;
therefore FAIL. No threshold or policy was changed after held-out execution.

| Arm | Positive net at H32 | Total net, synthetic units | Per-case net min/max | Sum finite regret |
|---|---:|---:|---:|---:|
| A immediate greedy |5/25|5880|-17 /1244|1095|
| B canonical bounded |20/25|29680|-153 /2087|260|
| C inferior-retaining |20/25|38510|-129 /2751|0|
| D random |19/25|27784|-121 /2142|347|

C has0 positive cases at H1 and20/25 at H8. Its total net at H1/H8/H32 is
-1635 /7430 /38510. The expensive family costs all arms work with no improvement.
B's breadth-first exploration exhausts its160-unit ceiling in some families;
exhaustion is unresolved potential, not proof of futility. Raw stop/regret/pruned
and abandonment fields remain in every row. The per-family table and all other
arms/horizons appear in campaign/run-002/arithmetic.txt and heldout/rows.csv.

These are finite synthetic configuration landscapes. The same author designed
the policy and held-out family generator, and5 seeds/family do not support a
population confidence interval. Min/max are descriptive uncertainty only. C/B/D
also differ in ordering/capacity; the greedy-C mutant specifically isolates the
retention condition on the development valley. No real learning/capability,
real-model quality, general optimum or physical speedup is established.

## Evidence and counts

* campaign/run-001/checks.log:2337 checks,0 failures, interpreted/native state
  identity; emitted AArch64 policy14052 bytes. All3 development families x3 seeds
  x4 arms, finite independent enumeration, budget/refusal/capacity boundaries,
  incumbent preservation and every completed checkpoint cut.
* Six mutants, all runtime assertion failures with exit1: greedy pruning7,
  invalid evidence2, free failed work65, ignored budget34, lost restart seed19,
  skipped context220 failed assertions. No compiler refusal/timeout credited.
* campaign/run-001/process-checks.txt and individual logs/exits: separate-process
  completed checkpoint recovery exactly matches uncut Cortex journal bytes;
  repeated completion unchanged; forced exits99 after reservation/evaluation;
  recover keeps9 spent, retry spends18; stale source refuses with exit2 and no
  append. All24 context words independently tampered/refused in the driver.
* Correctly rehashed state lies refused by transition replay; real digest flips
  and torn Cortex journals refused. Pending attempts stay paid and count toward
  the14-attempt ceiling. Journal capacity24392 bytes includes both checkpoints,
  setup and file header. C's actual peak is24392 bytes, frontier4.
* sanitizer.log:2337/0 with AddressSanitizer and UndefinedBehaviorSanitizer,
  exit0; build script, log and binary hash alongside. No memory-safety finding.
* campaign/run-002: exactly100 arm/case histories,300 horizon rows,940 evaluated
  trials/trace rows,375 exhaustive node rows.4660 checks,0 failures. Independent
  report.awk rebuilds prefix reachability and validates300/300 arithmetic rows,
  checking optimum/regret, full work sums, lifecycle/net/break-even, exposure,
  frontier ceilings and actual journal-byte conservation.
* campaign/final-verdict.json: existing m19r_campaign.sh aggregate PASS,2/2 host
  qualification records verified. This is not M19R/GPU qualification or scientific
  hypothesis PASS. Each run has exact command/head, source/binary hashes, exits,
  raw output, original checkpoints and hashes.sha256.
* calculations/:5 aien-calc receipts exported and independently recomputed MATCH.
  inputs.tsv names every formula, input values and raw CSV/contract source.
  toolchain.txt records actual tool binaries/hashes and versions.

Actual shared-host diagnostics include compilation of OSC, both execution modes,
fixture/oracle checks and journal IO/replay. Check driver wall/CPU seconds:
0.991660251 /0.098334912; peak RSS9984 KiB. Held-out driver:
7.664090266 /0.550013744; peak RSS9980 KiB;100 reloads and1880 replayed transitions.
These aggregate process numbers are not per-arm performance measurements. All
modeled per-arm lifecycle costs are synthetic; per-arm seconds/energy NOT_RUN.
The checkpoint2 coefficient is an uncalibrated constant allowance for2 appends
and a recovery lookup/recheck; raw replay work is also disclosed above. Physical
repayment cannot be inferred from this coefficient or old583 DRAM measurements.

## Reproduction and provenance

Executable source/held-out commit48283b7830bd376dbdf387a3e7ae01474f54c2b7,
base a8ffd2b912c84197d08f963b05f9c7d8b30a3f8a. Freeze fe2cdcd2 was pushed before
implementation. Meaningful development RED preceded GREEN: greedy retention
failed all3 valley seeds; the same fault is reproduced by the final greedy mutant
with its source/binary hashes. Earlier red*.log/green.log/preflight*.log are
historical development diagnostics, not the final gate counts or held-out data.
Their earlier host-driver binary was not retained; do not present them as an
independently reproducible final packet. Final mutants and gate packets are pinned.

The original stale-identity fixture added a blank final line, which the formatting
check flagged. Raw first packet is preserved in development-6aca0850-packet.tar.gz;
the runner now appends a comment to change identity. Checks were rerun before
held-out. No original packet measurement was relabeled as the new run.

From repository root, choose a NEW directory:

```
bash research/exploration-600/run.sh checks /absolute/new/packet
bash research/exploration-600/run.sh heldout /absolute/new/packet
bash research/exploration-600/run.sh aggregate /absolute/new/packet
awk -f tests/research/ex600/report.awk \
  /absolute/new/packet/run-002/heldout/landscapes.csv \
  /absolute/new/packet/run-002/heldout/rows.csv
```

The recorded held-out packet was run once; reproduction is a new experiment and
must not overwrite it. Every build/test uses quiet-build and a60-second foreground
timeout. A repeated phase on an existing run directory refuses with exit2.

Ownership/reuse in research/exploration-600/FREEZE.md preserves L048584 b557e9e7,
L167584-r2 ce47b8b8, L0475833bd56731 and L05924979c436c2. Their earlier negative
results and runtime ownership remain intact. No shared Makefile change.

## Remaining boundaries

Parent L487/desk L488 independently review and reproduce; no worker was spawned
for that. Cortex synchronous-append crash recovery is tested. Deliberate rollback
of an entire valid prefix requires the existing J-Space/external anchor and is
outside this no-promotion experiment. No new store/anchor is added.

HARDWARE-PACKET.md requests parent-routed disposition from existing shared-train
owner L386 under the L480 mapping. No new hardware/model load or window. Real
workload, quality gates and calibrated search/check/storage costs are prerequisites
for any future physical claim. No issue/PR write, closure, merge or judge trigger.
