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
| `om/sha256/gen.sh` | regenerates the `.inc`; `--check` proves the committed file is what the pinned tools produce |
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

- Parity (`parity/run.sh`): 2,000 inputs (boundary lengths 0..4097, 982 random 0..299 bytes, 1,000
  random up to 2 MiB), each one-shot and in random `update` splits, equal to the C oracle
  (src/sha256.c @ a647b6a9); FIPS vectors empty / abc / 448-bit / one million `a` correct.
- `make all libomega_gpu` with `-Werror`: clean; `nm build/libomega_gpu.a` has `ombr_code_omsha`
  and `omega_omsha`, no `sha256_transform`.
- `make test`: output byte-identical to the unmodified baseline build.
- Host gates on branch and baseline, same verdicts: see the PR.

## Limits

- Throughput: 120 MB/s against 203 MB/s for the C (1.7x), 1.9 us against 0.34 us for a
  32-byte hash. Callers in the live closure hash generated kernel code (KB), ids and evidence
  records, not per-token data. Inlining the rounds and sigma functions (every Omega call spills and
  reloads the caller's live values) took it from 37.7 to 120 MB/s; widening the piece from 64 to
  4096 bytes gained little. A small hash still pays three ABI entries and two K-table builds
  (function-mode units have no read-only data).
- The stale bytes of `sha256_ctx.buffer` past the pending index can differ from the C after the
  same updates (the C copies only the final partial block; the adapter copies every partial piece).
  No caller reads them; digests are identical.
- A trap or refused argument from the unit aborts the process (it can only mean a broken
  invariant: n <= 4096, valid op, fixed sizes).
- Each calling thread gets an 8 MiB stack plus 8 MiB guard mapping from the binding on first use.
