# Turing Yield TY-7 result: V0 FAIL (as predicted), conditional gain PASS

**Under measurement profile V0, the 27B (qwen3.8-27b-fp8 on our omega-qwen38 engine) did NOT reduce total
description length relative to baseline B on its own held-out samples once it paid for itself: T_V0 =
-243,220,226,670.923 bits over 4,162 symbols. Its 30.4 GB of decode files cost 243,220,274,608 bits; three rounds of
decoding repaid 47,937.077 of them.**

**Conditional on the model already existing (its description not charged; NOT a V0 quantity), the model coded its
own text in 1.501 bits per symbol against 13.019 for B: a gain G of 11.518 bits per symbol, repeatable across three
rounds (11.38, 11.69, 11.51), at 38.98 wall joules per token gross, so G / E_gross = 0.297 bits per wall joule
(a lower bound; the idle-subtracted figure is UNRESOLVED under the pre-registered rule, see Energy).**

Protocol: `TURING_YIELD_TY7_PROTOCOL.md` (pre-registered at `e44504c9`, amendment 1 at `2afe0b3e`, both pushed
before the first timed window). Evidence: `evidence/TURING_YIELD/ty7-2026-10-19/`.

## Pass rules (pre-registered, section 9)

| check | value | required | result |
|---|---|---|---|
| V0 1: pooled T_V0 minus eps | -243,220,226,670.936 bits | > margin 3 sqrt(3) sd_G = 6,550.647 bits | fail |
| V0 2: each round T_V0 with full L(M), minus eps | about -2.4322e11 bits each | > 0 | fail |
| **TY-7 V0** | | | **FAIL** (predicted FAIL) |
| G 1: each round G minus eps | 17,352.400 / 14,874.394 / 15,710.282 bits | > 3 sd_G = 3,782.019 bits | pass |
| G 2: G per symbol within 10% of the mean (11.527) | 11.379 / 11.694 / 11.509 | within 10% | pass |
| G 3: G / E_gross within 20% of the mean (0.2972) | 0.3052 / 0.3027 / 0.2836 bit/J | within 20% | pass |
| readable (request time >= 30 s per round) | 204.1 / 165.8 / 184.8 s | >= 30 s | pass |
| **G-rule (not V0)** | | | **PASS** |

eps (error bound) = 3e-6 bits x symbols = 0.0125 bits pooled.

## Code lengths (bits)

| round | symbols | tokens | L(D\|uniform) | L(D\|B) | L(D\|B_warm) | L(D\|M) | G = L(D\|B) - L(D\|M) |
|---|---|---|---|---|---|---|---|
| 1 | 1,525 | 1,518 | 27,330.816 | 19,629.726 | 15,490.194 | 2,277.326 | 17,352.400 |
| 2 | 1,272 | 1,264 | 22,796.589 | 16,838.210 | 13,000.335 | 1,963.815 | 14,874.394 |
| 3 | 1,365 | 1,358 | 24,463.321 | 17,715.701 | 13,722.803 | 2,005.419 | 15,710.282 |
| pooled | 4,162 | 4,140 | 74,590.726 | 54,183.637 | 42,213.332 | 6,246.560 | 47,937.077 |

Per symbol, pooled: uniform 17.922, B 13.019, B_warm 10.143, M 1.501 bits. L(B) = 0 table bits (prequential, starts
empty each round). L(M) = 243,220,274,608 bits (calc:6f0cbc58); the whole model directory would be 247,120,452,368.
Symbols = tokens plus one end symbol per reply that ended on the model's stop draw (22 of 24 replies; the other two
hit the 256-token cap). Every one of the 4,162 draws was taken from the full softmax: `kept` = 248,320 on every draw,
`top_k_applied` false, `top_p` 1.0, `greedy_tokens` 0.

Break-even: N* = L(M) / (G per symbol) = 2.1117e10 symbols, about 89.7 years of nonstop decoding at the measured
7.46 tok/s (calc:36e3c0f8e533dabe3bfb16989262193e35344a524fd6177361386158c85a6437). Prediction was 2.6e10 symbols,
127 years.

## Energy

| round | request s | tok/s | E_gross J | J/token gross | mean GPU W (context) | G/E_gross bit/J | T_V0/E_gross bit/J |
|---|---|---|---|---|---|---|---|
| 1 | 204.101 | 7.438 | 56,850.5 | 37.45 | 41.94 | 0.30523 | -4.2782e6 |
| 2 | 165.798 | 7.624 | 49,145.5 | 38.88 | 42.51 | 0.30266 | -4.9490e6 |
| 3 | 184.834 | 7.347 | 55,387.9 | 40.79 | 41.83 | 0.28364 | -4.3912e6 |
| pooled | 554.733 | 7.463 | 161,384.0 | 38.98 | | 0.29704 | -1.5071e6 |

- **E_gross** = wall power (UPS output, whole box and anything else on the UPS) over our 24 request intervals. Our
  generation was the only one in the engine during each interval, and the server held the hive GPU lock, so no
  lock-respecting chip job ran inside them. CPU tenants did: load1 15 to 20 during the rounds (other agents'
  builds and runs). So 0.297 bit/J is a **pressure-depressed lower bound**, not the machine's capacity.
