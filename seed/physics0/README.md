# PD0 deterministic streams in Omega

This is a handwritten, additive `.om` successor for the **stream primitive
slice** of [`src/physics0/pd0_rng.c`](../../src/physics0/pd0_rng.c): FNV-1a64 tag
hashing, SplitMix64 state/next, stream initialization, and uniform micro-units.
`stream.om` is an actual native consumer of that primitive, not generated C or
a source-extension rename. It is designed to run without a C runtime when compiled with the
pinned seeded compiler below. Native compilation/execution of this candidate
is **NOT_RUN**; that runtime property still requires qualification.

The C world, planner, scorer, fixed-point arithmetic, constant sampling and
noise consumers are **not switched or deleted**. This is not a whole PD0 port,
a drop-in C ABI, a cryptographic RNG, or a production qualification claim.
The existing PD0 owner and tests remain authoritative for those consumers.

## Contract

- `rngtag[256]` holds bytes. `rnginit(seed, n)` accepts `n` in 0..256 and exactly
  `n` **nonzero** bytes. Empty tags are valid. High-bit bytes are unsigned.
  These valid tags match the C string consisting of those bytes plus a trailing
  NUL. Longer strings and embedded NULs are deliberately outside the bounded
  adapter contract, not claimed to have the C function's truncation behavior.
- Initialization returns 0 and sets `rngstate[0] = seed XOR FNV1a64(tag)`, or
  returns 2 on a bad tag without changing the prior state. The 256-byte buffer
  is caller-owned; its unused tail is ignored. One global stream is supported;
  it is not reentrant or thread-safe. All 64-bit seeds are accepted.
- `rngnext()` increments state by `0x9e3779b97f4a7c15` modulo 2^64, stores that
  incremented state, then applies the exact existing SplitMix64 mixing steps.
  `rnguniform()` consumes exactly one next word and returns word `% 1000001`.
- Omega's arithmetic must not accidentally replace C unsigned wrapping with
  overflow traps: `rngadd` uses 32-bit halves, `rngmul` four base-65536 columns.
  Addition intermediates are below 2^34. A multiplication column contains at
  most four products <=65535^2 plus carry <262144, hence is below 2^35. Masked
  digits alone are shifted into their final positions; no shift is by 64.
- `request.om` adapts a request of **8 little-endian seed bytes followed by
  0..256 nonzero tag bytes**. `rngreqok(n)` validates length before indexing and
  preserves stream state on refusal. No terminator, count, or newline is sent.
- `stream.om` reads that request through EOF and emits **eight consecutive
  little-endian 64-bit words** (64 bytes). It handles partial reads/writes;
  a 265th byte detects oversized requests. Exit 0 means success, 2 malformed
  or oversized input, 4 I/O error. Malformed input produces no output. An I/O
  error during output can leave a partial response; it is not transactional.

## Source and compiler pins

C baseline and PR base: `d863780eb42bd940349eba61a3d1c78b0b819941`.
`src/physics0/pd0_rng.c` SHA-256:
`957efcd03de3d3153e4e46347969bd3649ebc84af95cf9f2e8c616a4e20ebf24`.
The contract is also recorded in `src/physics0/pd0_rng.h`; generators remain
isolated in `pd0_gen.c` and are not linked into this work.

Use the existing compiler owner's Linux AArch64 seed profile, not an old C
frontend or a new compiler implementation:

