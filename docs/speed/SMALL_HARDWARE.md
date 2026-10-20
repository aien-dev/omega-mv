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
- **Probe result so far:** streamed experts give bit-identical words (smoke run). With the memory an 8 GB Pi can spare, 92 percent of expert lookups hit the cache, so the 30B streamed from a Pi NVMe drive has a ceiling of 3.76 words/s (calc:e35fa8b7), about the 4.25 of an 8B held fully in memory (calc:edc54726). Timed stand-in run: section 5.
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

### 4b. The whole grid: every model, format and target

Generated by `research/small-hw/grid.sh` (INFERRED arithmetic, one receipt per cell). "fits" = whole model plus 1.7 GB
(system 1 GB, KV 0.4 GB, scratch 0.3 GB; the 5070 card 0.9 GB) fits in memory: the cell is memory speed / active bytes.
"streams X GB/word" = a dense model that does not fit reads the part that stays outside memory again for every word:
time = active / memory speed + X / disk speed. For the RTX 5070 the "disk" is PC RAM over PCIe 5.0 x16 (63 GB/s; PC RAM
size UNKNOWN). MoE rows that do not fit use the expert cache (section 5); only W4 has predicted numbers there.

| Model | Pi5-8GB | Pi5-16GB | M2Air-8GB | M2Air-24GB | RTX5070-12GB | Laptop-8GB |
|---|---|---|---|---|---|---|
| 27B-BF16 | streams 44.902797 GB/word: 0.00973 (calc:0d9f4fe1) | streams 36.902797 GB/word: 0.0118 (calc:6b4ae2e2) | streams 44.902797 GB/word: 0.0318 (calc:c15269dd) | streams 28.902797 GB/word: 0.0954 (calc:78ce87c9) | streams 40.102797 GB/word: 1.4 (calc:b4c59d23) | streams 44.902797 GB/word: 0.0626 (calc:6b1d5cd4) |
| 27B-FP8 | streams 20.590000 GB/word: 0.0211 (calc:e38dafc4) | streams 12.590000 GB/word: 0.0338 (calc:18bee639) | streams 20.590000 GB/word: 0.0691 (calc:b6c1e26a) | streams 4.590000 GB/word: 0.54 (calc:f7759de2) | streams 15.790000 GB/word: 3.44 (calc:4e7e8120) | streams 20.590000 GB/word: 0.135 (calc:006f6862) |
| 27B-W4 | streams 7.350000 GB/word: 0.0584 (calc:1582a08c) | fits: 1.25 (calc:744d88d8) | streams 7.350000 GB/word: 0.192 (calc:e97e5921) | fits: 7.33 (calc:151e41c9) | streams 2.550000 GB/word: 16.5 (calc:2b9aa5f6) | streams 7.350000 GB/word: 0.368 (calc:53cc5e91) |
| 27B-W2 | streams 1.218243 GB/word: 0.318 (calc:50f3917c) | fits: 2.27 (calc:2bb63001) | streams 1.218243 GB/word: 1.09 (calc:6efeb200) | fits: 13.3 (calc:2872c234) | fits: 89.4 (calc:ebe27237) | streams 1.218243 GB/word: 1.81 (calc:7f5b67c9) |
| 30B-BF16 | streams experts (section 5) | streams experts (section 5) | streams experts (section 5) | streams experts (section 5) | streams experts (section 5) | streams experts (section 5) |
| 30B-FP8 | streams experts (section 5) | streams experts (section 5) | streams experts (section 5) | streams experts (section 5) | streams experts (section 5) | streams experts (section 5) |
| 30B-W4 | streams experts (section 5) | streams experts (section 5) | streams experts (section 5) | fits: 61.2 (calc:aabae990) | streams experts (section 5) | streams experts (section 5) |
| 30B-W2 | streams experts (section 5) | fits: 17.9 (calc:e6ffd807) | streams experts (section 5) | fits: 105 (calc:af1168e5) | fits: 704 (calc:06e6fa58) | streams experts (section 5) |
| 8B-BF16 | streams 8.836195 GB/word: 0.0487 (calc:0e7c4e4b) | streams 0.836195 GB/word: 0.364 (calc:4efa7b6f) | streams 8.836195 GB/word: 0.16 (calc:73171327) | fits: 6.61 (calc:9d4b70b0) | streams 4.036195 GB/word: 11.5 (calc:20ac8825) | streams 8.836195 GB/word: 0.309 (calc:202d4e3b) |
| 8B-FP8 | streams 1.890427 GB/word: 0.214 (calc:d587a59f) | fits: 2.08 (calc:b96207fa) | streams 1.890427 GB/word: 0.722 (calc:77b84f1d) | fits: 12.2 (calc:426be524) | fits: 82 (calc:1df28171) | streams 1.890427 GB/word: 1.27 (calc:bb6200f5) |
| 8B-W4 | fits: 4.25 (calc:18e9324c) | fits: 4.25 (calc:18e9324c) | fits: 24.9 (calc:16bbf797) | fits: 24.9 (calc:16bbf797) | fits: 167 (calc:6bb962fb) | fits: 12.7 (calc:71ebc911) |
| 8B-W2 | fits: 7.47 (calc:66254642) | fits: 7.47 (calc:66254642) | fits: 43.8 (calc:cc0b6336) | fits: 43.8 (calc:cc0b6336) | fits: 294 (calc:6e80e2f1) | fits: 22.4 (calc:15bb89d2) |

