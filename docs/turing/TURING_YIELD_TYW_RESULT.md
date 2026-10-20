# TY-4/TY-5 wall amendment (TYW) result: check (a) FAIL on both configurations (as predicted); check (b) NOT RUN

Protocol: `TURING_YIELD_ENERGY_PROTOCOL_V0.md`, amendment 2 (`d559841e`, pushed before any timed window). Evidence:
`evidence/TURING_YIELD/tyw-2026-10-19/`. Run 2026-10-20 01:59Z to 02:14Z inside spark-window
`20261020T005426Z-3662ac`, right after TY-7.

**The wall meter on this shared box cannot resolve the interaction term I = E_AB - E_A - E_B + E_0 of two CPU
workloads.** The 95% interval half-width on mean I is 19.7% of E_AB for S6 and 37.0% for S2, against the
pre-registered 2%. I is not distinguishable from zero in either. The cause is other tenants, not the meter step:
the whole 868 s run drew 21,796 J LESS than the 30 s before it would predict (spark-receipt `incremental_J`
-21,795.7), and solo windows were not solo (call counts below).

## Code status (the part that does close)

| check | result |
|---|---|
| build (`make test-turing-energy ty-energy-tools`, inside the window) | OK |
| `test_ty_energy` plain and ASan/UBSan | 54 checks, 0 failures each |
| hostile fixtures plain and ASan/UBSan | nine valid/refusal cases passed each |
| window tool + workload on hardware | 24 of 24 windows ran; every participant ok, pinned, oracle check passed; `meter_ok 0` ("no aien_spbm hwmon") as expected |

## Wall energy per window (J, 30 s windows, UPS rows inside [GO, GO + 30 s])

| config | round | E_0 | E_A | E_B | E_AB | I | slowdown A | slowdown B |
|---|---|---|---|---|---|---|---|---|
| S6 (5+5 X925) | 0 | 9,000.0 | 8,892.9 | 9,000.0 | 8,871.4 | -21.4 | 0.3871 | 0.3450 |
| S6 | 1 | 6,814.3 | 8,721.4 | 8,076.9 | 8,769.2 | -1,214.8 | -1.0319 | -1.3930 |
| S6 | 2 | 8,892.9 | 8,421.4 | 8,850.0 | 8,353.8 | -24.7 | 0.0210 | 0.0793 |
| S2 (1+1) | 0 | 7,671.4 | 7,842.9 | 5,700.0 | 7,992.9 | 2,121.4 | 0.5667 | 0.6674 |
| S2 | 1 | 7,800.0 | 8,314.3 | 7,971.4 | 8,561.5 | 75.8 | 0.0728 | 0.1167 |
| S2 | 2 | 8,057.1 | 8,185.7 | 8,378.6 | 8,400.0 | -107.1 | 0.6372 | 0.0921 |

| config | mean I | sd | 95% t-interval (2 df) | half-width / mean E_AB | I != 0 | check (a) |
|---|---|---|---|---|---|---|
| S6 | -420.3 J | 688.1 J | [-2,129.7, 1,289.1] J | 19.73% of 8,664.8 J | no | **FAIL** |
| S2 | 696.7 J | 1,237.2 J | [-2,377.0, 3,770.4] J | 36.95% of 8,318.1 J | no | **FAIL** |

Check (b) (counter model on held-out coalitions): **NOT RUN**, per amendment 2; it waits for the package meter.
Package energy: **UNAVAILABLE** (no `aien_spbm` hwmon on this boot).

## What the numbers say

- An IDLE window sometimes drew more than a workload window (S6 round 0: E_0 9,000 J > E_A 8,893 J). Ten X925
  cores streaming DRAM should add tens of watts; the box's other tenants (load1 15 to 23 during the run) moved the
  wall draw by more than that from one 30 s window to the next.
- Negative slowdowns (S6 round 1: A completed about twice as many calls in AB as in its own solo window) mean the
  solo windows were themselves slowed by other programs on the same cores. The pinned cores are not reserved; other
  agents' threads were scheduled on them. The contention measured here is mostly contention with strangers.
- So this run says nothing about whether I is zero, positive or negative for these workloads. It does show the
  harness runs end to end on the hardware and the wall meter alone is not enough on a shared box.

## What a passing rerun needs

The package meter loaded (operator step with sudo: handoff 2026-10-05-spbm-energy-operator-steps.md), the measured
cores reserved for the window (no other tenant threads on cpus 5-9 and 15-19), and the protocol's 10 rounds.

## Provenance

| item | value |
|---|---|
| tools | `tools/ty_energy_wall_run.sh`, `tools/ty_energy_wall.c` (branch `research/turing-energy-rerun`) |
| `build/ty/ty_energy_window` | SHA-256 `e1ad10558a9e87fc9aac4981fc9e8f9b11baf4b11aa0afcd4dd468ef4d600ac8` |
| `build/ty/ty_workload` | SHA-256 `d0a78a5602cd71f47f4e714e8478ee680a17d5a7de9ee7243d647661a2b17166` |
| `ty_energy_wall` | SHA-256 `fac2bb16cc148c921257062c53ad1e9fac68eb4624ebf689e389e4f599ec1fe4` |
| raw | `raw/*.jsonl` (24 windows, `raw/SHA256SUMS`), `offsets.txt`, `manifest.txt`, `run.log`, `sensors-slice.csv`, `receipts/2026-10-19-tyw.json`, `build-and-fixture-tests.log` |
| reducer output | `reducer.txt` (reproduces from the evidence folder: `ty_energy_wall evidence/TURING_YIELD/tyw-2026-10-19 evidence/TURING_YIELD/tyw-2026-10-19/sensors-slice.csv`) |
