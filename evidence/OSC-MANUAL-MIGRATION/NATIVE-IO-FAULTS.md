# Real-kernel I/O fault qualification for the native replacements

`tests/seed-migration/native-io-faults.py` closes two prior verification gaps without modifying the production Omega programs. It builds the unchanged SHA256 and streamcopy sources using the pinned seed-born compiler, then exercises real Linux file descriptors, not mocked read/write results.

## Four cases

1. SHA256 reads a nonempty prefix from a nonblocking pipe whose writer remains open. The next read receives EAGAIN. Required: exit 2 and no digest.
2. Streamcopy reads the same prefix, copies it, then receives EAGAIN. Required: exit 2 and exactly the consumed prefix, consistent with its nontransactional stream contract.
3. SHA256 reaches its final digest write with stdout opened on /dev/full. Required: exit 2 rather than success.
4. Streamcopy reads an 8192-byte buffer from a regular file. Stdout is a nonblocking pipe restricted to 4096 bytes and deliberately not drained until the child exits. Its first write transfers a positive prefix; completing the remaining write encounters EAGAIN. Required: exit 2 and exactly the correct 4096-byte prefix.

Wolfram independently computes the prefix SHA256 and input-prefix size. Exact evaluator input/result is in `native-io-wolfram.json`; the executable test compares its actual output with this receipt as well as the byte oracle.

Three deliberately faulty, successfully compiled Omega programs must fail these checks: SHA ignores its read failure, streamcopy reports read failure as success, and streamcopy assumes a partial write consumed the whole buffer. The last mutant exits 0 after losing the suffix and is rejected. Mutants are compiled in private temporary directories; no tracked production source or shared compiler is modified.

## Scope and remaining gaps

This qualifies failure after real I/O progress and the partial-write accounting path. It does not force EINTR, prove successful recovery from transient I/O, or guarantee that a pipe-writer fragmentation test yields a particular sequence of short reads. These commands intentionally fail closed on EAGAIN/EINTR rather than implementing retries. The historical review's positive-short-write and post-progress-read concerns are covered for these failure paths; positive partial writes followed by eventual successful completion remain untested.

Host Python creates descriptors and supplies independent test oracles. It is not linked into either Omega binary. No production code was replaced in this verification step; the existing replacements were tested more rigorously. Whole-system migration and live NVIDIA replacement remain incomplete.
