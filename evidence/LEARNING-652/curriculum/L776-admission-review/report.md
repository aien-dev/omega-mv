# L776 bounded admission independent consumer review

Reviewer: `/root/resume_learning_lead`, 2026-10-23. This is a narrow source/replay check requested by the existing L776 owner. Existing full independent-review and publication owners retain their roles. No peer source or live service was changed.

Disposition: no blocking findings in the declared public, in-memory admission-preparation scope. This does not qualify full #653/#654, candidate execution, generated curricula, authenticated source custody, protected records, durable resume or model learning.

## Exact inputs

- L776 source: `d8d14c47647e49774f64b58e3c138335656f175d`, exported with `git archive` from the corpus owner's repository.
- L775 dependency: `84d03c5523f39ca1045d1c313c5f62597779462b:src/learning/benchmark/core.om`, exported with `git show`, SHA-256 `a5a2e74c815f9ca6a90dbdfbfc7ecc343f0362c824064f2058e9d1d7c119c40c`.
- Admission module SHA-256: `533638a9f128853d696644de2bbf7b1859797b170a7e32e54c112ab94c3dadb1`.
- Owner test SHA-256: `21da3e4d5d3dec56df618931944d06d8c7e665d6e8ac8c63e63cfbdfc98a6257`.
- Actual compiler: `~/workspace/omega-learning652-lead-L775/build/L775/toolchain/build/bootstrap/omc`, rehashed before use as `191b776ef98821597c3e30655da6d680a9b66edbc9ceaca3c39c61ea6df098e9`.

## Checks and results

Read the exact module, owner tests, both replacement guard mutants, underlying benchmark implementation and admission README. Reviewed public partition checking, whole-record snapshot comparison, source-identity words, metadata refusals, observed-output classification, cost charging, duplicate submission, count bounds and completion gating.

Production and both mutants were rebuilt from the immutable exports in this private review directory. All compiler commands exited 0. Each output was independently identified as a static AArch64 ELF before execution. All three rebuilt executable hashes match the author's saved artifacts exactly.

| Replay | Actual result |
|---|---|
| Production | exit 0, exact `CURRICULUM_ADMISSION_PASS` plus newline, no stderr |
| Provenance-check bypass | compiled successfully; exit 83, no stdout/stderr |
| Source-identity-check bypass | compiled successfully; exit 93, no stdout/stderr |
| Independent boundary probes | compiled successfully; exit 0, no stdout/stderr |

Independent `.om` probes verify that accepted indices continue to identify the frozen public records after the caller changes live `LT` scope/source fields, all 64 allowed records can be admitted and finished without overflow, a source-declared output bound below the required 24 bytes is quarantined, and an invalid record index is charged and invalidates completion. A second finish correctly returns an error. The README's explicit requirement to consume `CFreeze`, rather than a subsequently edited `LT`, is essential to the observed guarantee.

The API cannot authenticate a hostile caller that supplies its globals, measure the source bytes, or prove independence of caller-supplied expected observations. These limitations are explicit in the source and README. Synthetic costs/metadata in tests are test inputs, not real benchmark receipts. Historical original compile failures remain in the owner's evidence; no new compilation failures occurred in this review.

## Reproduction and raw evidence

Review root: `~/handoffs/learning-652/lead/admission-review-d8d14c47/`.
`source/production.om` concatenates core, module and owner tests. `source/provenance.om` and `source/identity.om` replace only the module with the respective frozen mutant. `source/independent-combined.om` concatenates core, module, owner helpers before `main`, and independent `.om` probes.

Each build used the existing tools with this form, substituting the four source names:

```sh
/usr/bin/time -o raw/production.compile.time -f 'exit=%x elapsed_s=%e user_s=%U system_s=%S peak_kib=%M' quiet-build /home/drakestapleton/workspace/omega-learning652-lead-L775/build/L775/toolchain/build/bootstrap/omc < source/production.om > raw/production.elf 2> raw/production.compile.stderr
file raw/production.elf
chmod +x raw/production.elf
/usr/bin/time -o raw/production.run.time -f 'exit=%x elapsed_s=%e user_s=%U system_s=%S peak_kib=%M' timeout 10 raw/production.elf > raw/production.run.stdout 2> raw/production.run.stderr
```

Raw command exit/time/resource receipts, output and stderr are under `raw/`. Timing is coarse process observation only, not a throughput or cost benchmark. `artifacts.sha256` pins exported/assembled `.om` source and rebuilt executables. The independent probe source is `c58c58ddd4d2c73d37602df6bc37be6b8d50ff49ac62c916a33916c0bf733f4d`; its executable is `423f3c14b036d117d738ca6111d40f207d18ffa7aa06156b656156fef345948e`.

The new Xerj workflow skill was read. No Xerj tools were present in this already running session; exact-path and immutable-source reads were used. No index, owner, worker or search service was changed.
