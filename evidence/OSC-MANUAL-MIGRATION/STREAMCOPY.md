# Compiler copy moved to seeded Omega

`seed/native/streamcopy.om` is a handwritten stdin-to-stdout copier. It uses one fixed 8192-byte buffer, handles short reads and partial writes, and accepts at most 8 MiB. Exit codes: 0 EOF/success, 2 read/write failure, 3 capacity refusal. EINTR is treated as an error. SIGPIPE retains the inherited Linux disposition. Streaming output can contain a prefix on failure; no atomicity, pathname, metadata-preservation, hostile-path or concurrent-writer guarantee is claimed.

The existing rung4 program named bytecopy reverses its input and ignores write errors; it was inspected but is not suitable for copying a compiler. The existing native byte comparator is reused, not reimplemented.

## Actual caller

`seed/omega-from-seed.sh` compiles and ELF-checks the copier with the born compiler. It replaces the `cp` invocation with the native copier, then verifies destination equality with the native comparator before writing the compiler fingerprint and continuing. Shell redirection and chmod remain external orchestration. This preserves the caller's ordinary-file scope; neither the old cp path nor this path provides atomic publication or protects a prior output from a failed overwrite. No live installed service/compiler is overwritten by this worktree gate.

## Evidence

- A compilable refusal stub failed 20 of 23 cases; implementation passes all 23.
- Cases cover binary data, boundaries around read buffers, a million-byte input, exact capacity, over-capacity, closed input, closed output and /dev/full.
- Three compiled mutants remove capacity/read-error/write-error enforcement and are detected; original source is restored and retested.
- Five isolated caller cases exercise intact boot, cp disabled, copier error, empty output and corrupt output. The old caller fails four requirements; the new one passes all five. Refusal diagnostics are checked explicitly.
- Wolfram independently provides the raw-byte SHA256 for all byte values and exact buffer/capacity arithmetic. The native copier preserves all byte values, and its output matches the Wolfram fingerprint. Exact evaluator input/result and the native check are retained.

Short-write handling is implemented but no test forces a positive partial write; /dev/full and closed stdout exercise non-SIGPIPE failures. Post-progress input failures and EINTR remain unforced. Full-repository and independent-review results must be read separately, not inferred from these focused gates.

The bootstrap's early cmp, shell/file utilities and loader remain on loan. This removes cp from one actual bootstrap caller, not from the system. No NVIDIA module, GPU or live service is changed.
