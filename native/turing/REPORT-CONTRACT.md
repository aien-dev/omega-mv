<!-- SPDX-License-Identifier: Apache-2.0 -->
# ORIGSEL evaluator contract (part of commit E, Turing-profile-ORIGSEL-v1.1)

Two implementations compute the same report from the same raw inputs. The first is the seeded
.om evaluator (`origsel.om`). The second is the independent recomputer (`tools/turing_origsel_indep/`,
C, an external referee). The reports must be byte-identical, `h1` lines excepted (those come only
from the .om H mode). Everything below is fixed in E. The profile `Turing-profile-ORIGSEL-v1.1.toml`
is binding wherever this file is silent.

## Inputs

| name | content |
|---|---|
| REC | the collected omexp-v1 file (raw, 256-byte records) |
| ALLOW | allow-list text (`<fnv decimal> <STATUS> <label>` per line) |
| V1 | the two v1 files concatenated (raw records; only words 5 and 6 are used) |
| IMG_M1, IMG_SH, IMG_UN | SI19SLCK images (kind 2, 11560 bytes): the V2=1 mlp, shuffled and untrained images |
| units | `select.om`, `omexp.om` (aien-train 692d8510) and `origsel_kappa.om` (for the P2 byte counts) |

## Admission and partitions

- The allow-list status of FNV-1a 64 of REC must be AUTHORIZED or SYNTHETIC. Otherwise the report is the single line `refused allow`.
- Records are admitted by ORIG1-CONTRACT-v2 section 2 (oxadm2). A whole-file refusal gives `refused admission <code>`.
- The split is section 3 (oxhspl).
- Cell of a record: `16 fl(M) + fl(N)`, where fl is floor(log2).
- lg16 is as in ORIG1-CONTRACT section 3.

Word indices are 0-based 64-bit LE words of a record. ps of arm a is word 16+a.

## Arms, in report order

`U O0 FR S R M1 M2 A CSH CUN` (arm codes 1..10). An order is a permutation of arms 0..4.