- **E_incr: UNRESOLVED** by the pre-registered rule. P_base needed at least 5 sensor rows in the IDLE windows at
  which the engine was idle; there were 0. aien-translate (another user's paper translation) kept the server
  generating through every IDLE window: 1,413 of 1,418 health polls over the whole session reported busy (the 5 idle polls never bracketed a sensor row on both sides).
- **E_pkg: UNAVAILABLE.** The `aien_spbm` package meter is not loaded on this boot (needs sudo).
- spark-receipt window receipts (copies in `receipts/`): session 1,443 s, 407,790 J, mean 282.6 W; w1 incremental
  29,828 J (its window includes a 447 s wait for the GPU lock while another job ran, so it is not our work alone);
  w2 incremental 1,366 J; w3 incremental 95 J. Their "idle before" (30 s) contains aien-translate's decoding, so
  these increments are ours minus another generation's and are context, not E_incr.
- **Post hoc, not counted:** inside the session slice, rows with GPU util <= 5% (27 rows) average 231.1 W and rows
  with util >= 80% (645 rows) average 284.5 W. Taking the 53.4 W difference as the decode increment gives about
  7.2 J per token and G / E about 1.62 bit/J (calc:0ecbaa40b5ae0bcc6782d03dd87475daad1fa8ed3a0482a5d0370bf9e0e2af08).
  This selection was chosen after seeing the data and uses nvidia-smi utilisation as a selector; it is a lead for
  the next protocol, never a result.

## Predictions versus measurement

| quantity | predicted | measured |
|---|---|---|
| L(D\|M) per token | 2.0 bits (1.0 to 3.5) | 1.501 per symbol |
| L(D\|B) per symbol | 11.5 bits (9 to 14) | 13.019 |
| G per symbol | 9.5 bits | 11.518 |
| T_V0 | about -2.43e11 bits: FAIL | -2.4322e11: FAIL |
| N* | about 2.6e10 symbols, 127 years | 2.11e10, 89.7 years |
| decode rate | 6 tok/s (4 to 8) | 7.46 tok/s (request time includes prefill) |
| wall W during requests | 225 W | 290.9 W (E_gross / request s); the box was busier than at prediction time |
| J per token gross | 35 | 38.98 |
| J per token incremental | 9 | UNRESOLVED (post hoc about 7.2) |
| G / E_gross | about 0.27 bit/J | 0.297 bit/J |
| G / E_incr | about 1 bit/J | UNRESOLVED (post hoc about 1.6) |

## What this means, and what it does not

- **T here is not learning.** The model's weights do not change while it decodes. T measures how much shorter the
  model makes the description of text, against a baseline that knows nothing in advance, after paying for its own
  description. No in-context or prefix-cache effect was defined or measured.
- **The text is the model's own samples, not a held-out human corpus.** The server cannot score a given text (no
  log-probability of a supplied text in its API), so D is what the model sampled at temperature 1 with no cut-off.
  On its own samples the model is the true source, which is its best case: on human text its code length would be
  higher by the divergence between that text and the model. The given-text measurement needs a teacher-forced
  scoring path in the engine.
- **The V0 failure is structural.** A 243-gigabit model cannot repay itself with a few thousand tokens. At the
  measured gain it needs about 2.1e10 tokens of text like this before T_V0 turns positive. Profile V0 charges a
  model in full against every data set it is scored on; a model that is used for years would be charged once
  against all of its work. A per-use amortised profile is a separate, future registration.
- B is weak by design (it starts empty each round and must learn a 248,321-symbol alphabet from about 1,400
  symbols). B_warm, primed with the other two rounds and not charged, gets to 10.14 bits per symbol; the model is
  still 8.6 bits per symbol ahead of it.

## Box state

| round | start load1 / MemAvailable / GPU lock holders / GPU util | end |
|---|---|---|
| 1 | 17.01 / 72,064,956 kB / 1 / 95% | 17.46 / 71,002,060 kB / 1 / 94% |
| 2 | 15.32 / 70,989,404 kB / 1 / 95% | 16.49 / 70,296,084 kB / 1 / 94% |
| 3 | 19.61 / 70,957,576 kB / 1 / 96% | 20.29 / 70,277,776 kB / 1 / 96% |

NVRM error delta over the session: 0 (receipt). The session ran in spark-window `20261020T005426Z-3662ac` (quiet
lock held, builds held off), 2026-10-20 01:35Z to 01:59Z.

## Provenance

| item | value |
|---|---|
| protocol | `e44504c9` (pre-registration), `2afe0b3e` (amendment 1), branch `research/turing-energy-rerun` |
| tools | `tools/ty7_drive.sh`, `tools/ty7_round.sh`, `tools/ty7_reduce.c` at `117cde4d` |
| reducer binary | gcc `-std=gnu11 -O2`, SHA-256 `e5b81a43ea19e974cf412e4603347b77dd332ba207ce3e75b6fdd079122a5eea`; `--selftest` Kraft sum = 1 within 1e-9 (an exclusion mutant gives 0.847 and fails) |
| server binary | `~/.local/state/aien-q38/bin-fast-46134247/aien-serve-http`, SHA-256 `df9360112fdc815539f3e289fccfd8070845129ef4ccb466bb90c729ec71d0e5`, aien-sovereign-core `46134247` |
| model | `~/models/qwen3.8-27b-fp8-017b9c7a`, vocab 248,320 |
| prompts | `prompts.txt`, SHA-256 `0978a272f25d851a8e5a461e834af4a7cc0c4dceac359bb489791b49e964449c` |
| reducer output | `reducer.txt`, SHA-256 `b30e28cf0a7fb488270b509550f568d06f652548bbb031c07f13d38c24841172` |
| raw | `run/` (24 replies, intervals, health polls, session log, per-symbol B costs `symbols.tsv`), `sensors-slice.csv`, `receipts/` |

Re-check: `gcc -std=gnu11 -O2 -o ty7_reduce tools/ty7_reduce.c -lm && ./ty7_reduce
evidence/TURING_YIELD/ty7-2026-10-19/run evidence/TURING_YIELD/ty7-2026-10-19/sensors-slice.csv`
