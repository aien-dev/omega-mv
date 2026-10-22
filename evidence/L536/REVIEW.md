# L536 parent re-review: L531 receipt and register-reuse corrections

Exact reviewed/pushed source branch `issues/receipt-reuse-review-fixes-20261022`, final head `63a92c55b9ab6f76bdbe47e4a85ad85bb85bc849`. Source milestones `8c53f3c220c88363968ab33fad0855f2901c545b` and `95fad27653677ae1220af76d0c4e5039eb7859bf`; later changes are evidence/generated metadata. Exact remote ref and clean author tree confirmed. Parent personally reviewed production converter, strengthened oracle, CSV adversarial suite and compiled-mutant harness, and ran the fresh checks below. No additional review worker.

## Source disposition

**C1/C2/C3/A1 resolved within the bounded host correction.** Original `85d8343fa04647b198e3ddda00ad8e448837f9ca` remains HOLD without this follow-on. This is not issue closure or approval of real-model/chip/timing qualification.

- **C1:** Input is captured once into a private read-only snapshot. The snapshot's raw SHA256 and one jq invocation bind validation and output to the same bytes. Exactly one top-level object is required; empty/array/null/multiple/invalid streams refuse. The adversarial suite also changes the original pathname at the first jq invocation and verifies output and digest still come from the captured bytes. Existing output is preserved on invalid input.
- **C2:** Required machine metadata uses the documented 1..64 ASCII identifier subset with initial alphanumeric character. Formula-prefix, whitespace, quotes/comma and non-scalar cases refuse. This deliberately rejects identifiers outside that grammar rather than claiming CSV quoting neutralizes formulas. Genuine aarch64/x86_64 output remains unchanged. Output directories, symlinks, FIFOs and devices refuse; atomic same-directory publication uses `mv -T`. Directory ancestors/process UID are trusted as explicitly documented.
- **C3:** Checks/counts/timing fields require producer decimal integer tokens and a finite exact-integer bound of9007199254740991, with existing positivity/equality requirements. The literal-preserving jq capability check fails closed rather than silently rounding a near-bound fraction or underflow. These intentional bounds are stricter than arbitrary uint64 values; they are not a claim to accept every possible long-running measurement. Missing/invalid types, fractional/overflow and numeric-string controls refuse.
- **A1:** Oracle expectation now captures count, vreg, interval endpoints, widths and pair/quad attributes before solving. Safety checks use the saved inputs and refuse solver mutation of that metadata. Tests require feasible disjoint reuse, exact full allocation and one-more refusal for widths1/2/4, and mixed-width fragmented reuse/refusal. Capacity is independently counted from software register bounds/alignment; it is not GPU occupancy. Production allocator source is unchanged. The previously surviving no-reuse mutation is now rejected by actual behavioral checks, including width-specific mutations.

## Parent fresh execution

Output directory `/home/drakestapleton/handoffs/omega-issue-sweep-20261022/L531-parent-20261022T201347Z`.

```sh
quiet-build timeout 90s bash tests/m1/block3_csv_adversarial_test.sh <parent>/csv
quiet-build timeout 30s tests/m1/block3_csv_test.sh
quiet-build timeout 150s make OUT_DIR=build/L531-parent test-gpu-regalloc-lifetime
git diff --exit-code 85d8343fa04647b198e3ddda00ad8e448837f9ca..HEAD -- src/
```

All commands exited0. Actual outputs:

- CSV adversarial136 assertions,0 failures; original success plus six malformed-receipt refusals pass. The original tests now use regular output targets so receipt refusal is not accidentally replaced by special-file refusal.
- Lifetime oracle27000 original exhaustive cases pass.
- Scalar:127 disjoint intervals reuse, exactly126 simultaneously live allocate, one-more refuses.
- Pair:64 disjoint reuse, exactly63 allocate, one-more refuses.
- Quad:32 disjoint reuse, exactly31 allocate, one-more refuses.
- Mixed fragmentation feasibility/refusal pass;11 direct input/output corruption controls rejected.
- Eleven source-mutated allocators compiled successfully, then each failed an expected behavioral assertion with exit1. No timeout/build failure counted as detection.
- No production `src/` diff from the reviewed base; author tree remains clean.

Raw parent logs are in `evidence/L536/`. Parent did not rerun the author's full sanitizer, five-golden,1304-plan, or simulator334 gates here; those remain separately published author evidence, not parent replay claims.

## Remaining trust and qualification

The converter validates self-reported matching source/binary pins. It does not authenticate signatures, independently attest execution or prove machine identity. Original stricter plan gate/G1 remains FAIL; successful CSV conversion is not scientific qualification. Retained bytes and isolated lookup remain UNKNOWN, wall energy UNMEASURED. Stable schedule superiority, real-kernel allocation/occupancy and hardware/current-model qualification remain NOT_RUN. No issue520/574/576/600 closure is authorized.

Desk should carry the corrected converter/test follow-on into any integration of L511/L519 descendants before describing the old host evidence boundary as reviewed. Source-defect re-review resolved here; integration and broader acceptance remain with existing owners.
