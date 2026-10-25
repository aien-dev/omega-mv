# REUSE-1 FINAL result for the candidate (#742, #743, #744)

The frozen candidate is omega-mv `6ad09bff0c544bf73ceb3c63b349ba0cd6998e52`, with source `2b25e1bf`.
Commits after it change only `docs/` and `evidence/`.

The evaluation ran once on the FINAL split. The independent evaluator is spark-rsi-mv `4345eda9`.
Its raw evidence, reproduction commands and receipt are in spark-rsi-mv `84900162`, under
`evidence/REUSE-73/final/`.

**Decision: STOP.** Contract section 10 stops when the lower bound of rho(delta) is at least 1.
Here rho(delta) = 1.164, with a 95 % interval of 1.040 to 1.307.

Correctness passed: there were 0 wrong answers and 0 wrong refusals in 189,504 scored requests.
All 36 negative controls were detected.

## Lifecycle cost

Sums cover all 24 groups and 3 repetitions. Units are ms.

| Arm | Round trips | Start to ready | Acquisition | Total | rho (95 % interval) |
|---|---|---|---|---|---|
| full | 854 | 65 | 0 | 867 | 1 |
| method | 824 | 134 | 222 | 1057 | 1.219 (1.091 to 1.363) |
| delta | 777 | 136 | 222 | 1010 | 1.164 (1.040 to 1.307) |
| idelta (empty library) | 638 | 62 | 0 | 651 | 0.751 (0.650 to 0.867) |
| select | 634 | 131 | 222 | 868 | 1.001 (0.887 to 1.127) |

How the delta cost splits:
- Acquisition (three library builds) costs 222 ms.
- Loading a generation costs about 71 ms more start-to-ready than B. That covers manifest and lineage verification, the memfd ELF and the exec.
- Both arms use the same delta algebra, and every request in both was routed to it. Even so, the generation's round trips sum to 139 ms more than idelta's.
- That excess is concentrated at small n. Request-only rho at n = 64 is 1.585 for delta and 1.094 for idelta.
- [INFERENCE] The likely cause is the larger generation binary (111 kB of `.om` against 103 kB for B) plus per-request method-state maintenance. This was not profiled.

## Per issue

**#742, guarded methods from admitted experience.**
- Transfer coverage was 100 %: methods served all 5,616 transfer QUERYs.
- No QUERY fell back to route 5.
- Every integrity and lineage control was detected: C1, C1b, C1c with G3, C2, C3, C6, C7, C8 (K = 1..9), C9, C10 and C11 restart identity.
- The method arm costs more than full over the lifecycle: rho(method) = 1.219 (1.091 to 1.363). Its round trips alone are 0.965 of full's.
- No payback within the horizon.
- Cortex persistence: BLOCKED. There is no `.om` to C path; it is routed to #651 and the ADR 0022 owner.
- The #699 experience item: NOT MET.
- Run-time discovery and rewrites: NOT_RUN.

**#743, incremental execution with exact invalidation and atomic generations.**
- Incremental execution without a library reduces lifecycle cost: rho(idelta) = 0.751 (0.650 to 0.867).
- Payback for idelta is immediate, at 0 queries.
- With the learned library it does not reduce cost:
  - delta/method = 0.955 (0.854 to 1.079): inconclusive;
  - delta/idelta = 1.551 (1.406 to 1.737): the library makes delta worse.
- Dense regime (type D) is a loss: delta/method 2.43 and idelta/full 2.41. Every other regime is between 0.37 and 1.04.
- Controls C4 (wrong delta unit) and C8 (crash after each of the 9 publish steps) were detected.
- Peak memory high-water mark is 832 kB for delta, against 232 kB for full.

**#744, lifecycle-cost selection.**
- The selector helps relative to delta: select/delta = 0.859 (0.759 to 0.957).
- It is not better than full: rho(select) = 1.001 (0.887 to 1.127).
- It used 23,055 delta routes, 3,939 full routes and 78 method routes.
- Calibration came from CALIB only, and was handed over on fd 7 (K16).

## Verdicts

| Class | Verdict |
|---|---|
| Source | PASS after independent review (two candidate findings, P0 calibration TOCTOU and P1 churn counting, both fixed and verified) |
| Host (Spark CPU) | Correctness and controls PASS; lifecycle cost STOP |
| Simulator | NOT_RUN |
| Physical (GB10) | NOT_RUN |
| Energy | NOT_MEASURED |

Independence is UNISOLATED: both sides ran under one operator account. The receipt is UNSIGNED, because the #67 signer is Rust.
