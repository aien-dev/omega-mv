# Turing Yield TY-7 protocol: bits of T per wall joule for the 27B on our engine (pre-registered)

Status: PRE-REGISTERED 2026-10-19. Committed and pushed on branch `research/turing-energy-rerun` (base omega
`a8ffd2b9`) before any timed window. Any change after data is seen is a dated amendment at the end of this file,
committed before the run it affects. Ledger line L109.

## 1. Question

How many bits of T does the 27B language model earn per joule of wall electricity while it decodes? T is defined by
measurement profile V0 (`TURING_YIELD_PROFILE_V0.md` section 1): T(M; B, D) = DL(B, D) - DL(M, D), with
DL(M, D) = L(M) + L(D|M), every length in bits, every model paying for its own description.

Plain words: T counts how many bits shorter a text becomes when written down with the help of the model instead of
the baseline, after the model has paid for writing itself down too. E is the electricity the box drew while the
model did the work. T/E is bits saved per joule.

## 2. What the server can and cannot give (ground truth, checked before registration)

- The work is the live 27B server: `aien-serve-http` on 127.0.0.1:18091, health `engine: omega-qwen38`, model
  `qwen3.8-27b-fp8`, model directory `~/models/qwen3.8-27b-fp8-017b9c7a`, binary
  `~/.local/state/aien-q38/bin-fast-46134247/aien-serve-http`, flags `--thinking off --max-tokens-cap 16384`.
  It is never restarted or stopped by this work; one request at a time; `server_busy` is retried.
- **The server does not serve the log-probability of a given text.** Its request type
  (aien-sovereign-core `10b4f851`, `crates/aien-serve-http/src/openai.rs`) has no `logprobs`, `echo` or
  `prompt_logprobs` field. So the code length of a held-out *given* text under the model cannot be read from it, and
  the TY-7 form named in the brief (score a fixed held-out corpus token by token) is **not measurable today**.
- What it does serve: `aien_sampling.reply_logprob`, the natural-log probability of the exact sequence of tokens it
  sampled, summed over draws, under the kept distribution (`crates/aien-serve-http/src/sampling.rs:131`), and the
  sampled token ids (`aien_return_token_ids`). The sampler (`crates/aien-inference-abi/src/transformer_backend.rs`
  `sample_with_params_traced`) computes the full softmax at the requested temperature in f64; with top_p = 1.0 and
  top_k = 0 nothing is cut (`kept` = every token with p > 0), and each draw's log-probability is
  ln(p_i), rounded to f32.

## 3. Fallback used (fits PROFILE V0, declared honestly)