- U, O0, A: identity (0,1,2,3,4).
- FR: rule arm r first (r = 2 if N <= 8 else 3), then the other arms ascending.
- S: separately for core class 0 and 1, total_a = the sum of ps_a over admitted TRAIN records (both pressures) of that class (u64). Arms are ordered by total ascending, ties to the lower index.
- R: the nearest record q is chosen among admitted TRAIN records with the same core class and the same pressure. It minimizes |lg16 M_q - lg16 M_r| + |lg16 N_q - lg16 N_r|, with the lowest record index winning a tie (strict less-than while scanning ascending q). With no such q the order is identity. Otherwise arms are ordered by ps of q ascending, ties to the lower index.
- M1, M2, CSH, CUN use the MLP order of their image (M2 uses M1's image):
  - score_a = MLP(x, a), binary32, round-to-nearest-even, gradual underflow, no fused multiply-add.
  - Order the arms ascending by key, ties to the lower index. key(b) = -(b & 0x7fffffff) when the sign bit is set, else b, so -0 = +0.
- MLP(x, a):
  - Inputs: z_0..z_11 = f_j / 16 (exact; the 12 features of ORIG1-CONTRACT section 3), z_12..z_19 = one-hot arm a (1.0f at 12+a).
  - Parameters: P[k] = low 32 bits of the LE u64 at image offset 5912 + 8k (the best-on-VALIDATION block SB), k = 0..704.
  - Hidden layer, h = 0..31: acc = P[640+h]; for i = 0..19, acc = acc + P[32i+h] * z_i. a_h = acc if acc > 0 (strictly positive) else 0.
  - Output: out = P[704]; for h = 0..31, out = out + P[672+h] * a_h.
- Context c: kappa = 2 core + (N <= 8) for M2 and A; 0 for every other arm.

## Rows (F1)

Fit set: admitted VALIDATION records with pressure 0 and core 0 or 1.

- n_r per (arm, context) counts the fit records whose oracle arm (oxbest: cheapest ps, lowest index on a tie) has rank r in the arm's order.
- Q_r = 1 + floor(65531 (2 n_r + 1) / (2N + 5)). The remainder goes to the largest n_r, lowest r on a tie.
- U never fits: its row is (13108, 13107, 13107, 13107, 13107).
- Probability of arm a at decision t = Q[c][rank of a] / 65536.

## Model code lengths (bits)

Building blocks:
- gamma(v) = 2 floor(log2 v) + 1.
- row(N) = gamma(N+1) + ceil(log2 C(N+4, 4)), with ceil(log2 1) = 0.
- TG1(FR) = 20. TG1(kappa) = 21.
- ntrain = admitted TRAIN records, both pressures and both cores.

P1 by arm:

| arm | P1 |
|---|---|
| U | 0 |
| O0 | row |
| FR | 20 + row |
| S | 14 + row |
| R | 12 + 26 ntrain + row |
| M1, CSH, CUN | 32 + 22560 + row |
| M2 | 32 + 22560 + 21 + its four rows |
| A | 21 + its four rows |

P2 = P1 + 8 x the canonical bytes of:

| arm | units counted |
|---|---|
| FR | function `oxrule` of omexp.om |
| S | `oxstatic` |
| R | `oxnearok`, `oxl1`, `oxnear` |
| M1, CSH, CUN | the whole `select.om` |
| M2 | `select.om` + `origsel_kappa.om` |
| A | `origsel_kappa.om` |
| U, O0 | nothing (0) |

- A function's text runs from its line `fn NAME ` up to, not including, the next line beginning `fn ` or `global `, or the end of file.
- Canonical bytes: drop every line that is empty or begins with `note`; count the remaining bytes including their newline.

## D, regimes, ub

- D: admitted HELD-OUT records with pressure 0 and core class 0 or 1, minus any record whose (M, N) occurs in V1. Kept in file order. The number removed is `burned`.
- D_v1fam: the records of D whose cell holds any V1 record. D_fresh: the rest of D.
- P1R: the same as D, but with pressure 1.
- ub(q): CODER_SPEC section 8. The 128-bit product is truncated, as specified. Golden values: ub(1) = 16000000, ub(32768) = 1000000, ub(43481) = 591903, ub(46819) = 485194, ub(13107) = 2321950, ub(13108) = 2321840.
- ub_X(t) = ub(Q_X[c(t)][rank_X(x_t)]), where x_t is the oracle arm of t.

## Bootstrap

- Blocks: the distinct cells of D, ascending (both cores pooled), numbered 0..F-1. blocksum_X(j) = the sum of ub_X(t) over t in block j.
- splitmix64, wrapping u64, state s = 20261025. Each output: s = s + 0x9E3779B97F4A7C15; z = s; z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9; z = (z ^ (z >> 27)) * 0x94D049BB133111EB; out = z ^ (z >> 31).
- Replicate i = 0..9999, k = 0..F-1: pick = out mod F, taking outputs in order. S_X(i) = the sum of blocksum_X over the picks.
- Statistics, in micro-bits (signed 64-bit):
  - T(M;Y)(i) = S_Y(i) - S_M(i) - 10^6 (P1(M) - P1(Y)).
  - dl(M;C)(i) = S_C(i) - S_M(i).
  - delta(i) = S_M1(i) - S_M2(i) - 10^6 (P1(M2) - P1(M1)).
- Each statistic's 10000 values are sorted ascending: lb = element 166 (0-based), ub = element 9833.
- total = the same statistic over D with every block once.
- Second bootstrap (sensitivity): blocks are the distinct (cell, core) of D, ascending by 2 cell + core. The RNG restarts at 20261025. Only T(M;FR) is reported, as `tcf` lines.

## Verdicts

- n_min(M) = ceil(P1(M) 10^6 / 2321950).
- Per candidate M in M1, M2, A, first match wins:
  1. INCONCLUSIVE(UNDERPOWERED) if F < 20 or n < 100.
  2. QUALIFIED_IMPROVEMENT if total(M;FR) > 0 and lb(M;Y) > 0 for Y in FR, U, O0, S. For M1 and M2 it also needs lb of dl(M1;CSH) > 0 and lb of dl(M1;CUN) > 0.
  3. NO_IMPROVEMENT(COST_BOUND) if ub(M;FR) <= 0 and n < n_min(M).
  4. NO_IMPROVEMENT if ub(M;FR) <= 0.
  5. INCONCLUSIVE otherwise.
- Experiment: QUALIFIED_IMPROVEMENT(UNISOLATED) if any candidate qualifies. NO_IMPROVEMENT if every candidate's word begins with NO_IMPROVEMENT. INCONCLUSIVE otherwise.

## Report (text, `\n` line ends, decimal integers, '-' for negatives)

```
ORIGSEL v1.1
admit records=<all records> admitted=<admitted> train=<n> validation=<n> heldout=<n>
d n=<n> burned=<n> F=<F> fresh=<n> v1fam=<n> p1r=<n>
arm <NAME> p1=<bits> p2=<bits> dl=<sum ub_X over D> fresh=<sum over D_fresh> v1fam=<sum over D_v1fam> p1r=<sum over P1R> rows=<n0>,<n1>,<n2>,<n3>,<n4>[;<next context>...]
   (10 lines, arm report order; U prints rows=- ; p1r uses the same rows)
t <M> <Y> total=<micro> lb=<micro> ub=<micro>
   (M in M1 M2 A R CSH CUN, Y in FR U O0 S, M-major)
dl M1 CSH total=.. lb=.. ub=..
dl M1 CUN total=.. lb=.. ub=..
delta M2 M1 total=.. lb=.. ub=..
tcf <M> FR total=.. lb=.. ub=..        (M in M1 M2 A)
nmin <M> <n_min>                        (M in M1 M2 A)
verdict <M> <WORD>                      (M in M1 M2 A)
experiment <WORD>
```

Arm orders and rows for D also feed the TPS1/TSY1 writer of the .om evaluator (profile `tps1_mapping`):
- TPS1: K = 5, qbits = 16. Records carry observation_index t, context_key = word 14, crumb = block index, norm_sum = 65536, q[a] = Q[c][rank of a].
- TSY1: the oracle arms of D.
