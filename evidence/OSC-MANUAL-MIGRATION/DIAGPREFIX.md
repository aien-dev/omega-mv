# Native diagnostic normalization and next-word fuzz integration

`seed/native/diagprefix.om` is handwritten Omega. It replaces `om4:` and `omc:` with `X:` only at the beginning of each LF-delimited line, preserving all other bytes, including NUL, CR, arbitrary non-ASCII bytes and an absent final newline. Input is bounded to 65536 bytes; the fixed buffer includes one overflow-detection byte. Exit 0 succeeds, 2 reports I/O failure, 3 refuses capacity. Input errors/capacity emit no output. Partial writes are handled; an output error may leave a prefix. EINTR is an error, not retried.

`seed/check/nextfuzz.sh` now builds the normalizer, comparator and ELF checker using its seeded reference compiler, validates their ELF shapes, and uses them for normalized diagnostic and emitted-byte comparison. Its former normalization `sed` and comparison `cmp` calls are removed. Raw diagnostic files are retained instead of edited in place. The generator, vocabulary, cuts/mutations and status accounting are unchanged.

## Qualification

- Refusal stub: 72 failures of 74 cases. Complete normalizer: 74/74 pass against both Python byte-regex and GNU sed reference output. Includes exact/over capacity, truncated prefixes, mid-line prefixes, multiple lines, binary data, read failures and output failure.
- Three successfully compiled mutants are caught: ignoring line-start position, wrong replacement byte, ignored read error. Original source is restored and retested.
- Wolfram independently checks bytes and SHA256 for a multiline example that must preserve a mid-line prefix; native output matches. Exact evaluator input/result retained.
- Old and new fuzz runners execute 20 generated programs and 4860 cut/changed inputs with zero bad results and identical complete exit-code histograms. Final-seed artifact membership and bytes also match. This is not a claim that every intermediate fixture from all seeds was separately archived.
- With sed/cmp blocked, the old runner refuses and the migrated runner still completes the 20-program run without either command being invoked.
- A deliberately wrong comparison compiler that appends a byte is caught on all 188 inputs in its one-program negative run; gate exits 1. This is a host test wrapper, not a production compiler or native implementation.

`nextfuzz-native.json` contains the exact baseline/new summaries and negative results. Fresh committed-source replay runs the native normalizer and the full 20-program fuzz after rebuilding the compiler from the seed. Default repository target results are separate.

## Boundary

The normalizer is specifically a diagnostic filter, not a general sed replacement. Fuzz program generation and mutation still use awk; orchestration, error detection, sorting and capture use host utilities. The helper compiler remains the supplied seeded REF, so comparison is consistency checking rather than independent DDC. This new helper belongs to the fuzz path; the bootstrap closure's existing fourteen fingerprints still cover its own unchanged input set. No compiler language extensions, other-owner source edits, live service changes or NVIDIA replacement are claimed.
