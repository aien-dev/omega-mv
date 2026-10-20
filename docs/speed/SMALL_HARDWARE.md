# Smart models on small hardware (ledger L273, 2026-10-20)

Question (Drake, 2026-10-20): "really I want to be able to use these really smart models on small hardware ... and by
doing that we need to make this faster for inference."

Scope: three of our models (Qwen3.8-27B dense, Qwen3-30B-A3B mixture-of-experts, an 8B dense) on four small targets
(Raspberry Pi 5, a MacBook Air M2, the PC's RTX 5070 12 GB, a generic 8 GB laptop). Every derived number carries an
aien-calc receipt (`calc:` id, full list in `research/small-hw/calc-receipts.txt`). Claims are tagged OBSERVED (measured
or read from a primary record), INFERRED (arithmetic from observed parts) or UNKNOWN. Rule kept throughout: a speedup
counts only if the words come out identical to the reference; anything that changes them is labelled.

## Summary for Drake

- **The one law.** Writing a word means reading the part of the model that word uses. Speed ceiling = memory speed
  divided by bytes read per word. Small machines have slow memory (a Pi 5 moves 17 GB/s, the Spark 273), so the only
  ways in are: read fewer bytes per word, keep the bytes that are needed close, and guess several words per read.
- **The 30B mixture-of-experts model is the natural small-hardware "smart" model.** Each word uses only 8 of its 128
  experts per layer, so it reads 3.37 GB per word at 8 bits (calc:70c43eb6) against 26.89 GB for the 27B: eight times
  less. At 4 bits that is 1.63 GB per word (calc:d16a15a4).
- **It does not fit in a Pi's memory** (16.7 GB at 4 bits, calc:6139121f), so the experts must come from the disk while
  it runs. That is what the probe in section 5 measures: the 30B with its experts pulled from disk through a
  speed-limited reader, words checked bit for bit against the model running fully in memory.
- **Probe result:** see section 5 (PENDING until the measurement window).
- **Biggest levers, ranked** (section 6): 4-bit weights (half the bytes, not word-identical: about 1 word in 8 differs
  from 8-bit on code), the expert cache with prefetch (identical words), the laptop-drafts / Spark-checks Wormhole
  (identical to the Spark's words, needs the network), 8-bit KV cache (small risk), distillation (different model,
  labelled), CPU paths (our Pi engine runs at about 6 percent of its memory ceiling today, so software is the gap there).

## 1. Terms used once

- **Word / token**: the unit the model writes. **tok/s**: words per second, one person, one word at a time.
- **Active bytes per word**: weight bytes that must be read to write one word. For a dense model, all of it; for the
  30B, the shared parts plus the 8 chosen experts per layer.
- **Ceiling**: memory bandwidth / active bytes per word. Real engines reach a fraction of it (the Spark's 27B engine
  reaches 0.71 of its ceiling at 4 bits, L125, calc:babb1a77).
- **Resident**: the weights sit in the machine's memory. **Streamed**: weights are read from disk when needed.
- **Expert cache (Heartbeat ring slots)**: a fixed set of memory slots that hold recently used experts; a **hit** means
  the expert is already there, a **miss** means a disk read.
- **W4 / W2**: weights stored in about 4 or 2 bits (4.25 / 2.25 bits per weight counting the scales).

## 2. The targets

| Target | Memory | Memory speed | Disk read | Source / tag |
|---|---|---|---|---|
| Raspberry Pi 5 | 8 GB or 16 GB LPDDR4X-4267, 32-bit bus | 17.07 GB/s spec (calc:59c2129b); measured UNKNOWN | SD card 96 MB/s (OBSERVED: our firmware read 807,694,368 B in 8.40 s, aienos `docs/native/PI5-6-INFER-RUNS.md`); NVMe on the PCIe 2.0 x1 port about 0.45-0.5 GB/s, 0.83-0.9 GB/s with the unofficial Gen 3 setting (web reports, raspberry.tips, kiwi-electronics); USB 3 SSD UNKNOWN (not looked up) | spec + one OBSERVED |
| MacBook Air M2 | 8, 16 or 24 GB unified | 100 GB/s (Apple figure, Computerworld review) | 256 GB model about 1.45 GB/s, 512 GB and up about 2.8-2.9 GB/s (How-To Geek / Tom's Guide Blackmagic runs) | published |
| PC: RTX 5070 | 12 GB GDDR7 on the card; host RAM UNKNOWN | 672 GB/s on the card (Guru3D review spec table); PCIe 5.0, lane count UNCONFIRMED (x16 in leaks, x8 in one listing): 63 GB/s at x16, 31.5 at x8 | PC disk 1.82 TB, type UNKNOWN (aienos `docs/PC_AIENOS_MAP`: photo-level facts only) | published + UNKNOWN |
| Generic 8 GB laptop | 8 GB DDR4-3200 dual channel (assumed) | 51.2 GB/s (2 x 64-bit x 3200 MT/s; single channel would be 25.6) | NVMe 2-7 GB/s class (assumed) | ASSUMED, no device |
| DGX Spark (for reference) | 128 GB LPDDR5x unified | 273 GB/s spec, 218.6 measured whole-model stream | NVMe 0.64-1.95 GB/s model-free round trip | L123 audit section 2 |

## 3. The models, bytes per word

Exact counts from the model configs (hidden sizes, layer counts, expert sizes); the 30B whole-model sum (31.17 GB,
calc:7d37adcf) matches its checkpoint files on disk (31,175,618,584 B), which checks the method.

| Model | Format | Whole model | Active bytes per word | Multiplies per word | Source |
|---|---|---|---|---|---|
| 27B dense | BF16 | 51.2 GB | 51.20 GB (calc:7877f940) | about 25.6 G | config arithmetic |
| 27B dense | FP8 (+ bf16 head) | 26.89 GB | 26.89 GB | 25.6 G | L123 audit (`CEILING.md:34`) |
| 27B dense | W4 (stored 4-bit, L125) | 13.65 GB | 13.65 GB | 25.6 G | L125 receipt |
| 27B dense | W2 (2.25 b body, 4.25 b head) | 7.52 GB | 7.52 GB (calc:14904590) | 25.6 G | arithmetic; quality UNKNOWN |
| 30B-A3B MoE | BF16 | about 61 GB | 6.08 GB (calc:5e2ab3b7) | 3.04 G | config arithmetic |
| 30B-A3B MoE | FP8 (as shipped, bf16 head) | 31.17 GB (calc:7d37adcf) | 3.37 GB (calc:70c43eb6), of which experts 1.81 GB (calc:dcadaaa2) | 3.04 G | config arithmetic |
| 30B-A3B MoE | W4 | 16.70 GB (calc:6139121f) | 1.63 GB (calc:d16a15a4): shared 0.67 (calc:8d61da57) + experts 0.96 (calc:a392f781) | 3.04 G | arithmetic |
| 30B-A3B MoE | W2 | 9.22 GB (calc:f2bf26d1) | 0.95 GB (calc:050401c8) | 3.04 G | arithmetic; quality UNKNOWN |
| 8B dense | BF16 | 16.4 GB | 15.14 GB (calc:5057bee8) | 7.57 G | config arithmetic |
| 8B dense | FP8 (+ bf16 head) | 8.8 GB | 8.19 GB (calc:4ad1228e) | 7.57 G | config arithmetic |
| 8B dense | W4 | 4.4 GB | 4.02 GB (calc:74205d0a) | 7.57 G | arithmetic |
| 8B dense | W2 | 2.6 GB | 2.28 GB (calc:1664020a) | 7.57 G | arithmetic; quality UNKNOWN |

"Multiplies per word" is one multiply-add per active weight (attention over the context adds a little and grows with
it). KV cache per earlier word: 30B 98,304 B (calc:ffee0b48), 27B 65,536 B (calc:174870b2, L123). Joules per word on the
small targets: UNKNOWN (no wall-power receipt on any of them); bytes read is the proxy, since moving a byte from
DRAM costs far more energy than one multiply on it. L274 owns the counted work-per-token harness; this document does
not build a second one.

What we know about quality per format (OBSERVED, all against the FP8 27B): W4 (MXFP4) picks the same next word
88 percent of the time on code and passes the frozen L125 fit gate (pooled 0.938); a calibrated 3.15-bit trellis code
is on par with MXFP4 (L134, sub4bit/drift-study@4451fec3). Below 3 bits: no receipt. None of the formats below FP8 is
word-identical to FP8; each is its own reference once chosen.

## 4. Ceilings per target

Ceiling = memory speed / active bytes per word, only where the whole model fits in memory with room for the system
(INFERRED). Measured engines reach a fraction of it (0.71 on the Spark at W4).

| Target | Model and format that fits in memory | Ceiling tok/s | Receipt |
|---|---|---|---|
| Pi 5 8 GB | 8B W4 (4.4 GB) | 4.25 | calc:edc54726 |
| Pi 5 8 GB | 8B W2 | 7.47 (quality UNKNOWN) | calc:abea9c93 |
| Pi 5 16 GB | 30B W2 (9.2 GB) | 17.9 (quality UNKNOWN) | calc:57b87edd |
| Pi 5 16 GB | 27B W2 (7.5 GB) | 2.27 (quality UNKNOWN) | calc:029d27ac |
| Pi 5 (any) | 30B W4 if it were resident | 10.4; does not fit 8 or 16 GB with the system, so it streams (section 5) | calc:8d130156 |
| M2 Air 24 GB | 30B W4 (16.7 GB) | 61.2 | calc:916c95a2 |
| M2 Air 16 GB | 30B W2 | 104.7 (quality UNKNOWN) | calc:f2749b19 |
| M2 Air 16/24 GB | 27B W4 (13.65 GB) | 7.33 (16 GB is tight) | calc:c8d9bb1d |
| M2 Air 8 GB+ | 8B W4 | 24.9 | calc:813bbba0 |
| RTX 5070 12 GB | 8B FP8 | 82.1 | calc:fa8a0aef |
| RTX 5070 12 GB | 8B W4 | 167 | calc:7a98ea66 |
| RTX 5070 12 GB | 27B W2 (7.5 GB) | 89.4 (quality UNKNOWN) | calc:16cdff9c |
| RTX 5070 12 GB | 30B W2 (9.2 GB) | 704 (quality UNKNOWN) | calc:d1c1277d |
| RTX 5070 12 GB | 30B W4 (16.7 GB): does not fit; shared parts + hot experts on the card, the rest in PC RAM over PCIe (63 GB/s at x16) | bounded by PCIe for misses: see section 5 formula | |
| 8 GB laptop | 8B W4 | 12.7 | calc:d2c3a087 |
| 8 GB laptop | 30B any 4-bit: does not fit, streams from NVMe | section 5 | |

What does not fit anywhere small without streaming: the 27B at FP8 or W4 on 8 GB machines, the 30B at FP8 or W4 on
anything under 24 GB. The 30B is the only "really smart" model whose per-word bytes are small enough that streaming
part of it can still be fast: its experts per word are 0.96 GB at W4, but most of them repeat from word to word.

## 5. The probe: the 30B with its experts streamed from disk at a capped speed (STAND-IN, not a Pi number)

PENDING (measurement window).



## 7. Cross-machine Wormhole: the laptop drafts, the Spark checks

Idea (L080/L190 across machines): a small model on the laptop guesses k words; the Spark's big model checks all k in
one pass and keeps the longest correct run plus one word of its own. The words are exactly the Spark's words (the
check is the Spark's own greedy choice), so output is identical to the Spark alone by construction.

**Measured link, Spark to Drake's MacBook Air (2026-10-20, OBSERVED, files in `evidence/SMALL-HW-L273/`):**

| Path | Samples | Round trip |
|---|---|---|
| Raw LAN ICMP ping to 192.168.1.193 | 50 | min 1.94, avg 3.00, max 4.66 ms (an earlier 30-ping run had one 303 ms spike: the Mac is on Wi-Fi) |
| Tailscale address 100.64.0.1, ICMP | 50 | min 2.66, avg 4.00, max 21.7 ms |
| Tailscale ping (UDP over WireGuard, encrypted and authenticated; disco and TSMP modes) | 30 + 29 | median 3 ms, p90 5 ms (path is direct, not relayed) |

UNKNOWN: a round trip on our own signed transport. It exists only inside AienOS (aienos `native/net/aienos_sec.c`,
`aienos_ipj`, QEMU-gated); it does not run on macOS, and there is no remote login to the Mac to start an echo. The
signed WireGuard round trip above is the stand-in. Message size per round is k word ids or k short strings (tens of
bytes), so link speed does not matter; only the round trip does.

**Latency budget per round (INFERRED).** Round time with drafting overlapped (the laptop drafts the next block while
the Spark checks the current one) = max(k x laptop draft time per word, Spark check time) + round trip.

- Network share of a round: 3 ms of a 131 ms live FP8 check is 2.3 percent (calc:e8709247); 5 ms (p90) of the 70 ms
  W4 pass is 7.1 percent (calc:5e61cc1d). On the home LAN the network is not the limit.
- Against the Spark's own on-chip drafter (MTP: 131 + 23 + 2 ms per round, moonshot E in L123): a remote drafter with
  the same acceptance makes rounds 1.16 times shorter (calc:3a38a3a9), because drafting leaves the Spark's memory bus.
- Example at the MTP code acceptance (4.15 words per round, measured on the Spark, used here only as an illustration):
  31.0 tok/s at FP8 (calc:9e823deb), 55.4 at W4 (calc:f23368f6, assumes a 4-bit multi-row check at one-row cost, which
  has no receipt yet, L123 lever G). Over the internet with a 50 ms round trip: 34.6 at W4 (calc:2e0e0836).
- The binding limit is the laptop drafter, not the network: it must draft k words within one Spark check. A 0.6B
  drafter on the M2 is a 250-word/s-class ceiling (100 GB/s over about 0.4 GB, INFERRED, not measured); an 8B W4
  drafter at 24.9 tok/s ceiling (calc:813bbba0) only fits k of 2 to 3 into a 131 ms check.
- UNKNOWN: acceptance of any laptop-sized drafter against the 27B. The 27B uses a 248k vocabulary and the Qwen3 small
  models use 151k, so drafts cross as text through L190's alignment bridge (`align.rs`). L190 and L270 own the pair and
  the cascade measurements; this section is the cross-machine budget only.

What it gives Drake: on the laptop, words identical to the Spark's 27B at the Spark's speed or better, as long as the
laptop can reach the Spark. Offline, the laptop gets only what fits locally (sections 4 and 5). "Yes if": a measured
acceptance of at least 1 + round trip / check time (1.023 at FP8 on the LAN) pays against the Spark alone; beating the
Spark's own MTP drafter needs acceptance within 16 percent of MTP's.

## 6. Levers, ranked for small hardware

Ranked by how much "smart model per small box" each one buys, identical-output levers first. Bytes and multiplies
are per word; joules per word on small targets: UNKNOWN for all (no wall-power receipt), bytes are the proxy.

| # | Lever | Bytes per word | Multiplies per word | Words identical? | Evidence | Status / owner |
|---|---|---|---|---|---|---|
| 1 | **30B MoE with an expert cache streamed from disk** (Heartbeat ring slots) | shared 0.67 GB + misses only (W4); misses measured in section 5 | 3.04 G, unchanged | **yes**, measured bit for bit (section 5) | this probe; L080 ring crate (sc `aien-heartbeat`, `aien-wormhole/cache.rs`) | L190 puts the ring on the chip path; this doc measures the small-box case |
| 2 | **Prefetch the next word's experts** while the current word computes | same bytes, hidden behind compute: word time becomes max(compute, disk) instead of compute + disk | +1 small predictor per layer | yes (the prediction only moves reads earlier) | section 5 gives both terms per target | L271 (unassigned) |
| 3 | **Draft k words locally, check them in one pass of the streamed 30B** | one pass reads each needed expert once for all k words, so disk misses per word fall by about the words accepted per pass (if the k words share experts: UNKNOWN until measured) | k x 3.04 G per pass, same per accepted word | yes | MTP/DFlash acceptance on the 27B (L123 G/H); none for the 30B on a small box | not started; "yes if" a small resident drafter's acceptance on the 30B is measured |
| 4 | **W4 weights** | half of FP8 (27B 26.89 -> 13.65 GB; 30B 3.37 -> 1.63 GB) | unchanged | **no**: same next word as FP8 about 88 percent on code; passes the frozen W4 fit gate (L125, pooled 0.938) | L125, L134 | L125 READY; the W4 model is its own reference once chosen |
| 5 | **Laptop drafts, Spark checks over the network** (section 7) | 0 big-model bytes on the laptop | the Spark's | **yes**, identical to the Spark | link measured 3 ms median | needs a reachable Spark; L190/L270 own the pair and cascade |
| 6 | **Lossless weight coding** (store weights in their entropy, decode exactly) | below the stored bits if the entropy is lower; decoding cost on the small CPU is the price | + decode work | yes | L274 `tools/wpt_entropy.c` (no result yet) | L274 owns it; "yes if" its entropy count shows a gap worth the decode time |
| 7 | **8-bit KV cache** | 30B: 98,304 -> 49,152 B per earlier word (calc:ffee0b48); at 4k context 0.40 -> 0.20 GB per word read | unchanged | no (small drift) | L131 (stopped) | worth it only at long context: at 4k words the KV read is 0.40 GB against 1.63 GB of W4 weights |
| 8 | **W2 / 3-bit weights** | 30B W2 0.95 GB (calc:050401c8); 27B W2 7.52 GB (calc:14904590) | unchanged | no; 3.15-bit calibrated trellis is on par with MXFP4 on six texts (L134); below 3 bits no receipt | L134 | "yes if" a 2-bit code passes the same frozen fit gate as W4; the method (rotation + trellis + activation scaling) exists on sc sub4bit/drift-study |
| 9 | **Faster CPU path on the Pi** | unchanged | unchanged | yes (same model, same math order if kept) | our Pi engine: 1B at 1.20 tok/s = 5.7 percent of the Pi's 17.07 GB/s ceiling (calc:8c5dfc2e) | software gap, not physics: the Pi is compute- or code-bound today, not memory-bound; profiling it is the next step on the aienos side |
| 10 | **Distillation** (train a small model to imitate the big one) | the small model's bytes | the small model's | **no: a different model**, labelled | none | only for a box with no network and no room for the 30B; never counted as a speedup of the big model |

Ruled out, with the physical limit and what would have to be true:

- **27B dense at FP8 or W4 on any 8 GB box**: the model is 26.89 / 13.65 GB and every word reads all of it. Streaming
  it from a 0.45 GB/s Pi disk costs 30 s per word at W4 (13.65 GB / 0.45 GB/s, INFERRED). Yes if the box has 16 GB+
  and W2 quality holds (2.27 tok/s ceiling on a 16 GB Pi, calc:029d27ac), or via lever 5.
- **Keeping the 30B on the RTX 5070 entirely in its 12 GB**: W4 is 16.7 GB. Yes at W2 (9.2 GB, 704 tok/s ceiling,
  calc:d1c1277d, quality UNKNOWN), or with 4158 W4 expert slots on the card (calc:b41147b7) and misses over PCIe from
  PC RAM (63 GB/s at x16, lane count unconfirmed; PC RAM size UNKNOWN).
