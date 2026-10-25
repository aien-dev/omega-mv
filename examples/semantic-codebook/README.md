# Seeded Omega local semantic short-code smoke

**Status: PROPOSED / compiler and native execution NOT_RUN.** Source: dispatch-v0.om; exact synthetic dictionary: codebook-v0.txt; proposed contract: [semantic-codebook-v0.md](../../spec/semantic-codebook-v0.md). Work: [Omega #721](https://github.com/aien-dev/omega-mv/issues/721), [Architecture #315](https://github.com/aien-dev/aien-architecture-mv/issues/315).

## The synthetic 256-bit root

The UTF-8 bytes of codebook-v0.txt including the final newline hash to SHA-256:

    a4d36c23819c865d1d3ae271508fee7e38c1919f62852821ac19f80c3714b0f3

Its little-endian unsigned 64-bit limbs are:

    6739245970865574820
    9146405468906863133
    2389306260950466872
    17559557173790906796

Independent external fixture check (not product code):

    sha256sum examples/semantic-codebook/codebook-v0.txt

The program compares all four limbs and one epoch; it does not independently authenticate the supplied root, validate full Omega canonical semantic identities or check capability authority.

## Expected input/result test cases

All admitted results are u8 values encoded as unsigned u16 values from 0 through 255. Rejected operations use values above 65280, so a valid u8 result is not confused with a refusal. This encoding is **only for this toy**, not a general production result ABI.

| Case | Short code | Epoch | x | parameter | Expected result |
| --- | ---: | ---: | ---: | ---: | ---: |
| add one | 0 | 9 | 41 | 0 | 42 |
| multiply two | 1 | 9 | 21 | 0 | 42 |
| twice plus parameter | 2 | 9 | 20 | 2 | 42 |
| wrapping add | 0 | 9 | 255 | 0 | 0 |
| wrapping multiply | 1 | 9 | 128 | 0 | 0 |
| wrapping affine | 2 | 9 | 127 | 2 | 0 |
| stale epoch | 0 | 8 | 41 | 0 | 65281 (FF01) |
| unknown code | 3 | 9 | 41 | 0 | 65282 (FF02) |
| changed root limb | 0 | 9 | 41 | 0 | 65283 (FF03) |

## Owner-run native test (AArch64 Linux, no GPU required)

Execute only on a claimed isolated, source-pinned Omega worktree. From the repo root:

    sh seed/omega-from-seed.sh
    mkdir -p build/semcode
    seed/out/omc < examples/semantic-codebook/dispatch-v0.om > build/semcode/dispatch-v0
    chmod +x build/semcode/dispatch-v0
    timeout 5 build/semcode/dispatch-v0
    echo "$?"

Expected compile rc=0 and program exit rc=0. This command sequence was **NOT_RUN** by the creator of this GitHub branch. The harness tests nine cases internally and returns a distinct nonzero status when any check fails. The native compiler may reject or miscompile a new program; if so record the exact error and fix through the existing seed/compiler owner. Do not infer PASS from GitHub file creation.

The code uses the currently documented seed grammar: fixed struct, view parameter, function calls, u8/u16/u64 arithmetic, comparisons, conditional returns. If a call/struct capability is missing at the chosen pinned seed profile, stop with MISSING_CAPABILITY and the relevant existing seed owner; no foreign product fallback.

## First independent review requirements

1. A *second* checker reconstructs the synthetic hash from the exact fixture bytes, independently computes all nine outcomes including unsigned-u8 wraparound, and catches mutated expected values.
2. Verify the source actually compiles and returns zero on the named pinned AArch64 seed compiler. Record SHA256 of source, compiler binary, compiled binary, exact command/host identity and actual stdout/stderr.
3. Audit why a caller-provided matching root is **not** authentication. Production admission must verify full canonical Omega semantic/contract identity, signed evidence and capability before any short reference becomes executable.
4. Keep short-code compressed signaling and program-body factoring separate. The toy does **not** reduce final executable size, remove LLM/35B weights or lower measured RAM bandwidth.
5. File additional adversarial cases under [#721](https://github.com/aien-dev/omega-mv/issues/721) after formal contract freeze, not by changing these fixture bytes silently.

This starter PR is an implementation **example**, not a verified project-wide semantic-compression feature.