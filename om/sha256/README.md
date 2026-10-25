# om/sha256: SHA-256 in Omega, linked into the product

`src/sha256.c` no longer computes SHA-256. The algorithm (initial hash value, buffering, the
compression function, padding, digest) is `sha256.om`, compiled by the seed-born `omc` and called
in-process through the omega-bridge native call ABI v1 (aien-dev/omega-bridge #8, #12). Every
`sha256_*` caller (about 400 call sites, all of `libomega_gpu.a`, `omegatool`, the `mk/*.mk` tools)
is unchanged: the header and `sha256_ctx` are the same.

| file | role |
|---|---|
| `om/sha256/sha256.om` | the implementation: one function-mode unit, entry `omsha` |
| `src/omega_om_units.inc` | generated: the unit's machine code, byte for byte as omc wrote it, plus the ABI binding (`omega_omsha`) |
| `src/sha256.c` | `sha256_init/update/final/hash`: cuts caller data into pieces of at most 4096 bytes; no hashing logic |
| `om/units.sh` | regenerates the `.inc` (all Omega units: sha256, blake3); `--check` proves the committed file is what the pinned tools produce |
| `om/sha256/parity/` | parity against the replaced C (`run.sh`) and a throughput probe (`bench.c`) |

Pins: omc `734f0a39…6b61`; generator `src/native.om` of omega-bridge main `c4013511`
(sha256 `e79733f8…f80f`).

## Why a 4096-byte adapter remains

ABI v1 arrays have a length fixed by the Omega type, so `sha256_update(ctx, data, len)` with any
`len` cannot be one Omega call. The adapter passes whole 4096-byte pieces straight from the caller's
buffer and copies only a final partial piece into a 4096-byte stack buffer (only its first `len`
bytes are read). The bytes it passes and
the order of calls are the only things it decides. The adapter goes away with a variable-length
array in the ABI.

## Evidence (2026-10-25, GB10 host, no GPU)

- Parity (`parity/run.sh`): every length 0..9000 one-shot and split, 2,000 inputs (boundary lengths 0..4097, 982 random 0..299 bytes, 1,000
  random up to 2 MiB), each one-shot and in random `update` splits, equal to the C oracle
  (src/sha256.c @ a647b6a9); FIPS vectors empty / abc / 448-bit / one million `a` correct.
- `make all libomega_gpu` with `-Werror`: clean; `nm build/libomega_gpu.a` has `ombr_code_omsha`
  and `omega_omsha`, no `sha256_transform`.
- `make test`: output byte-identical to the unmodified baseline build.
- Host gates on branch and baseline, same verdicts: see the PR.

## Limits

- Throughput, GB10 host (`parity/bench.c`, 64 MiB and 200,000 x 32 bytes): 233.7 MB/s against
  202.7 MB/s for the C it replaced, and 0.32 us against 0.34 us per 32-byte `sha256_hash`. The
  speed comes from the source, not from the compiler: `uint` arithmetic (wraps mod 2^32, no masks),
  the 64 rounds unrolled with the eight working words in locals whose names rotate (no moves, no
  stores), K as immediates, blocks read straight from the caller's buffer, and a one-shot path
  (op 3) for inputs under 4096 bytes that never touches `sha256_ctx`. The first port ran at
  37.7 MB/s; every Omega call spills and reloads the caller's live values, so helper calls in the
  rounds cost most.
- After `sha256_final` the context's buffer and count fields differ from the C (padding is built
  in place and the count is not advanced). No caller reads a finished context; digests are identical.
- A trap or refused argument from the unit aborts the process (it can only mean a broken
  invariant: n <= 4096, valid op, fixed sizes).
- Each calling thread gets an 8 MiB stack plus 8 MiB guard mapping from the binding on first use.
