# L547: native continuation protocol correction

Parent personally resolves A1/A2 from independent review L541. Base is L523 `dc24fcf27bbc6c36ca6dfcac020452bb06004bae`. Original frozen exploration source/data and its scientific FAIL are unchanged. This is a protocol correction, not an economic improvement.

A1: native Omega now accepts all six ASCII whitespace separators promised by the contract, including VT and FF. A2: contract explicitly names the existing16,383-byte EOF-framed request maximum, including whitespace and leading zeroes. Requests at16,384 bytes and above still refuse20 without output. No unsigned arithmetic, admission condition, accounting, original sealed policy or replay input was changed.

New framing oracle has16 checks. On the original hash-qualified native executable, three actually fail: VT, FF and mixed whitespace. Other original length/nonseparator boundaries already pass. Corrected native executable:16/16 PASS. Cases include each allowed separator, mixed separators,16,382/16,383 valid lengths,16,384/16,385 refusals, NUL/non-ASCII/nonseparator refusals and a maximal leading-zero request. The runner now includes this test on every replay.

The first six-comparison expression was rejected by the pinned seed compiler at line53 with error12, producing an empty output. That log is retained as a development failure, never a test pass or mutant kill. Splitting the checks into nested bounded expressions compiled successfully; no compiler, warning or gate was changed.

Fresh final native runner exit0:423 original oracle checks,16 new protocol checks, all five successfully compiled behavioral mutants caught,600 known-prefix replay rows, and original2,337 checks/40 diagnostic checks/six original fault controls. Recomputed known-prefix CSV is byte-identical to the original published continuation replay. Original policy/freeze/amendments/300heldout rows/trace and all old evidence remain unchanged.

The seeded native compiler is reused at SHA256 `01b1545b23e8ff40cb5691e20b5df910ea4acaefde3495f0d62d270465b98cd6`. Test Python is an independent host oracle, not the decision runtime. Native artifact, source and mutation hashes are retained.

No discovery/payoff improvement is claimed. Default guard replacement remains REJECTED, original #600 scientific verdict remains FAIL, and this replay remains known synthetic trace truncation rather than a fresh held-out campaign. Real-model quality, performance, energy, runtime integration and hardware remain NOT_RUN/UNMEASURED. Independent corrective-head review is pending.

Reproduction:

```sh
SEED_OMC=/absolute/path/to/qualified/omc L523_OUT=/absolute/fresh/output \
 sh research/exploration-600/continuation/run.sh
```

Packaging note: the byte-identical600-row CSV is transported as `counterfactual-rows.csv.gz`, losslessly compressed (gunzip to recover the original bytes). The first attempt to stage its raw CRLF CSV failed the unchanged whitespace gate; compression preserves all bytes without relaxing that gate or changing original evidence.
