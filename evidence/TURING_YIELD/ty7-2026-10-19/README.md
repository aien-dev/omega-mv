# TY-7 evidence (2026-10-19 local, session 2026-10-20 01:35Z to 01:59Z)

Protocol docs/turing/TURING_YIELD_TY7_PROTOCOL.md; result docs/turing/TURING_YIELD_TY7_RESULT.md.

- prompts.txt, prompts.sha256: the 8 fixed prompts (SHA-256 0978a272f25d851a8e5a461e834af4a7cc0c4dceac359bb489791b49e964449c), hashed before the first window.
- run/r<R>-p<I>.json: the 24 raw server replies (token ids, aien_sampling with reply_logprob and per-draw kept counts).
- run/intervals.log: per request round, prompt, send ns, last-byte ns (epoch), HTTP code, attempts, GPU-lock waits.
- run/session.log: IDLE and WORK window boundaries; run/health.log: server busy flag every 1 s.
- run/symbols.tsv: per symbol the B and B_warm code length in bits (the model's code length is only served per reply).
- sensors-slice.csv: the wall sensor rows for the session (epoch s, local time, wall W, GPU W, GPU util, load1, MemAvailable kB, GPU lock holders, NVRM errors).
- receipts/: spark-receipt JSON copies (originals in ~/yardsticks/receipts/2026-10-19-ty7-{session,w1,w2,w3}.json).
- reducer.txt: tools/ty7_reduce.c output, both verdicts. Reproduces from this folder alone.

Verdicts: TY-7 V0 FAIL (predicted); G-rule PASS. E_incr UNRESOLVED, E_pkg UNAVAILABLE.
