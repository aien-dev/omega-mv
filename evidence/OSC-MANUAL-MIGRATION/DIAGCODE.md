# Native diagnostic exit-code validation

`seed/native/diagcode.om` replaces the diagnostic `grep -q` in `seed/check/nextfuzz.sh`. The fuzzer builds it with its seeded reference compiler, ELF-checks it, writes the actual compiler exit code to a private work-file and supplies it on fd3. Exit-zero compiler results bypass diagnostic validation as before; statuses >=128 remain crashes.

## Contract

- fd3: canonical decimal 1..127, no newline, at most three digits. Empty, leading-zero, non-digit and out-of-range codes refuse.
- stdin: at most 65536 bytes. Reads to EOF before checking, so a match before a later input error does not turn the error into acceptance.
- Accept the existing GNU grep contract: an occurrence of `line [0-9]*: error CODE` ending immediately at LF or EOF. The start is intentionally unanchored and the line-number digit run may be empty, matching the historical gate. This is not a complete diagnostic grammar or a new stricter compiler-message policy.
- Compare the expected code's bytes exactly; wrong codes, leading zeros and extra trailing digits/text do not match. CRLF is not LF-equivalent.
- Silent statuses: 0 match; 1 absent; 2 I/O; 3 diagnostic capacity; 4 malformed expected code. Expected-code validation precedes diagnostic reading. Short reads are accumulated; EINTR refuses, not retried.

## Evidence

- `diagcode.py`: 129 cases; compiling a refusal stub produces 66 failures, real source produces zero. For ordinary matching cases Python byte regex and GNU grep independently agree. Boundary, invalid-code and missing-fd refusals have explicit expectations.
- Three successfully compiled mutations are rejected: ignored line-end anchor (27 failures), ignored code bytes (9), ignored upper range (2). Original source is restored and retested.
- Five integration controls in `diagcode-fuzz-integration.json`: baseline; baseline with retired calls blocked; migrated runner with those calls blocked; wrong output compiler; wrong reference diagnostics with NO comparison compiler. The last control isolates the new validator so the existing binary/diagnostic comparator cannot mask a missing check.
- Baseline and migrated runners agree on 20 programs, 4860 cut/changed inputs, zero bad results and the complete exit-code histogram. The final generated seed's fixture bytes/membership agree too; not every intermediate seed's files are separately archived.
- Blocking diagnostic `grep -q`, sed and cmp makes the old runner fail and leaves the migrated runner passing without blocked invocations. The remaining histogram `grep .` is deliberately allowed and still a host dependency.
- A wrong reference message code is caught on all 182 refusing inputs of the one-program run (188 total, six successful). Wrong compiler output is caught on all 188 inputs.
- Seven Wolfram cases independently confirm LF/EOF, suffix, leading-zero, wrong-code and inherited unanchored/empty-line-number behavior. Exact evaluator input/results and native comparison retained.

Fresh committed-source reconstruction/replay and default repository test receipts are separate. Independent source review is tracked by L587; publication alone is not a review verdict.

## Remaining boundary

Production validator and matching are handwritten seeded Omega; Python, GNU grep and Wolfram are external qualification oracles only. Shell, awk generation, histogram tools and the host OS remain. The compiler source and its bootstrap closure fingerprint set are unchanged. No live GPU/NVIDIA module or service change, full migration completion, merge or deployment is claimed.