- Repository commit `8921a65204c2daf6d66a61813e89854c145713da` (PR #676).
- `seed/omc/omc.om` Git blob `8e73c4f2c1d6736222f27ecd6dfdf796c27b4213`.
- Compiler source SHA-256
  `fe67a0c31e42fcf6d86fce786b0a16b46a95daf3c7da1af7b6974df68ecfe400`.
- Follow that checkout's `seed/README.md`, `seed/host/README.md`, and existing
  `seed/omega-from-seed.sh` reconstruction/fixpoint procedure in a disposable
  compiler checkout. The initial loader is the existing documented exception;
  no C compiler, assembler or linker builds this candidate.

This source uses only integer scalars, bounded global byte/word arrays,
functions, loops, shifts, and read/write. Global/text/local identifiers stay
within eight bytes. It adds no host operations, shared compiler changes,
compiler binaries or bootstrap scripts to main. A correctly rebuilt/pinned
compiler is a prerequisite, not supplied or certified by this PR.

## Native qualification procedure (NOT_RUN here)

On authorized Linux AArch64 hardware, after checking the compiler pin and
successfully reconstructing the existing compiler, set `OMC` to its absolute
`seed/out/omc` path. From this candidate checkout, compose the handwritten
sources in dependency order into a disposable directory:

```sh
(
set -eu
[ "$(uname -m)" = aarch64 ]
: "${OMC:?Set OMC to the qualified pinned compiler's absolute path}"
B=$(mktemp -d)
cat seed/physics0/rng.om seed/physics0/request.om seed/physics0/unit.om > "$B/unit.om"
"$OMC" < "$B/unit.om" > "$B/unit"
LC_ALL=C readelf -h "$B/unit" > "$B/unit.elf"
grep -q 'Class:.*ELF64' "$B/unit.elf"
grep -q 'Machine:.*AArch64' "$B/unit.elf"
grep -q 'Type:.*EXEC' "$B/unit.elf"
chmod +x "$B/unit"
"$B/unit"                    # require exit 0, not just successful compilation
cat seed/physics0/rng.om seed/physics0/request.om seed/physics0/stream.om > "$B/stream.om"
"$OMC" < "$B/stream.om" > "$B/stream"
LC_ALL=C readelf -h "$B/stream" > "$B/stream.elf"
grep -q 'Class:.*ELF64' "$B/stream.elf"
grep -q 'Machine:.*AArch64' "$B/stream.elf"
grep -q 'Type:.*EXEC' "$B/stream.elf"
chmod +x "$B/stream"
printf 'Candidate test artifacts: %s\n' "$B"
)
```

Stop on any failing command; do not run a partially emitted executable. The
shell here only composes/launches files, not implements the primitive. No new
shell/Python/C automation is added. The current x86_64 authoring environment
has **not** run these commands or any bundled AArch64 program.

`unit.om` checks raw state 0/max (including increment wrap), modular carries
and products, both seed extremes for empty/ASCII/high-bit/capacity tags,
38 fixed C next-word answers, state advancement, one-draw uniform behavior,
refusal-state preservation, short/oversized/NUL requests, a mixed-byte seed,
and response packing with independently fixed first-word bytes.

Before qualification, also exercise the compiled stream via real stdin/stdout:
empty/7-byte requests -> exit 2 and zero bytes; 8 zero bytes -> exit 0 and 64
bytes beginning `30 ff a4 6b 01 7c 81 c3`; all-FF seed with 256 `a` bytes ->
exit 0; a 265-byte request or NUL tag -> exit 2 and zero bytes. Check interrupted
and partial I/O, and descriptor failures (exit 4), under the existing owner's
native I/O test facilities. In a disposable source copy, alter the increment,
first FNV multiplier, or seed byte order separately; the unchanged unit test
must return nonzero. Retain exact source/compiler hashes and outcomes. These
native positive, refusal, I/O and mutation runs remain **NOT_RUN**.

## Checks actually performed

On x86_64, the unmodified pinned C source was compiled with GCC solely as a
disposable reference. A separate checked-integer host model of the handwritten
limb formulas matched 10,000 modular add/multiply pairs, 1,000 nonzero byte tags
of lengths 0..256, and 32,000 sequential C words **and states**. All 38 committed
next-word constants were compared with that actual C baseline; identifier
bounds and whitespace checks passed. The disposable model/harness is not a
product implementation, compiler, or substitute for running `.om`.

Independent review also compared 100,000 arithmetic pairs and 48,000 words and
states across 1,500 tags against a separately GCC-built C baseline. This is
source/model evidence only. Native compile, native unit/consumer execution,
compiler regression gates, signed hardware evidence, and production-consumer
qualification are **NOT_RUN**. Do not delete C or claim migration completion
until the existing owners qualify and switch the real consumers.