**D = the token sequences the model itself samples at temperature 1.0, top_p 1.0, top_k 0** (untruncated: the
model's own distribution), in reply to a fixed prompt set. Then:

- L(D|M) = -(sum of `reply_logprob` over the replies) / ln 2, in bits. This is exact for D under M (it is the
  ideal code length of the sampled draws), up to f32 rounding of each draw (relative 2^-24, below 2e-6 bits per token
  for any draw under 30 bits). The per-reply sum is reported by the server in f64.
- D is held out from every fit step: B is not fit on anything (section 4). The prompts are text the model reads,
  not part of D, and are coded by nobody.
- **This favours the model.** On its own samples the model is the true source, so its expected code length is its
  own entropy rate, the least any code can achieve on average. On a human text the model's code length would be
  larger by the divergence between that text's source and the model. So G and T here are a best case for the
  model on text of this kind, not a measurement on a held-out human corpus. The given-text measurement needs a
  scoring path in the engine (a teacher-forced log-probability endpoint); that is a named follow-up, not done here.
- **No learning during the run.** The model does not update its weights while decoding. T here measures the
  model's description-length gain over the baseline on held-out text; it does not measure learning during the
  run. No prefix-cache or in-context effect is defined or measured in TY-7.

## 4. The baseline B and the ladder

All models code the same symbol stream: per reply, the sampled token ids in order, followed by one end symbol
(id V, the stop marker, V = 248,320 = `vocab_size` in the model's config.json) if and only if the reply's
`finish_reason` is `stop`. The end symbol is coded by B so that B describes where each reply ends, which the model's
own code does as well when it samples its stop token. If the server's `reply_logprob` already includes the stop
draw and the token ids omit it, B's extra end symbol is the matching cost; if the token ids include the stop token,
no extra symbol is added (rule applied mechanically by the reducer from `sampled_tokens` versus the id count, and
reported). Alphabet size K = V + 1.

- **uniform**: log2(K) bits per symbol. L(M) = 0 beyond the declared K.
- **B = prequential order-1 token model with escape (PPM-C style, exclusion at the order-0 step)**, started empty
  at the start of each round and updated after every symbol (`ty.ty7.ppmc1.v0`, tools/ty7_reduce.c). It stores no
  table: it learns from the stream it is coding, and its learning cost is inside L(D|B). L(B) = 0 table bits (the
  coder is fixed code, the same for every round; this is the PROFILE V0 rule "models pay for their tables" with an
  empty table). Exact rule, per symbol x with previous symbol c (c = start-of-reply at the first token of a reply):
  - Order 1: if context c has total count n_c > 0 with u_c distinct symbols: a seen symbol costs
    -log2(n_{c,x} / (n_c + u_c)); otherwise escape costs -log2(u_c / (n_c + u_c)) and coding goes to order 0
    excluding the symbols seen in c. If n_c = 0, go to order 0 with no escape and no exclusion.
  - Order 0 (with exclusion set S, possibly empty): n' = n - sum_{y in S} n_y, u' = u - |S|. If n' > 0: a seen
    symbol not in S costs -log2(n_x / (n' + u')); otherwise escape costs -log2(u' / (n' + u')) and the symbol is
    coded uniformly over the K - u symbols never seen at order 0: log2(K - u). If n' = 0, go straight to that
    uniform step with no escape cost.
  - Then n_{c,x}, n_x and the distinct-symbol counts are updated. log2 is the C library's double log2; the
    per-symbol error (< 1e-12 relative) is negligible against every threshold below.
- **B_warm (context only, does not count)**: the same coder, primed on the other two rounds' streams before coding
  round r, with the primed counts NOT charged. It shows how much an order-1 count model could gain from more text
  of the same kind; it violates V0's table charge and is never used for a pass.
- **M = the 27B**: L(M) = 8 x the bytes of the files the engine needs to decode text: `layers-*.safetensors`
  (64 files, 24,382,562,592 bytes), `outside.safetensors` (6,007,102,112), `tokenizer.json` (12,809,320),
  `config.json` (51,350), `chat_template.jinja` (8,952). **L(M) = 243,220,274,608 bits**
  (calc:6f0cbc58cb898a58b95ddbd2c0760316ac8a6b8518a1baea49e16dd7df6dc598). `mtp.safetensors` (477,202,224 bytes)
  is excluded because text decode with `--thinking off` and no speculative mode is not shown to read it; excluding
  it only lowers L(M) (favours the model). The whole directory (30,890,056,546 bytes) is reported as an upper value.
  The engine binary itself is not charged (neither is B's coder).

Reported numbers per round and pooled: K, symbols, L(D|uniform), L(D|B), L(D|B_warm), L(D|M), and

- **T_V0 = DL(B) - DL(M) = L(D|B) - (L(M) + L(D|M))** (the PROFILE V0 quantity, full L(M) charged);
- **G = L(D|B) - L(D|M)** (the conditional gain: the model's description taken as already paid). G is NOT a V0
  quantity; it is reported because it is the number that grows with work done, and with it the break-even
  N* = L(M) / (G per symbol), the number of symbols the model must code before T_V0 turns positive.

## 5. Prompts (fixed, hashed)

`evidence/TURING_YIELD/ty7-2026-10-19/prompts.txt`: 8 lines, one prompt each, written for this protocol (topics
chosen to vary: weather, a recipe, a bridge, a letter, a market, a train, a garden, a workshop). Each request sends
`"Continue this text in plain prose. Write only the continuation.\n\n" + line` as one user message. The file's
SHA-256 is recorded in the evidence README before the first window. The prompts are not D and are never scored.

Request body per prompt i (1..8) in round r (1..3): `temperature 1.0, top_p 1.0, top_k 0, max_tokens 256,
seed 1000*r + i, aien_return_token_ids true, stream false`. Same prompts every round, different seeds, so each
round codes a fresh, independent D. Every reply is checked: `aien_sampling.top_k_applied == false`,
`aien_sampling.top_p` absent or 1, `sampled_tokens` > 0, `greedy_tokens == 0`. A failed check refuses the round.

## 6. Energy

Meter: wall power from the UPS (`~/yardsticks/sensors/<date>.csv`, one row every ~2 s: epoch s, wall W, GPU W,
GPU util, load1, MemAvailable kB, lock holders, NVRM errors). Wall W = UPS load x 10 W (1% steps of 1000 W),
whole UPS output, not the Spark alone. Package energy: the `aien_spbm` hwmon is **not loaded** on this boot
(no `/sys/class/hwmon/*/name` equals `aien_spbm`; loading it needs sudo, which agents do not use), so
**E_pkg = UNAVAILABLE** and is reported as such. NVML is not used anywhere in the measured path; nvidia-smi columns
in the sensor log are context only.

The server is shared (aien-translate keeps it busy most of the time) and runs one generation at a time, so during
one of our requests only our generation runs in the engine. Energy is therefore counted over **our request
intervals only** (from send to last byte, epoch ns, recorded by the driver):

- **E_gross** = sum over requests of (mean wall W of the sensor rows inside the request interval) x duration. It
  includes the box's base draw and anything other tenants run meanwhile, so T/E_gross is a **lower bound** on bits
  per joule.
- **E_incr** (idle subtracted) = sum over requests of (mean wall W inside - P_base) x duration, P_base = mean wall W
  of the sensor rows in the round's two bracketing IDLE windows at which the engine was idle (health `busy: false`
  at the nearest poll before and after the row; health polled every 1 s for the whole session). If fewer than 5
  such rows exist, E_incr for that round is UNRESOLVED and only E_gross is used. spark-receipt's own
  `incremental_J` per WORK window (idle = 30 s before) is reported beside it.
- Readability: a round counts only if its summed request time is >= 30 s.

Also reported per round: tokens, request seconds, tokens per second, J per token (gross and incremental), mean GPU
W, load1 and MemAvailable at start and end, lock holders, server busy fraction during IDLE windows.

## 7. Session order and box rules

One session: IDLE0, WORK1, IDLE1, WORK2, IDLE2, WORK3, IDLE3. IDLE windows are 60 s with no requests from us;
WORK window r runs the 8 prompts of round r in order. Before the session: `ledger list | grep -iE
"measure|timing|ceiling"` (wait for any open measurement), `ledger note L109 "MEASURING ty7 start"`; after: "end".
The session runs as `quiet-measure flock /tmp/aien-gb10.lock spark-receipt ty7-session -- tools/ty7_drive.sh`
(other chip jobs kept out; the server itself does not take the lock), each WORK window inside its own
`spark-receipt ty7-wN`. Per the 20-minute rule, if the quiet lock waits over 20 minutes the session runs anyway and
records the load. A server reply of `server_busy` is retried after 1 s; the wait is not inside any request interval.

## 8. Predictions (made before measuring)

| quantity | prediction | reasoning |
|---|---|---|
| L(D\|M) per token | 2.0 bits (1.0 to 3.5) | untruncated temperature-1 prose continuation from a model of this size; own-sample entropy is low but temperature 1 keeps the tail |
| L(D\|B) per symbol | 11.5 bits (9 to 14) | about 2,000 symbols per round against a 248,321-symbol alphabet: most early tokens are new and cost about 18 bits plus escape; repeats get cheap only late in the round |
| G per symbol | 9.5 bits | difference of the two lines above |
| T_V0 | about -2.43e11 bits per round: **FAIL** | L(M) = 2.43e11 bits and a round's G is about 2e4 bits; the model's table cannot be repaid by a few thousand tokens |
| N* break-even | about 2.6e10 symbols, about 127 years of nonstop decode at 6.4 tok/s (calc:b6d02c57e200cebb10bcbe8d06e57340c71a345e973c18d0fe50293d92a460e5) | L(M) / 9.5 bits; 6.4 tok/s is the recent single-stream decode rate on this engine (qwen38 serve handoff, calc:e3e66420) |
| decode rate | 6 tok/s (4 to 8) | as above; shared box |
| wall W during our requests | 225 W | sensor log while the engine decodes for aien-translate: 220-230 W; base without decode about 170 W |
| J per token | gross 35 J, incremental 9 J | 225 W / 6.4 tok/s; (225 - 170) W / 6.4 tok/s |
| G / E_incr | about 1 bit per J | 9.5 bits / 9 J |
| G / E_gross | about 0.27 bit per J | 9.5 bits / 35 J |
| T_V0 / E | about -1e6 bits per J | -2.43e11 bits over about 6,000 tokens x 35 J |

## 9. Pass rules (fixed now)

Error bound per round: eps = 2e-6 bits x symbols (f32 rounding of each draw) + 1e-6 bits x symbols (double log2 in
B), stated in the reducer output.

**TY-7 V0 PASS** (the PROFILE V0 rule, section 9 form) iff both:
1. pooled T_V0 over the three rounds (L(M) charged once) minus eps exceeds the margin 3 sqrt(3) sd_G, where sd_G is
   the sample standard deviation of the three per-round G values (L(M) is constant, so it is also the spread of
   T_V0);
2. each round's T_V0 with the full L(M) charged to that round alone, minus eps, is > 0.
Otherwise **FAIL**, reported plainly and kept. (Predicted: FAIL.)

**Conditional gain check (G-rule, not V0, reported separately)** PASS iff all of:
1. every round's G minus eps > 3 sd_G (gain clearly above round-to-round spread);
2. repeatability: every round's G per symbol is within 10% of the three-round mean;
3. energy repeatability: every round's G / E_gross is within 20% of the three-round mean.
Each round must be readable (section 6). The T/E figures are reported with their round spread either way.

## 10. Code and evidence

C and shell only, no Python, no CUDA, no NVML in the measured path. `tools/ty7_drive.sh` (session driver, curl),
`tools/ty7_reduce.c` (reply checks, symbol stream, B and B_warm code lengths, sensor integration, verdicts).
Evidence in `evidence/TURING_YIELD/ty7-2026-10-19/`: prompts and hash, raw reply JSON per request, request interval
log, health poll log, sensor CSV slice for the session, spark-receipt copies and paths, per-symbol B cost log,
reducer output, verdicts. Result in `docs/turing/TURING_YIELD_TY7_RESULT.md`.

## Amendments
