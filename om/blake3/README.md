# blake3.om — BLAKE3 in seeded Omega

`blake3.om` is a program-mode Omega port of `src/omega_blake3.c` (BLAKE3 default mode:
unkeyed, 32-byte output, no derive-key). It hashes one file, or stdin when no argument is
given, and prints 64 lowercase hex digits plus a newline — the same contract as
`tools/zero-legacy/sha256.om` in aien-architecture-mv. Part of #726 and
aien-dev/aien-architecture-mv#300 (zero-legacy migration).

## Build

```
OMC=/path/to/seed/out/omc     # pinned: sha256 734f0a39319fbfa930bcc62cb30dbf06d47d78e126372b74f5baf478e6126b61
$OMC < om/blake3/blake3.om > blake3 && chmod +x blake3
./blake3 FILE          # or:  ./blake3 < FILE
```

Exit codes: 0 hash written; 65 input cannot be read (missing file, no permission,
directory, read error); 66 input longer than 16 MiB (16,777,216 bytes is accepted,
16,777,217 is refused). Messages go to stderr.

Returned stdout write errors or zero-progress writes exit 74. Positive short writes
resume at the next byte; returned negative results, including interrupted writes,
fail closed. Signal termination (for example the default SIGPIPE action) is not
reported as exit 74.

Run the output-contract regression against a freshly compiled binary:

```
sh om/blake3/test-output.sh ./blake3
```

The fixture compares the complete empty-input digest, including its newline, and
checks /dev/full and closed-stdout failures. Positive short writes and EINTR are not
forced by this fixture.

Native run on the Spark (arm64 host, 2026-10-25), omc 734f0a39…6b61 built by
`sh seed/omega-from-seed.sh`: this source (sha256 c7c96a26…a150) compiles to an
8,664-byte binary, sha256 f3e343d2…2dbd. `test-output.sh` exits 0 on it and 1 on the
PR #732 binary (c51199a3…f77b), which exits 0 for /dev/full (ENOSPC) and closed
stdout (EBADF). A forced positive short write (`ulimit -f 1`, SIGXFSZ ignored, 994-byte
file opened for append) shows `write(1, …, 65) = 30` then `write(1, …+30, 35) = -1
EFBIG` and exit 74. The 64-case corpus below, rerun against this binary with a fresh
-O2 build of `src/omega_blake3.c` as oracle, gives 64/64, and the binary matches
`tests/resolve/blake3_vectors.txt` 35/35.

## Construction

Same as the C: 1024-byte chunks of 64-byte blocks, CHUNK_START on a chunk's first block,
CHUNK_END on its last, a binary tree of PARENT nodes merged eagerly with a chaining-value
stack (64 rows of 8 words; 16 MiB needs at most 14 levels, the 54-deep bound of the
reference holds trivially), ROOT on the single final compression. 32-bit words live in
`ulong` cells and are masked with `4294967295` after every add; the 64-bit chunk counter is
split into words 12 and 13 of the state. The message permutation runs 7 times per
compression, as in the C.

## Historical oracle and parity (PR #732)

Oracle: `src/omega_blake3.c` compiled with gcc -O2 behind a 12-line C driver (file → hex,
kept out of the tree). The driver was first checked against the official
BLAKE3-team/BLAKE3 `test_vectors/test_vectors.json` (fetched 2026-10-24): all 35 cases
match, and the file is identical to the repo's `tests/resolve/blake3_vectors.txt`.
Two single-line mutants of the C (the B3M:root and B3M:merge tags) are caught by the
vector set (35/35 and 16/35 vectors differ), so the corpus discriminates.

The following corpus and hashes belong to the original source at commit
`c2156c7069df619797a113582fa6ad658c3db999`, before the output-handling fix.
They are not verification of the revised source or binary.

Corpus run with omc 734f0a39…6b61, `blake3.om` sha256 79857ee8…baa3, output binary
sha256 c51199a3…f77b (8,660 bytes). Every row compares the .om output byte for byte to
the oracle (or the exit status where that is the contract).

| Case | Count | Result |
|---|---|---|
| Official vectors, input byte i = i mod 251, lengths 0 1 2 3 4 5 6 7 8 63 64 65 127 128 129 1023 1024 1025 2048 2049 3072 3073 4096 4097 5120 5121 6144 6145 7168 7169 8192 8193 16384 31744 102400 | 35 | 35/35 identical |
| Random files (getrandom), lengths 1048576 747415 857706 158155 834790 310298 179313 1010110 85785 92852 94544 571487 362064 1015847 785532 857018 943647 792807 210568 211540 | 20 | 20/20 identical |
| stdin path (102400-byte vector, 1 MiB random) | 2 | 2/2 identical |
| Capacity edge: 16,777,215 and 16,777,216 random bytes | 2 | 2/2 identical |
| Over capacity: 16,777,217 zero bytes, file and stdin | 2 | exit 66 both |
| Missing file, mode-000 file, directory | 3 | exit 65 all |
| **Total** | **64** | **64/64** |

Historical timing of the original PR #732 source on the Spark (arm64): 16 MiB in 0.33 s for the .om versus 0.05 s for the -O2 C.

## Limits

- Input is capped at 16 MiB (one `global uchar 16777216`, the largest array the language
  allows); the C has no cap. Longer inputs exit 66 instead of hashing.
- One-shot only: no incremental/streaming API, no keyed or derive-key modes, no extended
  output — the C exposes none of these either.
- A read error after some bytes were consumed exits 65 (the C driver has no such path to
  compare against; the status is a definition, not a parity result).

## Consumer note (cutover)

`src/omega_blake3.c` is a member of the live `libomega_gpu.a` (the aien-proof
EvidenceReceiptV1 id is BLAKE3 over the receipt bytes, and the resolver recomputes it).
Replacing that object with this .om is blocked on the .om object / C-ABI boundary: a
seed-compiled Omega function has to be callable from C with a stable signature. That
boundary is omega-bridge PR #8 (Native call ABI v1: one seed-compiled Omega function
called from C, C++, Python, Rust). Until it lands, `blake3.om` is the parity oracle and
the migration unit for the C file, not its replacement; the C stays in the closure.

## NOT_RUN

- No GPU, no live-server, no `make test` run was needed or performed for this change.
- The 54-deep parent-stack bound of the reference is not exercised (a 16 MiB cap reaches
  depth 14); the stack has 64 rows and the loop bound is 64.
