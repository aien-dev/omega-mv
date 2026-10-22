# Omega #600: direct investigation and native continuation guard (L523)

Implemented and exercised directly by Hermes, not delegated. This is a separate bounded research change based on the reviewed diagnostic correction. No original search policy, frozen result, or sealed campaign file was changed.

## What failed

All five no-payoff family-7 cases have the same result: qualified best cost100, search spent129, horizon32, net -129 synthetic work units. The original reference is already cheapest among all reachable candidates. Any positive-cost search therefore loses on that family. Strictly positive benefit on every case cannot be obtained without changing the task, granting free search, or introducing knowledge unavailable to the policy. None was done.

C also beats the random arm on only15 of25 cases. That separate comparison is preserved. The original scientific verdict remains **FAIL**.

## Actual implementation

`guard.om` is seeded native Omega, compiled with the reused qualified seed compiler SHA256 `01b1545b23e8ff40cb5691e20b5df910ea4acaefde3495f0d62d270465b98cd6` (seed source57f5f4258cebf3d5b2c22853cd062e351265c525). It is a nonempty statically linked AArch64 ELF, with no first-party C or Rust decision implementation. Python files are independent test/replay plumbing only.

The guard explicitly checks objective horizon, paid search/decision cost, next trial charge, overall budget, declared downside cap, empty frontier and already-optimal zero cost. Arithmetic is overflow-safe, including break-even counts beyond u64. It uses only a universally valid cost lower bound0, never unseen fixture optima. It does not reject a useful incremental improvement merely because past spending is already sunk.

This is an offline experimental admission primitive, not execution authority, integration into the live search engine, an automatic production switch, or proof of expected value. Its maximum-possible-saving test is necessary, not sufficient for actual payoff. Its risk cap bounds allowed spending, not guaranteed positive return.

## Verification

- Test-first native always-admit stub compiled successfully and failed421 of423 checks for behavioral reasons. Original red JSON retained.
- Native implementation:423/423 independent big-integer, boundary, malformed protocol and u64-extreme checks passed.
- Five separately compiled behavioral mutants caught: omitted risk cap, ignored horizon, free decision cost, ignored empty frontier and wrapped break-even. Compiler failures do not count as kills.
- Full original experiment regression:2337/2337, diagnostics40/40 and six existing mutation controls passed in fresh output.
- Original300 rows retain SHA256 `eae930c030069e3f35ee755a7132ffb843522b5f588d00748b31e1b67b2846a1`; original policy and frozen evidence unchanged.
- Initial development compilation hit the known16-register-per-function limit (error12 at the combined main validation expression). Splitting protocol/state checks into small functions resolved it; that failed compilation was never counted as a test result.

## Measured counterfactual outcome: do not promote this guard as a fix for the scientific failure

600 rows replay all four original arms, horizons1/8/32, caps32/160, charging one declared synthetic unit per guard decision. These are prefixes of already-known synthetic traces, not new held-out trials, real training or hardware measurements.

At horizon32 for C:

| Regime | Positive cases | Total net, synthetic units | Worst net | Discoveries lost vs original |
|---|---:|---:|---:|---:|
| Original frozen search |20/25|38510|-129|0|
| Guard, cap160 |20/25|38190|-143|0|
| Guard, cap32 |0/25|-590|-30|20|

The wide cap adds decision overhead without improving discovery. The tight cap limits losses but blocks all20 beneficial discoveries. Full results for horizons1/8/32 and every arm are published, including unfavorable rows. These results reject replacing the existing policy with this guard by default. No improved performance, model quality, universal payoff, or energy claim follows.

## Reproduce

From the repository root, on AArch64 Linux:

```sh
SEED_OMC=/absolute/path/to/qualified/seed/out/omc \
L523_OUT=/absolute/fresh/output/directory \
sh research/exploration-600/continuation/run.sh
```

The run refuses a wrong seed hash or existing output, verifies nonempty AArch64 ELF, then executes independent tests, compiled mutants, the known-trace replay and original experiment regressions. Full commands, source/binary hashes, raw outputs and replay rows are in `evidence/EX600-CONTINUATION-L523/`. Exact compiler reconstruction belongs to the pinned existing seed chain, not a new C-built compiler path.

## Disposition

Native guard correctness: PASS in the explicit bounded protocol. Economic replacement: REJECTED by the published replay. Original scientific result: FAIL unchanged. #600 remains open. A future improvement requires a separately frozen value-of-information/search-allocation experiment and genuinely untouched evaluation tasks; rerunning tuned choices on these known fixtures would not supply that evidence. Hardware/model qualification is NOT_RUN and energy UNMEASURED. No service/model/driver change or additional worker occurred.
