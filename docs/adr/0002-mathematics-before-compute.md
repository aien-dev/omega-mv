# ADR 0002: Mathematics before compute

- Status: Proposed 2026-10-20 (ledger L274, branch feat/math-before-compute). Needs Drake's acceptance.
- Date: 2026-10-20
- Drake, 2026-10-20: "I want most of this machine to be mathematics, with as little compute as possible, minimum compute
  as possible. Mathematics rules the day here."
- Evidence: [docs/speed/WORK_PER_TOKEN.md](../speed/WORK_PER_TOKEN.md) (the count, the identities, the measurements).

## Context

One decoded word of Qwen3.8-27B on the live engine reads 26.92 GB of weights (calc:741df630 + calc:81016d51) and does
25.62 G multiply-adds with them (calc:dc47e9b6). The GB10 moves at most 273 GB/s, so the hardware ceiling is
10.14 words/s (calc:0c1b9334); the live server reaches 7.63. Every weight byte is read for one word and then thrown
away. Arithmetic is not the limit: at 7.63 words/s the GPU does about 0.2 T multiply-adds per second, a small share
of what it can do. The cost of a word is the bytes it reads, the launches and waits around them, and the joules.

"Faster kernels" only close the gap to the ceiling. Mathematics can move the ceiling itself, without changing a
single output bit, by proving that some of the work is not needed.

## Decision

1. **A product is computed only when no identity proves it unnecessary.** Before a stage reads a byte or multiplies a
   pair, we ask whether an exact identity (algebra, information theory, a bound) shows the result is already known,
   is zero, cannot change the answer, or can be shared with work already done. If such an identity exists, the work
   is removed.
2. **Exact means identical tokens.** A change that removes work must give token-for-token identical output on the
   golden set against the reference engine, and where the claim is bit-exact (same bytes into the same arithmetic in
   the same order) the gate checks bits. A change that is only close is labelled NOT IDENTICAL, measured as an
   approximation (drift table, agreement, KL), and kept out of the main line until Drake accepts its accuracy cost.
3. **Receipts count work removed, not just time.** Every speed receipt carries the work count before and after:
   bytes read from DRAM per word, multiply-adds per word, launches per word, syncs and host syscalls per word, and
   joules per word at the wall. Time alone can hide a regression in work (a faster run on a quieter box); work alone
   can hide a regression in time (a cheaper method the hardware runs badly). Both are reported, each with its command.
4. **Ranking is by work removed per word.** Bytes come first while decode is memory bound; multiply-adds that do not
   change bytes are ranked last, because they do not move the ceiling.
5. **Refuted identities are kept on record with their numbers**, so nobody spends a day rediscovering why they fail
   (WORK_PER_TOKEN.md section 3 lists them: lm_head norm bounds, folding norms into FP8 weights, exact-zero skipping).

## The work-per-token gate

- `docs/speed/WORK_PER_TOKEN.md` is the ledger of work per word for the served model. Its numbers are produced by
  commands written next to them (aien-calc receipts for the analytic count, tool runs for the measured parts).
- A PR that changes the decode path states its work delta per word in the PR body: bytes, multiply-adds, launches,
  syncs/syscalls. **A PR that adds work per word must say why** (what it buys, measured), and the reviewer checks the
  claim against the count. A PR that removes work cites the identity and the exactness gate it passed.
- Until a machine check exists, the gate is a review rule. The machine check (owed, next step): a host-only test that
  rebuilds the per-word count from the q38 graph recording (launch list, weight bytes per launch, from
  `omega_gpu_q38_graph_*` stats) and fails when bytes or launches per word grow without an updated baseline file.

## How it binds the kernel plan (L184, Omegese for the kernel)

- Omegese kernel words that move memory (K2 peek/poke, K4 flat placement) are where bytes are spent. A kernel word
  that streams a weight tensor must be able to stream it in a lossless coded form (the FX1 code of WORK_PER_TOKEN.md
  section 4), so the language does not force the raw byte width onto the memory system.
- Omegese units (K5, L096) carry bytes and multiply-adds as first-class units, so a kernel's work per call is a
  typed, checkable number and the work-per-token count can be derived from the program instead of measured after.

## How it binds the driver model (aienos L215, a driver as a checked device contract)

- A driver contract states the work each operation costs: doorbells, DMA bytes, interrupts or polls, syscalls. The
  same rule applies: a poll loop is work, and a closed form (an event, a predicted completion time, a fence the
  device signals) that removes iterations is preferred when it is exact (never misses a completion).
- Measured example (WORK_PER_TOKEN.md section 2): the decode loop's host side waits for the chip with a sleep-poll;
  its syscalls and context switches per word are counted there. The driver model should give completion as one
  event, not a loop.

## Consequences

- Speed work gains a second axis: bytes and multiply-adds removed, independent of how busy the box was.
- Lossless weight coding becomes a first-class engine feature (top identity today: 17 percent fewer weight bytes
  at the information-theoretic floor of the exponent code, bit-identical output; WORK_PER_TOKEN.md section 4).
- Some ideas that sound like mathematics do not pay on this model, and the record says so with numbers.
- Cost: every decode PR carries a work delta. The count is cheap (aien-calc + the graph's own stats).

## Relation to other records

- Follows OMEGA-SYSTEMS-CORE-0000 (receipts, reproducibility) and the L123 speed audit (docs/SPEED_AUDIT_2026-10-19.md
  on doc/speed-audit), whose byte table this ADR reuses.
- Shares the counting harness with L272 (on-chip megakernel: launches and DRAM traffic); L272's per-kernel timeline is
  cited, not rebuilt.