Reading the grid: a dense model that does not fit and has to come from a disk is unusable (0.01 to 1.8 words/s), because
every word re-reads gigabytes from the disk. The RTX 5070 is different: its second tier is PC RAM over PCIe, so the 27B W4
streams at a 16.5 words/s ceiling (if the PC has the RAM). The 30B MoE is the exception because only its chosen experts are read and most of them
are already cached (section 5).

## 5. The probe: the 30B with its experts streamed from disk at a capped speed (STAND-IN, not a Pi number)

**What was built** (`research/small-hw/moe_stream/`, reusing our 30B CPU reference from aien-sovereign-core main
1e6ae85c, no second engine): the same forward pass, run three ways on the same prompts in one process:
`ref` = the unmodified reference (`forward_token`); `stream:CAP:CACHE` = the experts are not taken from memory but
read from the checkpoint file on disk with O_DIRECT (no operating-system file cache) through a reader paced to CAP
MB/s, into a fixed set of expert slots (least recently used is evicted). Every expert that lands is compared byte for
byte with the resident copy, and every word's full logits vector (151,936 numbers) is hashed and compared with `ref`.

**Cache size, derived not picked** (calc receipts in section 3's file): on a Pi 5 8 GB at W4 the room left for experts
after 1 GB of system, the shared 4-bit weights (0.67 GB), the bf16 embedding table (0.62 GB), a 4,096-word KV cache
(0.40 GB) and 0.3 GB scratch is 1,995 expert slots of 6,144 (calc:1013332d). Pi 5 16 GB: 5,186 (calc:c784597b).
RTX 5070 card: 4,158 (calc:b41147b7). The probe streams the shipped FP8 experts, so the Pi 8 GB case is run as 1,995
FP8 slots (9.41 GB, calc:9e9f4602): same number of experts held, twice the bytes per miss of W4.

**Hit rates** (OBSERVED, exact counting over a recorded routing trace: 2 prompts, 307 positions, 256 written words,
`evidence/SMALL-HW-L273/trace128/sim128.txt`; hit rates do not depend on machine load):

| Expert slots | Target it stands for | Hit rate while writing (LRU) | Misses per word | Best possible (Belady) |
|---|---|---|---|---|
| 384 (one word's worth) | | 0.367 | 243 | 0.664 |
| 1,000 | | 0.735 | 102 | 0.878 |
| 1,995 | Pi 5 8 GB, 8 GB laptop, M2 Air 8 GB | **0.920** | **30.6** | 0.968 |
| 4,158 | RTX 5070 card | 0.980 | 7.6 | 0.983 |
| 5,186 | Pi 5 16 GB | 0.983 | 6.7 | 0.983 (= first-touch floor of this short trace: 4,891 of 6,144 experts were touched at least once) |

Limits: one short trace on two prompts (code and story); a long conversation or a topic change will move the working
set. The first-touch floor shrinks as a conversation gets longer. UNKNOWN: hit rate over thousands of words.

**Predicted ceilings for the 30B at W4 with its experts streamed** (INFERRED; time per word = active bytes / RAM speed
+ miss bytes / disk speed, no prefetch overlap; 30.6 misses per word = 76.7 MB at W4, calc:3ac7c2af):

| Target | Disk | Ceiling tok/s | With full prefetch overlap (L271) | Compare: 8B W4 fully in memory |
|---|---|---|---|---|
| Pi 5 8 GB | NVMe Gen 2, 0.45 GB/s | 3.76 (calc:e35fa8b7) | 5.87 (disk-bound, calc:b7a5646f) | 4.25 (calc:edc54726) |
| Pi 5 8 GB | NVMe Gen 3, 0.9 GB/s | 5.52 (calc:4478ab4f) | up to 10.4 (RAM-bound, calc:847a8763) | 4.25 |
| Pi 5 8 GB | SD card, 96 MB/s | 1.12 (calc:f376425a) | | 4.25 |
| Pi 5 16 GB | NVMe Gen 2 | 7.51 (calc:83bcf6b4) | | |
| 8 GB laptop | NVMe 3 GB/s (assumed) | 17.4 (calc:bf5d8976) | | 12.7 (calc:d2c3a087) |
| M2 Air 8 GB | 256 GB SSD 1.45 GB/s | 14.4 (calc:dc4b9364) | | 24.9 (calc:813bbba0) |
| RTX 5070 | misses from PC RAM over PCIe x16 | 366 (calc:cf9a625d; PC RAM size UNKNOWN) | | 167 (calc:7a98ea66) |

Plain reading: on an 8 GB Pi with an NVMe drive, the 30B mixture-of-experts model, streamed with a cache, has about the
same speed ceiling as an 8B model held fully in memory, and it is the far larger model (30.5 B
parameters against 8.2 B; a quality comparison on our own texts has no receipt yet). On an SD card it drops to 1.12 against
3.76. These are ceilings; our Pi engine today reaches 5.7 percent of its memory ceiling (calc:8c5dfc2e).

**Measured on the Spark** (STAND-IN for the disk only: the Spark's CPU computes, its NVMe is capped to Pi-class
speeds; the compute time is the Spark's, not a Pi's):

PENDING (timed run waits for the measurement window; smoke run with 4 words already passed the identity gate, `evidence/SMALL-HW-L273/smoke`).



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

