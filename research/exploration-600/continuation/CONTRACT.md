# L523: prospective bounded continuation guard

Direct implementation by Hermes, parent L487, base b59631c510ea4555535578ef04041f402e05eaa9. This contract is fixed before new implementation or counterfactual evaluation. Original L492/L517 source, policy, freeze, 300 rows and scientific FAIL remain immutable.

## Root cause and limits
The original exploration policy does not receive the objective horizon: it performs the same search then scores horizons 1/8/32 afterward. Its five no-payoff cases cannot yield strictly positive lifecycle gain because the incumbent is already the cheapest reachable result and search has positive cost. No honest code fix can make the original universal-positive/always-beats-random hypothesis pass. Do not gate-relax, retune sealed workloads, or hide those results.

Fix the actionable continuation defect: expose an explicitly bounded native Omega admission decision using horizon, best qualified incumbent, paid cost, next trial charge, total budget and declared risk cap. This is an offline experimental guard, not runtime authority, a replacement scheduler, or a claimed successful learning campaign. Valid admission proves possible incremental gain, not probable gain, guaranteed repayment, or absence of loss.

## Protocol and accounting
One request: ten unsigned decimal u64 values, ASCII whitespace separated (HT 0x09, LF 0x0a, VT 0x0b, FF 0x0c, CR 0x0d, or space 0x20). The complete request through EOF must be at most 16,383 bytes, including separators, trailing whitespace and leading zeroes. At 16,384 bytes or more, refuse exit20 with no output. This is bounded EOF framing, not a persistent stream protocol:
`1 baseline best horizon spent next_charge budget risk_cap frontier_count guard_cost`.
Exact arity, strict syntax and unsigned overflow refusal. Exit20 with no output for invalid protocol/version, zero horizon/next_charge, best>baseline, or frontier_count>8. Lower bound is universally valid zero; do not import oracle optima or unseen candidate costs.
Output four decimal lines: reason, paid_after_guard, break_even_reuses, break_even_overflow. Codes:1 admit;2 total budget exhausted;3 risk cap exhausted;4 no frontier;5 best is zero/global minimum;6 maximum possible incremental saving cannot exceed next trial cost. Guard_cost is explicit synthetic declared decision cost, not measured CPU cost. If it cannot be paid within both limits, do not add it or perform a trial. Already-paid cost is never reset or refunded. Decision cost is charged even when the decision stops if affordable. On a repeated decision the caller must pass updated paid state; no idempotence/free retry or persistent ownership is claimed.

Economic rule: continue only if `horizon * best > next_charge`, using exact overflow-safe arithmetic. This compares possible *incremental* improvement with the next cost; sunk cost must not wrongly veto a future profitable incremental improvement. Limits additionally require `paid + next_charge <= budget` and `<= risk_cap`. No speculative candidate is promoted. Best=0 stops even with an empty numeric cost; empty frontier stops. Break-even compares total paid with baseline-best; zero means no finite repayment when saving is zero. If the first strictly profitable reuse count exceeds u64, output UINT64_MAX with overflow flag1, never wrap.

## Verification fixed before evaluation
- Meaningful RED from a compilable native stub that always admits; implementation must fail horizon/budget/cap/exhaustion tests before fixing.
- Structurally independent Python big-integer oracle, hand cases, deterministic randomized/extreme u64 inputs and malformed protocol; verify compiled artifacts are nonempty AArch64 ELF and compiler hash/provenance.
- Compiled behavioral mutants: omit risk cap, ignore horizon, free decision cost, ignore empty frontier, overflow/wrap break-even. Compilation failures cannot count as caught.
- Regression: rerun existing original 2337 checks,40 diagnostic checks and six fault controls; do not edit their policy or evidence.
- Known-trace replay only: original recorded candidates/transitions remain fixed. For horizons1/8/32, compare unguarded path with guarded prefix using budget160, risk caps32 and160 and guard_cost1. Replay emits all outcomes, overhead, losses, stop reason, candidate exposure. This is counterfactual truncation of known synthetic traces, not a fresh held-out campaign, actual resumed training, or causal hardware/model evidence. Bound claims to protocol/cap correctness, report any reduction in useful discovery or payoff.
- Original sealed rows and policy hashes must remain byte-identical. No external worker; no GPU/model/server/driver/service changes. Real performance/quality/energy are NOT_RUN/UNMEASURED.
